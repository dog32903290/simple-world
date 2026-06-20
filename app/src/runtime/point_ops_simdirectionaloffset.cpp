// SimDirectionalOffset — sim-family MODIFIER (batch sw-node-batch): directional position push
// (Mode 0) or velocity-in-rotation encode (Mode 1). Faithful port of external/tixl
// .../point/sim/SimDirectionalOffset (.cs slots, .hlsl math). Reads c.inputs[0], writes
// count-preserving c.output. Count INHERITED.
//
// TiXL parity (SimDirectionalOffset.cs/.hlsl):
//   - Defaults: Direction=(0,0.01,0), Amount=1.0, RandomAmount=0.0, Mode=0 (Legacy/position)
//   - offset = Direction * Amount * (1 + hash11(i.x) * RandomAmount)
//   - Mode 0: Position += offset ; Mode 1: encode offset into velocity (Rotation*(v+1))
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/simdirectionaloffset_params.h"  // params + bindings
#include "runtime/dispatch.h"                      // calcDispatchCount
#include "runtime/graph.h"                         // Graph/Node/pinId
#include "runtime/point_graph.h"                   // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"                    // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSimDirectionalOffset(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("simdirectionaloffset", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SimDirectionalOffsetParams P{};
  P.Count        = c.count;
  P.Amount       = cookParam(c, "Amount", 1.0f);
  P.RandomAmount = cookParam(c, "RandomAmount", 0.0f);
  P.Mode         = cookParam(c, "Mode", 0.0f);

  float dir[3] = {0.0f, 0.01f, 0.0f};
  cookVecN(c, "Direction", dir, 3, dir);
  P.DirectionX = dir[0]; P.DirectionY = dir[1]; P.DirectionZ = dir[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SIMDIRECTIONALOFFSET_SourcePoints);
  enc->setBuffer(c.output, 0, SIMDIRECTIONALOFFSET_ResultPoints);
  enc->setBytes(&P, sizeof(P), SIMDIRECTIONALOFFSET_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capSimDir = nullptr;
void captureDrawSimDir(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSimDir || !pts || c.count == 0) return;
  g_capSimDir->assign(c.count, SwPoint{});
  std::memcpy(g_capSimDir->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSimDirectionalOffsetOp() {
  registerPointOp("SimDirectionalOffset", cookSimDirectionalOffset);
}

// Golden: RadialPoints(Count=128, Radius=1) [XY ring, Z=0] -> SimDirectionalOffset(
//   Direction=(0,0,1), Amount=0.5, RandomAmount=0, Mode=0) -> capture.
// TEETH:
//   (1) count PRESERVED == 128.
//   (2) every point's Z increased by ~0.5: mean delta_z in [0.4, 0.6].
//   (3) X, Y barely change: max |delta_x| < 1e-3 and max |delta_y| < 1e-3.
// injectBug: Amount=0 -> offset=0 -> no movement -> Tooth2 FAIL.
int runSimDirectionalOffsetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128;
  const float R = 1.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-simdirectionaloffset] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSimDirectionalOffsetOp();
  std::vector<SwPoint> captured;
  g_capSimDir = &captured;
  registerDrawOp("DrawPoints", captureDrawSimDir);

  // Clean ring (Z=0 baseline) for delta measurement.
  std::vector<SwPoint> clean;
  {
    Graph g;
    Node gen; gen.id = 1; gen.type = "RadialPoints";
    gen.params["Count"] = (float)N; gen.params["Radius"] = R;
    g.nodes.push_back(gen);
    Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    PointGraph pg0(dev, lib, q, 64, 64);
    EvaluationContext c0{}; c0.deltaTime = 1.0f / 60.0f;
    pg0.cook(g, c0, nullptr, pg0.defaultDrawTarget(g));
    clean = captured;
    captured.clear();
  }

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = R;
  g.nodes.push_back(gen);

  Node mod; mod.id = 2; mod.type = "SimDirectionalOffset";
  mod.params["Direction.x"] = 0.0f; mod.params["Direction.y"] = 0.0f; mod.params["Direction.z"] = 1.0f;
  mod.params["Amount"]       = injectBug ? 0.0f : 0.5f;
  mod.params["RandomAmount"] = 0.0f;
  mod.params["Mode"]         = 0.0f;  // Legacy/position
  g.nodes.push_back(mod);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{}; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  std::vector<SwPoint> out = captured;

  bool countOk = (out.size() == N);

  double sumDz = 0.0;
  float maxDx = 0.0f, maxDy = 0.0f;
  size_t cmpN = (out.size() < clean.size()) ? out.size() : clean.size();
  for (size_t k = 0; k < cmpN; ++k) {
    float dz = out[k].Position.z - clean[k].Position.z;
    float dx = std::fabs(out[k].Position.x - clean[k].Position.x);
    float dy = std::fabs(out[k].Position.y - clean[k].Position.y);
    sumDz += dz;
    if (dx > maxDx) maxDx = dx;
    if (dy > maxDy) maxDy = dy;
  }
  float meanDz = cmpN ? (float)(sumDz / (double)cmpN) : 0.0f;

  bool pushedZ = (meanDz > 0.4f && meanDz < 0.6f);
  bool xyStable = (maxDx < 1e-3f && maxDy < 1e-3f);
  bool pass = countOk && pushedZ && xyStable;

  printf("[selftest-simdirectionaloffset] n=%zu meanDz=%.4f(need 0.4..0.6) maxDx=%.5f maxDy=%.5f"
         "(need<1e-3) -> %s\n",
         out.size(), meanDz, maxDx, maxDy, pass ? "PASS" : "FAIL");

  g_capSimDir = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
