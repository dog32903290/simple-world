// SimCentricalOffset — sim-family MODIFIER (batch sw-node-batch): radial inverse-power force
// along (Position - Center). Faithful port of external/tixl .../point/sim/SimCentricalOffset
// (.cs slots, .hlsl math). Reads c.inputs[0], writes count-preserving c.output. Count INHERITED.
//
// TiXL parity (SimCentricalOffset.cs/.hlsl):
//   - Defaults: Center=(0,0,0), MaxAcceleration=1.0, Amount(->Acceleration)=0.04, DecayExponent=2.0
//   - force = clamp(Acceleration / pow(distance, DecayExponent), -MaxAcceleration, MaxAcceleration)
//   - offset = normalize(d/distance) * force; Position += offset  (d = Position - Center)
//   With POSITIVE Acceleration the force is positive and offset points ALONG +d, i.e. AWAY from
//   Center (outward). A negative Acceleration would pull inward. (.t3 default 0.04 = mild push out.)
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/simcentrialoffset_params.h"  // SimCentricalOffsetParams, SimCentricalOffsetBinding
#include "runtime/dispatch.h"                   // calcDispatchCount
#include "runtime/graph.h"                      // Graph/Node/pinId
#include "runtime/point_graph.h"                // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"                 // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookSimCentricalOffset(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("simcentrialoffset", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SimCentricalOffsetParams P{};
  P.Count           = c.count;
  P.MaxAcceleration = cookParam(c, "MaxAcceleration", 1.0f);
  P.Acceleration    = cookParam(c, "Amount", 0.04f);   // .cs Amount -> shader Acceleration
  P.DecayExponent   = cookParam(c, "DecayExponent", 2.0f);

  float center[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SIMCENTRICALOFFSET_SourcePoints);
  enc->setBuffer(c.output, 0, SIMCENTRICALOFFSET_ResultPoints);
  enc->setBytes(&P, sizeof(P), SIMCENTRICALOFFSET_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capSimCentrical = nullptr;
void captureDrawSimCentrical(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSimCentrical || !pts || c.count == 0) return;
  g_capSimCentrical->assign(c.count, SwPoint{});
  std::memcpy(g_capSimCentrical->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSimCentricalOffsetOp() {
  registerPointOp("SimCentricalOffset", cookSimCentricalOffset);
}

// Golden: RadialPoints(Count=256, Radius=2) -> SimCentricalOffset(Center=0, Amount=0.5,
//   MaxAcceleration=0.5, DecayExponent=2) -> capture.
// FORK from task spec: the task's Tooth-2 expected points pulled TOWARD center (mean|r|<1.9).
//   But TiXL math with POSITIVE Acceleration pushes points AWAY from center (offset = +d*force).
//   We test the FAITHFUL behavior: positive Amount => outward push (mean|r| > 2.0 + margin).
//   force = clamp(0.5/2^2, -0.5, 0.5) = 0.125 ; each point moves outward by ~0.125 => mean|r|~2.125.
// TEETH:
//   (1) count PRESERVED == 256.
//   (2) net outward push: mean |r| > 2.05 (radial force actually applied).
//   (3) force along +d (no point flipped past center): every point's post dot pre > 0
//       (offset keeps the point on the same side of Center as it started).
// injectBug: Amount=0 (Acceleration=0) -> force=0 -> no movement -> Tooth2 FAIL.
int runSimCentricalOffsetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 256;
  const float R = 2.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-simcentrialoffset] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerSimCentricalOffsetOp();
  std::vector<SwPoint> captured;
  g_capSimCentrical = &captured;
  registerDrawOp("DrawPoints", captureDrawSimCentrical);

  // Clean ring positions for the "same side of center" tooth (RadialPoints, Z=0 plane).
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

  Node mod; mod.id = 2; mod.type = "SimCentricalOffset";
  mod.params["Center.x"] = 0.0f; mod.params["Center.y"] = 0.0f; mod.params["Center.z"] = 0.0f;
  mod.params["Amount"]          = injectBug ? 0.0f : 0.5f;  // -> Acceleration
  mod.params["MaxAcceleration"] = 0.5f;
  mod.params["DecayExponent"]   = 2.0f;
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
  bool sameSide = true;
  size_t cmpN = std::min(out.size(), clean.size());
  for (size_t k = 0; k < cmpN; ++k) {
    const SwPoint& p = out[k];
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    sumR += r;
    // dot(post, pre) > 0 -> offset did not flip the point past the center.
    const SwPoint& cp = clean[k];
    float dot = p.Position.x * cp.Position.x + p.Position.y * cp.Position.y +
                p.Position.z * cp.Position.z;
    if (dot <= 0.0f) sameSide = false;
  }
  float meanR = cmpN ? (float)(sumR / (double)cmpN) : 0.0f;

  bool pushedOut = meanR > 2.05f;
  bool pass = countOk && pushedOut && sameSide;

  printf("[selftest-simcentrialoffset] n=%zu meanR=%.4f(need>2.05) sameSide=%s -> %s\n",
         out.size(), meanR, sameSide ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capSimCentrical = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
