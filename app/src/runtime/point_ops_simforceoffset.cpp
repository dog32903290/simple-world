// SimForceOffset — sim-family MODIFIER (batch sw-node-batch): radial force + gravity inside a
// radius/falloff window. Faithful port of external/tixl .../point/sim/SimForceOffset (.cs slots,
// .hlsl math). Reads c.inputs[0], writes count-preserving c.output. Count INHERITED.
//
// TiXL parity (SimForceOffset.cs/.hlsl):
//   - Defaults: Center=(0,0,0), Radius=999, RadiusFallOff=0, RadialForce=0, UseWForMass=0,
//               Variation=0, Gravity=(0,0,0), ForceDecayRate=1.0
//   - effect = saturate(1-(distance-Radius)/RadiusFallOff)/100
//   - radialForce = direction / clamp(pow(distance,ForceDecayRate),0.02,1000) * RadialForce
//   - Position += (Gravity + radialForce) * effect
//   With defaults (RadiusFallOff=0) effect=0 -> no force; an effect needs RadiusFallOff>0.
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"
#include "runtime/tex_op_cache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/simforceoffset_params.h"  // params + bindings
#include "runtime/dispatch.h"               // calcDispatchCount
#include "runtime/graph.h"                  // Graph/Node/pinId
#include "runtime/point_graph.h"            // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"             // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSimForceOffset(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "simforceoffset");
  if (!pso) return;

  SimForceOffsetParams P{};
  P.Count          = c.count;
  P.Radius         = cookParam(c, "Radius", 999.0f);
  P.RadiusFallOff  = cookParam(c, "RadiusFallOff", 0.0f);
  P.RadialForce    = cookParam(c, "RadialForce", 0.0f);
  P.UseWForMass    = cookParam(c, "UseWForMass", 0.0f);
  P.Variation      = cookParam(c, "Variation", 0.0f);
  P.ForceDecayRate = cookParam(c, "ForceDecayRate", 1.0f);

  float center[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  float grav[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Gravity", grav, 3, grav);
  P.GravityX = grav[0]; P.GravityY = grav[1]; P.GravityZ = grav[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SIMFORCEOFFSET_SourcePoints);
  enc->setBuffer(c.output, 0, SIMFORCEOFFSET_ResultPoints);
  enc->setBytes(&P, sizeof(P), SIMFORCEOFFSET_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capSimForce = nullptr;
void captureDrawSimForce(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSimForce || !pts || c.count == 0) return;
  g_capSimForce->assign(c.count, SwPoint{});
  std::memcpy(g_capSimForce->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSimForceOffsetOp() { registerPointOp("SimForceOffset", cookSimForceOffset); }

// Golden: RadialPoints(Count=128, Radius=1) [|r|=1] -> SimForceOffset(Center=0, Radius=1.5,
//   RadiusFallOff=2.0, RadialForce=5.0, Gravity=0, Variation=0) -> capture.
// Hand math: distance=1 ; effect = saturate(1-(1-1.5)/2.0)/100 = saturate(1.25)/100 = 0.01 ;
//   radialForce = direction/clamp(1,..)*5 = direction*5 ; offset = direction*5*0.01 = direction*0.05
//   => points pushed OUT to |r| ~ 1.05.
// TEETH:
//   (1) count PRESERVED == 128.
//   (2) radial push out: mean |r| > 1.02 (force actually applied).
// injectBug: RadialForce=0 (and Gravity=0) -> (Gravity+radialForce)=0 -> no movement -> Tooth2 FAIL.
int runSimForceOffsetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128;
  const float R = 1.0f;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-simforceoffset] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSimForceOffsetOp();
  std::vector<SwPoint> captured;
  g_capSimForce = &captured;
  registerDrawOp("DrawPoints", captureDrawSimForce);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = R;
  g.nodes.push_back(gen);

  Node mod; mod.id = 2; mod.type = "SimForceOffset";
  mod.params["Center.x"] = 0.0f; mod.params["Center.y"] = 0.0f; mod.params["Center.z"] = 0.0f;
  mod.params["Radius"]        = 1.5f;
  mod.params["RadiusFallOff"] = 2.0f;
  mod.params["RadialForce"]   = injectBug ? 0.0f : 5.0f;
  mod.params["Variation"]     = 0.0f;
  mod.params["Gravity.x"] = 0.0f; mod.params["Gravity.y"] = 0.0f; mod.params["Gravity.z"] = 0.0f;
  mod.params["ForceDecayRate"] = 1.0f;
  g.nodes.push_back(mod);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{}; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  std::vector<SwPoint> out = captured;

  bool countOk = (out.size() == N);

  double sumR = 0.0;
  for (const SwPoint& p : out) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    sumR += r;
  }
  float meanR = out.empty() ? 0.0f : (float)(sumR / (double)out.size());

  bool pushedOut = meanR > 1.02f;
  bool pass = countOk && pushedOut;

  printf("[selftest-simforceoffset] n=%zu meanR=%.4f(need>1.02) -> %s\n",
         out.size(), meanR, pass ? "PASS" : "FAIL");

  g_capSimForce = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
