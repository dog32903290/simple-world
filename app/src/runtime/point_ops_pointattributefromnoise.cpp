// PointAttributeFromNoise — batch 24 MODIFIER op (point_modify family): per-point 3D simplex
// noise routed into chosen attributes (position xyzw / rotation xyz) through 4 channels (L/R/G/B).
// Faithful port of TiXL's PointAttributeFromNoise.
//
// TiXL authority:
//   .cs : external/tixl/Operators/Lib/point/modify/PointAttributeFromNoise.cs
//   .hlsl: external/tixl/Operators/Lib/Assets/shaders/points/modify/PointAttributesFromNoise.hlsl
//
// FORK (named): RemapNoise(Gradient)/UseRemapCurve/remapCurveTexture NOT wired (work order) ->
//   UseRemapCurve baked false -> always the else branch `c *= Amount/100`. See params.h / .metal.
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                       // calcDispatchCount
#include "runtime/graph.h"                          // Graph/Node/pinId
#include "runtime/point_graph.h"                    // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/pointattributefromnoise_params.h" // params + binding
#include "runtime/tixl_point.h"                     // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookPointAttributeFromNoise(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("pointattributefromnoise", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  PointAttributeFromNoiseParams P{};
  P.Count = c.count;
  // attribute selectors (.cs int enum, MappedType=Attributes) + per-channel Factor/Offset.
  P.L = cookParam(c, "Brightness", 0.0f); P.LFactor = cookParam(c, "BrightnessFactor", 1.0f);
  P.LOffset = cookParam(c, "BrightnessOffset", 0.0f);
  P.R = cookParam(c, "Red", 0.0f);   P.RFactor = cookParam(c, "RedFactor", 1.0f);
  P.ROffset = cookParam(c, "RedOffset", 0.0f);
  P.G = cookParam(c, "Green", 0.0f); P.GFactor = cookParam(c, "GreenFactor", 1.0f);
  P.GOffset = cookParam(c, "GreenOffset", 0.0f);
  P.B = cookParam(c, "Blue", 0.0f);  P.BFactor = cookParam(c, "BlueFactor", 1.0f);
  P.BOffset = cookParam(c, "BlueOffset", 0.0f);

  float center[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  P.Phase     = cookParam(c, "Phase", 0.0f);
  P.Frequency = cookParam(c, "Frequency", 1.0f);
  P.Amount    = cookParam(c, "Amount", 1.0f);
  P.Variation = cookParam(c, "Variation", 0.0f);

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, POINTATTRNOISE_SourcePoints);
  enc->setBuffer(c.output,                         0, POINTATTRNOISE_ResultPoints);
  enc->setBytes(&P, sizeof(P),                        POINTATTRNOISE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capPAN = nullptr;
void captureDrawPAN(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capPAN || !pts || c.count == 0) return;
  g_capPAN->assign(c.count, SwPoint{});
  std::memcpy(g_capPAN->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerPointAttributeFromNoiseOp() {
  registerPointOp("PointAttributeFromNoise", cookPointAttributeFromNoise);
}

// Golden: SpherePoints(R=1, N=512) -> PointAttributeFromNoise(route noise into position X/Y/Z via
// Red->For_X, Green->For_Y, Blue->For_Z; Amount=100 so c*=Amount/100=c, Frequency=2, Phase=0.5) ->
// capture.
// TEETH:
//   (1) count PRESERVED (modifier never changes point count).
//   (2) NOISE ACTUALLY MOVED POINTS: max |pos - cleanSpherePos| > 0.01 (the noise field displaced
//       positions off the clean sphere).
//   (3) PER-POINT VARIED: displacement spread > 0.05 (not a uniform shift — each point sampled a
//       different noise value).
//   (4) DETERMINISM: same params, two cooks -> identical output within 1e-5 (simplex noise +
//       hash31 are deterministic).
// injectBug: Amount=0 -> c *= 0/100 = 0 -> ff = 0 -> positions unchanged from the clean sphere ->
//   "noise actually moved points" (tooth 2) FAILS. Real degenerate (the noise routing collapses),
//   not a flipped assertion. (Mirrors AddNoise's Strength=0 bug shape; it hits the SAME承重線 the
//   work-order "改 frequency/漏 phase" hint targets — that the noise term genuinely drives output.)
int runPointAttributeFromNoiseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 512;
  const float R = 1.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pointattributefromnoise] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerPointAttributeFromNoiseOp();
  std::vector<SwPoint> captured;
  g_capPAN = &captured;
  registerDrawOp("DrawPoints", captureDrawPAN);

  auto makeGraph = [&]() {
    Graph g;
    Node gen; gen.id = 1; gen.type = "SpherePoints";
    gen.params["Count"]  = (float)N;
    gen.params["Radius"] = R;
    g.nodes.push_back(gen);

    Node pan; pan.id = 2; pan.type = "PointAttributeFromNoise";
    // route each noise channel into a position axis: For_X=1, For_Y=2, For_Z=3.
    pan.params["Red"]   = 1.0f;  // For_X
    pan.params["Green"] = 2.0f;  // For_Y
    pan.params["Blue"]  = 3.0f;  // For_Z
    pan.params["Brightness"] = 0.0f;  // NotUsed
    pan.params["RedFactor"]   = 1.0f; pan.params["GreenFactor"] = 1.0f; pan.params["BlueFactor"] = 1.0f;
    pan.params["RedOffset"]   = 0.0f; pan.params["GreenOffset"] = 0.0f; pan.params["BlueOffset"] = 0.0f;
    pan.params["Frequency"] = 2.0f;
    pan.params["Phase"]     = 0.5f;
    pan.params["Variation"] = 0.0f;   // deterministic (no per-point random offset)
    pan.params["Amount"]    = injectBug ? 0.0f : 100.0f;  // 100/100 = unit scale of the noise
    g.nodes.push_back(pan);

    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
    return g;
  };

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // First cook
  Graph g1 = makeGraph();
  pg.cook(g1, ctx, nullptr, pg.defaultDrawTarget(g1));
  std::vector<SwPoint> first = captured;

  // Second cook (determinism)
  captured.clear();
  Graph g2 = makeGraph();
  pg.cook(g2, ctx, nullptr, pg.defaultDrawTarget(g2));
  std::vector<SwPoint> second = captured;

  bool countOk = (captured.size() == N);

  // Displacement off the clean sphere (|pos| - R).
  float maxRadErr = 0.0f, dispMin = 1e9f, dispMax = -1e9f;
  for (const SwPoint& p : first) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    float disp = r - R;
    float e = std::fabs(disp);
    if (e > maxRadErr) maxRadErr = e;
    if (disp < dispMin) dispMin = disp;
    if (disp > dispMax) dispMax = disp;
  }
  float dispSpread = (countOk && !first.empty()) ? (dispMax - dispMin) : 0.0f;

  bool deterministic = (first.size() == second.size());
  for (size_t k = 0; k < first.size() && deterministic; ++k) {
    if (std::fabs(first[k].Position.x - second[k].Position.x) > 1e-5f ||
        std::fabs(first[k].Position.y - second[k].Position.y) > 1e-5f ||
        std::fabs(first[k].Position.z - second[k].Position.z) > 1e-5f) deterministic = false;
  }

  bool noised = maxRadErr > 0.01f;
  bool varied = dispSpread > 0.05f;
  bool pass = countOk && noised && varied && deterministic;

  printf("[selftest-pointattributefromnoise] n=%zu maxRadErr=%.4f(need>0.01) dispSpread=%.4f"
         "(need>0.05) deterministic=%s -> %s\n",
         first.size(), maxRadErr, dispSpread, deterministic ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  g_capPAN = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
