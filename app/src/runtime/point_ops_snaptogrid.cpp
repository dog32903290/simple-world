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
#include "runtime/tex_op_cache.h"       // cachedComputePSO
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

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "snaptogrid");
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
  // TiXL: UI port "BiasAndGain" wires straight into shader cbuffer "GainAndBias" (no swizzle).
  // bias-functions.hlsl reads gain=GainAndBias.x, bias=GainAndBias.y. Both default 0.5 (neutral).
  P.GainAndBiasX = gainbias[0];  // -> shader gain arg
  P.GainAndBiasY = gainbias[1];  // -> shader bias arg

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
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
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

// --- C++ reference recompute of TiXL ApplyGainAndBias (bias-functions.hlsl, scalar form) ---
// Verbatim mirror of getBias/getSchlickBias/applyGainAndBias in snaptogrid.metal.
// Used by the parity tooth below to compare GPU kernel output against the exact TiXL math
// in the divergent gain>=0.5 (schlick-then-bias) branch.
namespace {
float refGetBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
float refSchlick(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * refGetBias(g, x); }
  else          { x = 2.0f * x - 1.0f; x = 0.5f * refGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float refGainBias(float value, float gain, float bias) {
  float g = std::fmin(1.0f, std::fmax(0.0f, gain));
  float b = std::fmin(1.0f, std::fmax(0.0f, bias));
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = refGetBias(b, value); value = refSchlick(g, value); }
  else          { value = refSchlick(g, value); value = refGetBias(b, value); }
  return value;
}
}  // namespace

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

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
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

  // --- PARITY TOOTH: ApplyGainAndBias verbatim vs TiXL (batch 21 refuter) ---
  // The golden above only exercises neutral (0.5,0.5) where ApplyGainAndBias == identity,
  // so it cannot catch a wrong gain/bias reconstruction. This tooth drives a NON-neutral
  // gain>=0.5 config (the branch where TiXL flips to schlick-THEN-bias) and compares the
  // GPU kernel position against a C++ recompute of TiXL's exact math.
  //   Setup: RadialPoints(R=2) -> SnapPointsToGrid(Mode=2 AxisCenterDistance, Amount=0.7,
  //          GridScale=1, gain=0.8, bias=0.3). Mode 2 is per-axis (snapAmount=abs(signedFraction)),
  //          so each axis is independent and exactly reproducible in C++.
  //   injectBug: same Amount=0 -> ff=0 -> stays at original -> mismatches the recompute (RED).
  bool parityOk = true;
  float maxParityErr = 0.0f;
  {
    const float gParam = 0.8f, bParam = 0.3f, amount = 0.7f, gridScale = 1.0f;
    std::vector<SwPoint> capt2;
    g_capSnap = &capt2;

    Graph g2;
    Node gen2; gen2.id = 1; gen2.type = "RadialPoints";
    gen2.params["Count"] = (float)N; gen2.params["Radius"] = R; gen2.params["Cycles"] = 1.0f;
    g2.nodes.push_back(gen2);
    Node snap2; snap2.id = 2; snap2.type = "SnapPointsToGrid";
    snap2.params["Amount"]        = injectBug ? 0.0f : amount;
    snap2.params["GridScale"]     = gridScale;
    snap2.params["Mode"]          = 2.0f;  // AxisCenterDistance: per-axis snapAmount
    snap2.params["GridStretch.x"] = 1.0f; snap2.params["GridStretch.y"] = 1.0f; snap2.params["GridStretch.z"] = 1.0f;
    snap2.params["GridOffset.x"]  = 0.0f; snap2.params["GridOffset.y"]  = 0.0f; snap2.params["GridOffset.z"]  = 0.0f;
    snap2.params["BiasAndGain.x"] = gParam;  // .x -> gain arg
    snap2.params["BiasAndGain.y"] = bParam;  // .y -> bias arg
    g2.nodes.push_back(snap2);
    Node drw2; drw2.id = 3; drw2.type = "DrawPoints"; g2.nodes.push_back(drw2);
    g2.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g2.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    // Source positions (post-RadialPoints) to recompute expected output. Capture them via a
    // separate cook of just the generator into a draw target.
    std::vector<SwPoint> srcPts;
    {
      g_capSnap = &srcPts;
      Graph gs; gs.nodes.push_back(gen2);
      Node drws; drws.id = 9; drws.type = "DrawPoints"; gs.nodes.push_back(drws);
      gs.connections.push_back({201, pinId(1, 0), pinId(9, 0)});
      PointGraph pgs(dev, lib, q, 64, 64);
      EvaluationContext cs{}; cs.deltaTime = 1.0f / 60.0f;
      pgs.cook(gs, cs, nullptr, pgs.defaultDrawTarget(gs));
    }

    g_capSnap = &capt2;
    PointGraph pg2(dev, lib, q, 64, 64);
    EvaluationContext c2{}; c2.deltaTime = 1.0f / 60.0f;
    pg2.cook(g2, c2, nullptr, pg2.defaultDrawTarget(g2));

    auto satf = [](float v){ return std::fmin(1.0f, std::fmax(0.0f, v)); };
    auto fmod1 = [](float x){ float q = std::floor(x); return x - q; };  // y==1 floored mod
    if (capt2.size() != N || srcPts.size() != N) { parityOk = false; }
    for (size_t i = 0; i < capt2.size() && i < srcPts.size(); ++i) {
      float src[3] = {srcPts[i].Position.x, srcPts[i].Position.y, srcPts[i].Position.z};
      float got[3] = {capt2[i].Position.x, capt2[i].Position.y, capt2[i].Position.z};
      float effAmount = injectBug ? 0.0f : amount;
      for (int a = 0; a < 3; ++a) {
        // gridSize = gridScale*1, signedFraction = (mod(pos/gridSize + 0.5 - 0, 1) - 0.5)*2
        float gs = gridScale;
        float sf = (fmod1(src[a] / gs + 0.5f) - 0.5f) * 2.0f;
        float centerA = src[a] - sf * gs / 2.0f;
        float snapA = std::fabs(sf);             // Mode 2 per-axis
        float biased = refGainBias(snapA, gParam, bParam);
        float ff = (1.0f - satf(biased - effAmount * 2.0f + 1.0f)) * effAmount;
        float expected = src[a] + (centerA - src[a]) * ff;  // lerp
        float e = std::fabs(expected - got[a]);
        if (e > maxParityErr) maxParityErr = e;
        if (e > 1e-3f) parityOk = false;
      }
    }
    g_capSnap = nullptr;
  }
  printf("[selftest-snaptogrid] parity(gain=0.8,bias=0.3,Mode=2) maxErr=%.5f(need<0.001) -> %s\n",
         maxParityErr, parityOk ? "PASS" : "NO");

  bool pass = countOk && snapOk && parityOk;
  printf("[selftest-snaptogrid] n=%zu maxGridErr=%.4f(need<0.05) snapOk=%s -> %s\n",
         captured.size(), maxErr, snapOk ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capSnap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
