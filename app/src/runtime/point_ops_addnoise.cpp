// AddNoise — lane-A MODIFIER op (batch 15): simplex-noise position + rotation perturbation.
// Faithful port of external/tixl .../point/modify/AddNoise (.cs slots, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output) where each
// point's Position is displaced by a snoiseVec3 field and Rotation is updated to follow the
// displaced tangent frame (RotationLookupDistance probe). Count is INHERITED from upstream.
//
// TiXL parity (AddNoise.cs/.hlsl):
//   - StrengthFactor enum: None=0(weight=1), F1=1(p.FX1), F2=2(p.FX2)
//   - Noise: snoiseVec3 (3 decorrelated simplex evaluations) * Amount/10 * AmountDistribution
//   - Rotation: orthonormal frame from probed tangent directions -> qFromMatrix3Precise
//   - Defaults: Strength=1, StrengthFactor=None, Frequency=1, Phase=0, Variation=0,
//               AmountDistribution=(1,1,1), RotationLookupDistance=0.25, NoiseOffset=(0,0,0)
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/addnoise_params.h"   // AddNoiseParams, AddNoiseBinding
#include "runtime/dispatch.h"          // calcDispatchCount
#include "runtime/graph.h"             // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"       // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"        // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// AddNoise modifier: dispatch the addnoise kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookAddNoise(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn = c.lib->newFunction(NS::String::string("addnoise", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  AddNoiseParams P{};
  P.Count        = c.count;
  P.Amount       = cookParam(c, "Strength", 1.0f);     // TiXL .cs: Strength -> shader: Amount
  P.Frequency    = cookParam(c, "Frequency", 1.0f);
  P.Phase        = cookParam(c, "Phase", 0.0f);
  P.Variation    = cookParam(c, "Variation", 0.0f);
  P.StrengthMode = (int32_t)(cookParam(c, "StrengthFactor", 0.0f) + 0.5f);
  P.RotationLookupDistance = cookParam(c, "RotationLookupDistance", 0.25f);

  float amtDist[3] = {1.0f, 1.0f, 1.0f};
  cookVecN(c, "AmountDistribution", amtDist, 3, amtDist);
  P.AmountDistributionX = amtDist[0];
  P.AmountDistributionY = amtDist[1];
  P.AmountDistributionZ = amtDist[2];

  float noiseOff[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "NoiseOffset", noiseOff, 3, noiseOff);
  P.NoiseOffsetX = noiseOff[0];
  P.NoiseOffsetY = noiseOff[1];
  P.NoiseOffsetZ = noiseOff[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, ADDNOISE_SourcePoints);
  enc->setBuffer(c.output, 0, ADDNOISE_ResultPoints);
  enc->setBytes(&P, sizeof(P), ADDNOISE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capNoise = nullptr;
void captureDrawNoise(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capNoise || !pts || c.count == 0) return;
  g_capNoise->assign(c.count, SwPoint{});
  std::memcpy(g_capNoise->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerAddNoiseOp() { registerPointOp("AddNoise", cookAddNoise); }

// Golden: SpherePoints(R=1, N=512) -> AddNoise(Strength=1, Frequency=2) -> capture.
// TEETH:
//   (1) count is PRESERVED (modifier never changes point count).
//   (2) points moved off the clean sphere: max |r - R| > 0.01 (noise actually applied).
//   (3) displacements are per-point varied (dispSpread > 0.05, not all the same shift).
//   (4) DETERMINISM: same seed / same params -> same output (simplex noise is deterministic).
//       Run twice, compare positions element-by-element within 1e-5.
// injectBug: Strength=0 -> noise is scaled to 0 -> points stay on clean sphere
//   (max |r-R| ~ 0) -> "noise actually moved points" assertion FAILS.
int runAddNoiseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 512;
  const float R = 1.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-addnoise] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerAddNoiseOp();
  std::vector<SwPoint> captured;
  g_capNoise = &captured;
  registerDrawOp("DrawPoints", captureDrawNoise);

  // Build graph: SpherePoints -> AddNoise -> DrawPoints
  auto makeGraph = [&]() {
    Graph g;
    Node gen; gen.id = 1; gen.type = "SpherePoints";
    gen.params["Count"] = (float)N;
    gen.params["Radius"] = R;
    g.nodes.push_back(gen);

    Node noise; noise.id = 2; noise.type = "AddNoise";
    noise.params["Strength"]    = injectBug ? 0.0f : 1.0f;
    noise.params["Frequency"]   = 2.0f;
    noise.params["Variation"]   = 0.0f;   // deterministic (no per-point random variation)
    noise.params["Phase"]       = 0.0f;
    noise.params["AmountDistribution.x"] = 1.0f;
    noise.params["AmountDistribution.y"] = 1.0f;
    noise.params["AmountDistribution.z"] = 1.0f;
    noise.params["RotationLookupDistance"] = 0.25f;
    noise.params["NoiseOffset.x"] = 0.0f;
    noise.params["NoiseOffset.y"] = 0.0f;
    noise.params["NoiseOffset.z"] = 0.0f;
    noise.params["StrengthFactor"] = 0.0f;  // None
    g.nodes.push_back(noise);

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

  // Second cook (determinism check — same graph, same params)
  captured.clear();
  Graph g2 = makeGraph();
  pg.cook(g2, ctx, nullptr, pg.defaultDrawTarget(g2));
  std::vector<SwPoint> second = captured;

  bool countOk = (captured.size() == N);

  // Measure displacement off clean sphere (|pos|-R)
  float maxRadErr = 0.0f;
  float dispMin = 1e9f, dispMax = -1e9f;
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

  // Determinism check
  bool deterministic = true;
  if (first.size() == second.size()) {
    for (size_t k = 0; k < first.size() && deterministic; ++k) {
      float dx = std::fabs(first[k].Position.x - second[k].Position.x);
      float dy = std::fabs(first[k].Position.y - second[k].Position.y);
      float dz = std::fabs(first[k].Position.z - second[k].Position.z);
      if (dx > 1e-5f || dy > 1e-5f || dz > 1e-5f) deterministic = false;
    }
  } else {
    deterministic = false;
  }

  bool noised   = maxRadErr > 0.01f;
  bool varied   = dispSpread > 0.05f;
  bool pass = countOk && noised && varied && deterministic;

  printf("[selftest-addnoise] n=%zu maxRadErr=%.4f(need>0.01) dispSpread=%.4f(need>0.05) "
         "deterministic=%s -> %s\n",
         first.size(), maxRadErr, dispSpread,
         deterministic ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  g_capNoise = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
