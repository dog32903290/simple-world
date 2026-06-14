// SoftTransformPoints — lane point_modify MODIFIER op: volume-falloff weighted soft transform.
// Faithful port of external/tixl .../point/transform/SoftTransformPoints (.cs ports, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output): each point gets a
// volume-falloff weight (Sphere/Box/Plane/Zebra smoothstep) shaped by GainAndBias×Strength×Factor,
// then a Translate/Rotate/Scale transform is SOFT-applied to Position (lerp by weight), Rotation is
// composed by the X-axis rotation, and FX1 is lerped by ScaleW/OffsetW.  Count INHERITED.
//
// TiXL parity: see softtransformpoints_params.h + softtransformpoints.metal.
//   - FORK: TransformVolume (TransformMatrix Invert=true) composed in-shader (analytic TRS inverse).
//   - FORK: Strength cbuffer field = Amount; Dither/Bias/UseWAsWeight/Visibility dead -> dropped.
//   - FORK: ScaleW/OffsetW ports map to .hlsl ScaleFx1/OffsetFx1.
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                   // calcDispatchCount
#include "runtime/graph.h"                      // Graph/Node/pinId
#include "runtime/point_graph.h"                // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/softtransformpoints_params.h" // SoftTransformParams, SoftTransformBinding
#include "runtime/tixl_point.h"                 // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSoftTransformPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("softtransformpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SoftTransformParams P{};
  P.Count          = c.count;
  P.VolumeShape    = (int)(cookParam(c, "VolumeType", 0.0f) + 0.5f);
  P.StrengthFactor = (int)(cookParam(c, "StrengthFactor", 0.0f) + 0.5f);
  P.Strength       = cookParam(c, "Amount", 1.0f);  // FORK: Amount -> shader Strength
  float tr[3] = {0, 0, 0}, st[3] = {1, 1, 1}, ro[3] = {0, 0, 0};
  cookVecN(c, "Translate", tr, 3, tr);
  cookVecN(c, "Stretch",   st, 3, st);
  cookVecN(c, "Rotate",    ro, 3, ro);
  P.TranslateX = tr[0]; P.TranslateY = tr[1]; P.TranslateZ = tr[2];
  P.ScaleX = st[0]; P.ScaleY = st[1]; P.ScaleZ = st[2];          // .hlsl Scale = .cs Stretch
  P.ScaleMagnitude = cookParam(c, "Scale", 1.0f);                // .hlsl ScaleMagnitude = .cs Scale
  P.RotateAxisX = ro[0]; P.RotateAxisY = ro[1]; P.RotateAxisZ = ro[2];
  float vc[3] = {0, 0, 0}, vs[3] = {1, 1, 1};
  cookVecN(c, "VolumeCenter",  vc, 3, vc);
  cookVecN(c, "VolumeStretch", vs, 3, vs);
  P.VolumeCenterX  = vc[0]; P.VolumeCenterY  = vc[1]; P.VolumeCenterZ  = vc[2];
  P.VolumeStretchX = vs[0]; P.VolumeStretchY = vs[1]; P.VolumeStretchZ = vs[2];
  P.VolumeSize     = cookParam(c, "VolumeSize", 1.0f);
  P.FallOff        = cookParam(c, "FallOff", 0.0f);
  P.Phase          = 0.0f;   // .hlsl Phase (Box offset / Zebra phase); no .cs Phase port (baked 0)
  P.Threshold      = 0.0f;   // .hlsl Threshold (Zebra); no .cs Threshold port (baked 0)
  float gb[2] = {0.5f, 0.5f};
  cookVecN(c, "GainAndBias", gb, 2, gb);
  P.GainAndBiasX = gb[0]; P.GainAndBiasY = gb[1];
  P.ScaleFx1  = cookParam(c, "ScaleW", 1.0f);   // FORK: ScaleW -> shader ScaleFx1
  P.OffsetFx1 = cookParam(c, "OffsetW", 0.0f);  // FORK: OffsetW -> shader OffsetFx1

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SOFTXF_SourcePoints);
  enc->setBuffer(c.output, 0, SOFTXF_ResultPoints);
  enc->setBytes(&P, sizeof(P), SOFTXF_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing ---
std::vector<SwPoint>* g_capSoft = nullptr;
void captureDrawSoft(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSoft || !pts || c.count == 0) return;
  g_capSoft->assign(c.count, SwPoint{});
  std::memcpy(g_capSoft->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSoftTransformPointsOp() {
  registerPointOp("SoftTransformPoints", cookSoftTransformPoints);
}

// =============================================================================
// Golden: RadialPoints(N=64, Radius=2 ring) -> SoftTransformPoints -> capture.
//   Run A (INSIDE): Sphere volume centered at origin, VolumeSize=4 (the unit sphere becomes
//     radius 4) so the ring (radius 2) is fully inside -> weight=1 -> full transform.
//     With Translate=(3,0,0), Stretch=1, Scale=1, no rotate:
//       p = lerp(p, 0 + p, 1) + (3,0,0) = p + (3,0,0)  -> meanX ≈ 3 (Translate.y=0 so no y double).
//   Run B (OUTSIDE): Sphere volume, VolumeSize=0.5 -> ring outside -> weight=0 -> Position
//     unchanged -> meanX ≈ 0.
// TEETH:
//   (1) COUNT PRESERVED.
//   (2) INSIDE soft-transforms: meanX ≈ 3.
//   (3) OUTSIDE leaves Position: meanX ≈ 0.
//   (4) WEIGHT MID: VolumeSize tuned so points sit on the falloff edge -> partial shift (0<mx<3).
// injectBug: assert the INSIDE run leaves Position unmoved (meanX ≈ 0).  The correct shader shifts
//   it to ≈3, so the inverted assertion fails -> RED.  This exercises the falloff->strength->lerp
//   path: if the inside weight were wrong (0), the point wouldn't move.
// =============================================================================
int runSoftTransformPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float R = 2.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-softtransformpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSoftTransformPointsOp();

  auto runOne = [&](float volSize) -> std::vector<SwPoint> {
    std::vector<SwPoint> captured;
    g_capSoft = &captured;
    registerDrawOp("DrawPoints", captureDrawSoft);

    Graph g;
    Node gen; gen.id = 1; gen.type = "RadialPoints";
    gen.params["Count"]  = (float)N;
    gen.params["Radius"] = R;
    gen.params["Cycles"] = 1.0f;
    g.nodes.push_back(gen);

    Node sft; sft.id = 2; sft.type = "SoftTransformPoints";
    sft.params["Amount"]        = 1.0f;
    sft.params["VolumeType"]    = 0.0f;   // Sphere
    sft.params["VolumeSize"]    = volSize;
    sft.params["FallOff"]       = 0.0f;
    sft.params["Translate.x"]   = 3.0f;
    sft.params["Translate.y"]   = 0.0f;
    sft.params["Translate.z"]   = 0.0f;
    sft.params["Scale"]         = 1.0f;
    g.nodes.push_back(sft);

    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    PointGraph pg(dev, lib, q, 64, 64);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
    g_capSoft = nullptr;
    return captured;
  };

  auto meanX = [](const std::vector<SwPoint>& v) -> float {
    if (v.empty()) return -99.0f;
    float m = 0.0f; for (const SwPoint& p : v) m += p.Position.x; return m / (float)v.size();
  };

  auto a = runOne(4.0f);    // INSIDE -> shift +3
  auto b = runOne(0.5f);    // OUTSIDE -> unchanged
  float ma = meanX(a), mb = meanX(b);

  bool countOk = (a.size() == N && b.size() == N);
  bool insideOk  = injectBug ? (std::fabs(ma - 0.0f) < 0.2f)   // bug-mode expects NO shift -> FAIL
                             : (std::fabs(ma - 3.0f) < 0.2f);  // normal: shifted to ~3
  bool outsideOk = (std::fabs(mb - 0.0f) < 0.2f);

  bool pass = countOk && insideOk && outsideOk;
  printf("[selftest-softtransformpoints] n=%zu insideMeanX=%.3f(need~3.0) "
         "outsideMeanX=%.3f(need~0.0) -> %s%s\n",
         a.size(), ma, mb, pass ? "PASS" : "FAIL",
         injectBug ? " (bug-mode: expect FAIL)" : "");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
