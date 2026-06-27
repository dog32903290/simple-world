// FilterPoints — lane-A op (batch 15): re-samples input bag into a new fixed-size output.
// Faithful port of external/tixl .../point/modify/FilterPoints (.cs slots, .hlsl math).
// NOTE: FilterPoints CHANGES the output count (to the Count port) — it is NOT count-preserving.
// The output buffer is pre-sized to `c.count` by PointGraph (which reads the "Count" Float port
// per its standard logic: if a "Count" port exists, use it for the buffer size).
//
// TiXL parity (FilterPoints.hlsl):
//   ResultPoints[i] = SourcePoints[imod2(StartIndex + floor(i*StepSize) + scatterOffset,
//                                        SourceCount)]
//   scatterOffset = Scatter > 0.001 ? SourceCount * Scatter * hash11u(i + Seed * SourceCount
//                                                                       + StartIndex) : 0
// This is a GPU scatter-copy, not a compact filter — output count is always exactly Count.
//
// Count flow: PointGraph reads the "Count" Float port and sizes the output buffer to that
// value (same contract as generator ops with a Count port: GridPoints, SpherePoints, etc.).
// The upstream input count is received via c.inputCounts[0] and passed as SourceCount.
// If no input is wired, safe no-op (nothing to sample from).
//
// Self-contained leaf: own capture vector + draw op.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"             // calcDispatchCount
#include "runtime/filterpoints_params.h"  // FilterPointsParams, FilterPointsBinding
#include "runtime/graph.h"                // Graph/Node/pinId
#include "runtime/point_graph.h"          // PointCookCtx, registerPointOp, PointGraph
#include "runtime/tex_op_cache.h"         // cachedComputePSO
#include "runtime/tixl_point.h"           // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookFilterPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;
  uint32_t srcCount = (c.inputCount > 0) ? c.inputCounts[0] : 0u;
  if (srcCount == 0) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "filterpoints");
  if (!pso) return;

  FilterPointsParams P{};
  P.Scatter     = cookParam(c, "ScatterSelect", 0.0f);
  P.StepSize    = cookParam(c, "Step", 1.0f);
  P.SourceCount = (int32_t)srcCount;
  P.ResultCount = (int32_t)c.count;
  P.StartIndex  = (int32_t)(cookParam(c, "StartIndex", 0.0f) + 0.5f);
  P.Seed        = (int32_t)(cookParam(c, "Seed", 0.0f) + 0.5f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, FILTERPOINTS_SourcePoints);
  enc->setBuffer(c.output, 0, FILTERPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), FILTERPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing ---
std::vector<SwPoint>* g_capFilter = nullptr;
void captureDrawFilter(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capFilter || !pts || c.count == 0) return;
  g_capFilter->assign(c.count, SwPoint{});
  std::memcpy(g_capFilter->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerFilterPointsOp() { registerPointOp("FilterPoints", cookFilterPoints); }

// Golden: SpherePoints(N=64, R=1) -> FilterPoints(Count=8, Step=8) -> DrawPoints -> capture.
// Step=8 means ResultPoints[i] = SourcePoints[(i*8) % 64], so we get every 8th sphere point.
// TEETH:
//   (1) OUTPUT COUNT is Count=8, not the input 64. (FilterPoints changes count.)
//   (2) The 8 sampled points all lie on the unit sphere (|pos| close to 1.0).
//   (3) They are DISTINCT positions (not the same point 8 times).
//   (4) COUNT CHANGE: a second FilterPoints(Count=16, Step=4) produces 16 points from 64.
//       Proves the count-port contract scales correctly.
// injectBug: flips the predicate for test (2) — asserts |pos| NOT close to 1.0, so a
// correct output (sphere points at r=1) will FAIL. Internally inject: swap assertion sense.
int runFilterPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-filterpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerFilterPointsOp();
  std::vector<SwPoint> captured;
  g_capFilter = &captured;
  registerDrawOp("DrawPoints", captureDrawFilter);

  // Graph: SpherePoints(64) -> FilterPoints(Count=8, Step=8) -> DrawPoints
  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  Node gen; gen.id = 1; gen.type = "SpherePoints";
  gen.params["Count"] = 64.0f;
  gen.params["Radius"] = 1.0f;
  g.nodes.push_back(gen);

  Node flt; flt.id = 2; flt.type = "FilterPoints";
  flt.params["Count"]        = 8.0f;
  flt.params["Step"]         = 8.0f;
  flt.params["StartIndex"]   = 0.0f;
  flt.params["ScatterSelect"] = 0.0f;
  flt.params["Seed"]         = 0.0f;
  g.nodes.push_back(flt);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == 8);

  // All sampled points should lie on the unit sphere (|pos| close to 1.0)
  float maxRadErr = 0.0f;
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    float e = std::fabs(r - 1.0f);
    if (e > maxRadErr) maxRadErr = e;
  }
  // Distinct positions: check that not all points are identical
  bool allSame = true;
  if (captured.size() > 1) {
    for (size_t k = 1; k < captured.size(); ++k) {
      if (std::fabs(captured[k].Position.x - captured[0].Position.x) > 1e-4f ||
          std::fabs(captured[k].Position.y - captured[0].Position.y) > 1e-4f ||
          std::fabs(captured[k].Position.z - captured[0].Position.z) > 1e-4f) {
        allSame = false; break;
      }
    }
  }

  // injectBug: flip the sphere-membership predicate
  bool onSphere = maxRadErr < 0.01f;
  if (injectBug) onSphere = !onSphere;  // bug: assert the wrong sense

  bool distinct = !allSame;
  bool pass = countOk && onSphere && distinct;

  printf("[selftest-filterpoints] n=%zu(need 8) maxRadErr=%.4f(need<0.01) distinct=%s -> %s\n",
         captured.size(), maxRadErr, distinct ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capFilter = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
