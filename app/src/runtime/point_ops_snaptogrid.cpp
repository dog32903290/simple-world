// SnapPointsToGrid — batch 19 MODIFIER op (transform family): lerp each point toward the
// nearest grid-cell center, blended by Amount and shaped by Mode/GainAndBias.
// Faithful port of TiXL's SnapPointsToGrid.
//
// Reference: external/tixl/Operators/Lib/point/transform/SnapPointsToGrid.cs (slots) +
//            external/tixl/Operators/Lib/Assets/shaders/points/_internal/SnapPointsToGrid.hlsl (math)
//
// TiXL ports (fully ported):
//   Points (BufferWithViews) -> c.inputs[0]
//   Amount (Single, def 1.0)
//   Mode (enum: CenterDistance/CornersDistance/AxisCenterDistance/AxisEdgeDistance)
//   GridScale (Single, def 1.0)
//   GridStretch (Vector3, def 1,1,1)
//   GridOffset (Vector3, def 0,0,0)
//   BiasAndGain (Vector2, def 0.5,0.5 = neutral)
// TiXL ports baked:
//   AmountFactor (None): StrengthFactor baked to None (strength=1.0) -- NAMED FORK
//   Scatter (0.0): hash jitter deferred -- NAMED FORK
//   UseWAsWeight (false) -- NAMED FORK
//   UseSelection (false) -- NAMED FORK
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"           // calcDispatchCount
#include "runtime/graph.h"              // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"        // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"         // SwPoint (64B)
#include "runtime/snaptogrid_params.h"  // SnapToGridParams, SnapToGridBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSnapToGrid(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("snaptogrid", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SnapToGridParams P{};
  P.Count     = c.count;
  P.Amount    = cookParam(c, "Amount",    1.0f);
  P.GridScale = cookParam(c, "GridScale", 1.0f);
  P.Mode      = cookParam(c, "Mode",      0.0f);

  float stretch[3]    = {1.0f, 1.0f, 1.0f};
  float offset[3]     = {0.0f, 0.0f, 0.0f};
  float gainbias[2]   = {0.5f, 0.5f};
  cookVecN(c, "GridStretch", stretch, 3, stretch);
  cookVecN(c, "GridOffset",  offset,  3, offset);
  cookVecN(c, "BiasAndGain", gainbias, 2, gainbias);  // TiXL port name "BiasAndGain"

  P.GridStretchX = stretch[0]; P.GridStretchY = stretch[1]; P.GridStretchZ = stretch[2];
  P.GridOffsetX  = offset[0];  P.GridOffsetY  = offset[1];  P.GridOffsetZ  = offset[2];
  // TiXL BiasAndGain: .x=bias(gain), .y=bias(bias) — shader uses x as gain arg, y as bias arg
  P.GainAndBiasX = gainbias[0];
  P.GainAndBiasY = gainbias[1];

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SNAPTOGRID_SourcePoints);
  enc->setBuffer(c.output,                         0, SNAPTOGRID_ResultPoints);
  enc->setBytes(&P, sizeof(P),                        SNAPTOGRID_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capSnap = nullptr;
void captureDrawSnap(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSnap || !pts || c.count == 0) return;
  g_capSnap->assign(c.count, SwPoint{});
  std::memcpy(g_capSnap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSnapToGridOp() { registerPointOp("SnapPointsToGrid", cookSnapToGrid); }

// Golden: RadialPoints(N, R=2) -> SnapPointsToGrid(Amount=1, GridScale=1, Mode=0 CenterDistance,
//         GridStretch=1,1,1, GridOffset=0, GainAndBias=0.5,0.5) -> DrawPoints capture.
//
// With Amount=1 and Mode=CenterDistance, ff approaches 1 for points far from grid centers
// (i.e., in the middle between grid lines). In full-snap mode (Amount=1, GainAndBias=0.5,0.5),
// all output positions must lie ON grid points (integer multiples of GridScale=1 per axis).
//
// Quantitative tooth: at Amount=1, the snapped positions should be at integer x/y/z.
// We verify: for each axis, |round(pos[a]) - pos[a]| < 0.05 (snapped to nearest integer).
// Non-trivial: RadialPoints at R=2 has points at non-integer positions.
//
// injectBug: Amount=0 -> ff=0 -> lerp stays at original -> radial ring positions are
//   NOT at integer coordinates -> the integer-snap assertion FAILS (RED).
int runSnapToGridSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N  = 64;
  const float    R  = 2.0f;

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-snaptogrid] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSnapToGridOp();
  std::vector<SwPoint> captured;
  g_capSnap = &captured;
  registerDrawOp("DrawPoints", captureDrawSnap);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  gen.params["Cycles"] = 1.0f;
  g.nodes.push_back(gen);

  Node snap; snap.id = 2; snap.type = "SnapPointsToGrid";
  snap.params["Amount"]         = injectBug ? 0.0f : 1.0f;  // bug: no snap -> stays radial
  snap.params["GridScale"]      = 1.0f;
  snap.params["Mode"]           = 0.0f;  // CenterDistance
  snap.params["GridStretch.x"]  = 1.0f;
  snap.params["GridStretch.y"]  = 1.0f;
  snap.params["GridStretch.z"]  = 1.0f;
  snap.params["GridOffset.x"]   = 0.0f;
  snap.params["GridOffset.y"]   = 0.0f;
  snap.params["GridOffset.z"]   = 0.0f;
  snap.params["BiasAndGain.x"]  = 0.5f;
  snap.params["BiasAndGain.y"]  = 0.5f;
  g.nodes.push_back(snap);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk  = (captured.size() == N);
  // At Amount=1, Mode=CenterDistance with GainAndBias=(0.5,0.5), the biased ff drives points
  // to grid centers. We check each axis rounds to integer within tolerance.
  bool snapOk   = countOk && !captured.empty();
  float maxErr  = 0.0f;
  for (const SwPoint& p : captured) {
    float ex = std::fabs(p.Position.x - std::round(p.Position.x));
    float ey = std::fabs(p.Position.y - std::round(p.Position.y));
    float ez = std::fabs(p.Position.z - std::round(p.Position.z));
    float e = std::fmax(ex, std::fmax(ey, ez));
    if (e > maxErr) maxErr = e;
    if (e > 0.05f) snapOk = false;
  }

  bool pass = countOk && snapOk;
  printf("[selftest-snaptogrid] n=%zu maxGridErr=%.4f(need<0.05) snapOk=%s -> %s\n",
         captured.size(), maxErr, snapOk ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capSnap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
