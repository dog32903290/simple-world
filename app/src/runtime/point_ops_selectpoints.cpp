// SelectPoints — lane point_modify MODIFIER op: volume selection written into FX1/FX2.
// Faithful port of external/tixl .../point/modify/SelectPoints (.cs ports, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output): each live point
// gets a volume-selection scalar (Sphere/Box/Plane/Zebra/Noise) shaped by FallOff + GainAndBias,
// combined with its existing FX1/FX2 weight by SelectMode, written into FX1 or FX2 (WriteTo).
// Position is UNTOUCHED.  Count INHERITED from upstream.
//
// TiXL parity (SelectPoints.cs / .hlsl): see selectpoints_params.h + selectpoints.metal.
//   - FORK: TransformVolume composed in-shader (world->volume map for a pure TRS volume).
//   - FORK: Visibility/SetW [Input]s dead in the .hlsl -> dropped.
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"             // calcDispatchCount
#include "runtime/graph.h"               // Graph/Node/pinId
#include "runtime/point_graph.h"         // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/selectpoints_params.h" // SelectPointsParams, SelectPointsBinding
#include "runtime/tex_op_cache.h"        // cachedComputePSO
#include "runtime/tixl_point.h"          // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSelectPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "selectpoints");
  if (!pso) return;

  SelectPointsParams P{};
  P.Count              = c.count;
  P.VolumeShape        = (int)(cookParam(c, "VolumeShape", 0.0f) + 0.5f);
  P.SelectMode         = (int)(cookParam(c, "Mode", 0.0f) + 0.5f);
  P.WriteTo            = (int)(cookParam(c, "WriteTo", 1.0f) + 0.5f);
  P.StrengthFactor     = (int)(cookParam(c, "StrengthFactor", 0.0f) + 0.5f);
  P.ClampResult        = (int)(cookParam(c, "ClampResult", 0.0f) + 0.5f);
  P.DiscardNonSelected = (int)(cookParam(c, "DiscardNonSelected", 0.0f) + 0.5f);
  float vc[3] = {0, 0, 0}, vs[3] = {1, 1, 1}, vr[3] = {0, 0, 0};
  cookVecN(c, "VolumeCenter",  vc, 3, vc);
  cookVecN(c, "VolumeStretch", vs, 3, vs);
  cookVecN(c, "VolumeRotate",  vr, 3, vr);
  P.VolumeCenterX  = vc[0]; P.VolumeCenterY  = vc[1]; P.VolumeCenterZ  = vc[2];
  P.VolumeStretchX = vs[0]; P.VolumeStretchY = vs[1]; P.VolumeStretchZ = vs[2];
  P.VolumeRotateX  = vr[0]; P.VolumeRotateY  = vr[1]; P.VolumeRotateZ  = vr[2];
  P.VolumeScale    = cookParam(c, "VolumeScale", 1.0f);
  P.FallOff        = cookParam(c, "FallOff", 0.0f);
  P.Strength       = cookParam(c, "Strength", 1.0f);
  float gb[2] = {0.5f, 0.5f};
  cookVecN(c, "GainAndBias", gb, 2, gb);
  P.GainAndBiasX = gb[0]; P.GainAndBiasY = gb[1];
  P.Phase     = cookParam(c, "Phase", 0.0f);
  P.Threshold = cookParam(c, "Threshold", 0.0f);
  P.Scatter   = cookParam(c, "Scatter", 0.0f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SELECTPOINTS_SourcePoints);
  enc->setBuffer(c.output, 0, SELECTPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), SELECTPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing ---
std::vector<SwPoint>* g_capSelect = nullptr;
void captureDrawSelect(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSelect || !pts || c.count == 0) return;
  g_capSelect->assign(c.count, SwPoint{});
  std::memcpy(g_capSelect->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSelectPointsOp() { registerPointOp("SelectPoints", cookSelectPoints); }

// =============================================================================
// Golden: RadialPoints(N=64, Radius=2 ring at origin) -> SelectPoints -> capture.
//   Run A (INSIDE): Sphere volume, VolumeScale=4 so a unit-radius-1 sphere becomes radius 4 ->
//     every ring point (radius 2) is INSIDE -> s=1 -> Override*Strength=1 -> FX1=1 for all.
//   Run B (OUTSIDE): Sphere volume, VolumeScale=0.5 -> radius-0.5 sphere; ring at radius 2 is
//     OUTSIDE (distance 4 > 1+FallOff) -> s=0 -> FX1=0 for all.
//   Run C (BOX): Box volume, VolumeScale=4 -> box half-extent 4; ring inside -> FX1=1.
//   Run D (POSITION UNTOUCHED): A point's Position is identical before/after (selection only
//     writes FX1/FX2).
// TEETH:
//   (1) COUNT PRESERVED.
//   (2) INSIDE writes FX1≈1 (mean FX1 > 0.95).
//   (3) OUTSIDE writes FX1≈0 (mean FX1 < 0.05).
//   (4) Box shape selects too (mean FX1 > 0.95).
//   (5) Position untouched (run A radius unchanged ≈ 2).
// injectBug: assert that the INSIDE run produces FX1≈0 (selection failed).  The correct shader
//   selects (FX1≈1), so the inverted assertion fails -> RED.  This exercises the volume-distance
//   -> LinearStep -> Override path: if the inside-volume scalar were wrong, FX1 wouldn't be ~1.
// =============================================================================
int runSelectPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float R = 2.0f;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-selectpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSelectPointsOp();

  // Helper: run RadialPoints -> SelectPoints(params) and capture.
  auto runOne = [&](int shape, float volScale) -> std::vector<SwPoint> {
    std::vector<SwPoint> captured;
    g_capSelect = &captured;
    registerDrawOp("DrawPoints", captureDrawSelect);

    Graph g;
    Node gen; gen.id = 1; gen.type = "RadialPoints";
    gen.params["Count"]  = (float)N;
    gen.params["Radius"] = R;
    gen.params["Cycles"] = 1.0f;
    g.nodes.push_back(gen);

    Node sel; sel.id = 2; sel.type = "SelectPoints";
    sel.params["VolumeShape"] = (float)shape;   // 0=Sphere, 1=Box
    sel.params["Mode"]        = 0.0f;           // Override
    sel.params["WriteTo"]     = 1.0f;           // F1
    sel.params["Strength"]    = 1.0f;
    sel.params["VolumeScale"] = volScale;
    sel.params["FallOff"]     = 0.0f;
    g.nodes.push_back(sel);

    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    PointGraph pg(dev, lib, q, 64, 64);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
    g_capSelect = nullptr;
    return captured;
  };

  auto meanFx1 = [](const std::vector<SwPoint>& v) -> float {
    if (v.empty()) return -1.0f;
    float m = 0.0f; for (const SwPoint& p : v) m += p.FX1; return m / (float)v.size();
  };
  auto meanRadius = [](const std::vector<SwPoint>& v) -> float {
    if (v.empty()) return -1.0f;
    float m = 0.0f;
    for (const SwPoint& p : v)
      m += std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                     p.Position.z * p.Position.z);
    return m / (float)v.size();
  };

  // Run A: Sphere INSIDE (volScale=4 -> ring at R=2 inside unit sphere*4).
  auto a = runOne(0, 4.0f);
  // Run B: Sphere OUTSIDE (volScale=0.5 -> ring outside).
  auto b = runOne(0, 0.5f);
  // Run C: Box INSIDE.
  auto c = runOne(1, 4.0f);

  float fa = meanFx1(a), fb = meanFx1(b), fc = meanFx1(c);
  float ra = meanRadius(a);
  bool countOk  = (a.size() == N && b.size() == N && c.size() == N);
  bool insideOk = injectBug ? (fa < 0.05f) : (fa > 0.95f);  // bug flips the inside assertion
  bool outsideOk = (fb < 0.05f);
  bool boxOk     = (fc > 0.95f);
  bool posOk     = (std::fabs(ra - R) < 0.05f);  // Position untouched: radius still ~2

  bool pass = countOk && insideOk && outsideOk && boxOk && posOk;
  printf("[selftest-selectpoints] n=%zu inFX1=%.3f(need>0.95) outFX1=%.3f(need<0.05) "
         "boxFX1=%.3f(need>0.95) radius=%.3f(need~%.1f) -> %s%s\n",
         a.size(), fa, fb, fc, ra, R, pass ? "PASS" : "FAIL",
         injectBug ? " (bug-mode: expect FAIL)" : "");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
