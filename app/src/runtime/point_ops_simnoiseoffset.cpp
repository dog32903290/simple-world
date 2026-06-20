// SimNoiseOffset — sim-family MODIFIER (batch sw-node-batch): (simplex|curl)-noise position
// displacement + tangent-following rotation. Faithful port of external/tixl .../point/sim/
// SimNoiseOffset (.cs slots, .hlsl math). Reads c.inputs[0], writes count-preserving c.output.
// Count INHERITED from upstream.
//
// TiXL parity (SimNoiseOffset.cs/.hlsl):
//   - Defaults: Amount=0.2, Frequency=1, Phase=0, Variation=0, AmountDistribution=(1,1,1),
//               RotLookupDistance=2.0, UseCurlNoise=true
//   - noise = (UseCurlNoise ? curlNoise : snoiseVec3)(lookup) * Amount/100 * AmountDistribution
//   - Rotation = qMul(rotationFromDisplace, existingRotation)
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/simnoiseoffset_params.h"  // SimNoiseOffsetParams, SimNoiseOffsetBinding
#include "runtime/dispatch.h"               // calcDispatchCount
#include "runtime/graph.h"                  // Graph/Node/pinId
#include "runtime/point_graph.h"            // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"             // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSimNoiseOffset(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn = c.lib->newFunction(NS::String::string("simnoiseoffset", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SimNoiseOffsetParams P{};
  P.Count        = c.count;
  P.Amount       = cookParam(c, "Amount", 0.2f);
  P.Frequency    = cookParam(c, "Frequency", 1.0f);
  P.Phase        = cookParam(c, "Phase", 0.0f);
  P.Variation    = cookParam(c, "Variation", 0.0f);
  P.RotationLookupDistance = cookParam(c, "RotLookupDistance", 2.0f);  // .cs RotLookupDistance
  // .cs UseCurlNoise (bool, default true): cooked as float; >=0.5 -> 1 (curl), else 0 (snoise).
  P.UseCurlNoise = (cookParam(c, "UseCurlNoise", 1.0f) >= 0.5f) ? 1 : 0;

  float amtDist[3] = {1.0f, 1.0f, 1.0f};
  cookVecN(c, "AmountDistribution", amtDist, 3, amtDist);
  P.AmountDistributionX = amtDist[0];
  P.AmountDistributionY = amtDist[1];
  P.AmountDistributionZ = amtDist[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SIMNOISEOFFSET_SourcePoints);
  enc->setBuffer(c.output, 0, SIMNOISEOFFSET_ResultPoints);
  enc->setBytes(&P, sizeof(P), SIMNOISEOFFSET_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capSimNoise = nullptr;
void captureDrawSimNoise(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSimNoise || !pts || c.count == 0) return;
  g_capSimNoise->assign(c.count, SwPoint{});
  std::memcpy(g_capSimNoise->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSimNoiseOffsetOp() { registerPointOp("SimNoiseOffset", cookSimNoiseOffset); }

// Golden: RadialPoints(Count=128, Radius=1) -> SimNoiseOffset(Amount=10, Frequency=2,
//   Variation=0, UseCurlNoise=0) -> capture.
// NOTE on Amount: TiXL scales noise by Amount/100. The task's nominal Amount=0.5 yields a
//   ~0.005 displacement, below the 0.01 tooth floor. We raise Amount to 10 (=> ~0.1 scale) so
//   the displacement is observable; the op math is unchanged (faithful), only the test config.
// TEETH:
//   (1) count PRESERVED (modifier never changes count) == 128.
//   (2) points moved off the clean ring: max |r - R| > 0.01 (noise actually applied).
//   (3) per-point varied: dispSpread (radius-shift max-min) > 0.05.
//   (4) DETERMINISM: two cooks same params -> positions match within 1e-5 per point.
// injectBug: Amount=0 -> noise scaled to 0 -> points stay on the ring -> Tooth2/3 FAIL.
int runSimNoiseOffsetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128;
  const float R = 1.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-simnoiseoffset] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSimNoiseOffsetOp();
  std::vector<SwPoint> captured;
  g_capSimNoise = &captured;
  registerDrawOp("DrawPoints", captureDrawSimNoise);

  auto makeGraph = [&]() {
    Graph g;
    Node gen; gen.id = 1; gen.type = "RadialPoints";
    gen.params["Count"] = (float)N;
    gen.params["Radius"] = R;
    g.nodes.push_back(gen);

    Node mod; mod.id = 2; mod.type = "SimNoiseOffset";
    mod.params["Amount"]      = injectBug ? 0.0f : 10.0f;
    mod.params["Frequency"]   = 2.0f;
    mod.params["Phase"]       = 0.0f;
    mod.params["Variation"]   = 0.0f;
    mod.params["RotLookupDistance"] = 2.0f;
    mod.params["UseCurlNoise"] = 0.0f;  // snoiseVec3 path (deterministic & well-behaved)
    mod.params["AmountDistribution.x"] = 1.0f;
    mod.params["AmountDistribution.y"] = 1.0f;
    mod.params["AmountDistribution.z"] = 1.0f;
    g.nodes.push_back(mod);

    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
    return g;
  };

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g1 = makeGraph();
  pg.cook(g1, ctx, nullptr, pg.defaultDrawTarget(g1));
  std::vector<SwPoint> first = captured;

  captured.clear();
  Graph g2 = makeGraph();
  pg.cook(g2, ctx, nullptr, pg.defaultDrawTarget(g2));
  std::vector<SwPoint> second = captured;

  bool countOk = (captured.size() == N);

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

  bool noised = maxRadErr > 0.01f;
  bool varied = dispSpread > 0.05f;
  bool pass = countOk && noised && varied && deterministic;

  printf("[selftest-simnoiseoffset] n=%zu maxRadErr=%.4f(need>0.01) dispSpread=%.4f(need>0.05) "
         "deterministic=%s -> %s\n",
         first.size(), maxRadErr, dispSpread,
         deterministic ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  g_capSimNoise = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
