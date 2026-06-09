// RandomizePoints — lane-A MODIFIER op: cook fn + register + golden. Faithful port of
// external/tixl .../point/modify/RandomizePoints (.cs slots, .hlsl math). Reads an input bag
// (c.inputs[0]) and writes a count-preserving bag (c.output) whose every point has its
// Position/Rotation/Scale/F1/F2/Color jittered by a per-point hash-driven pseudo-random offset.
// The point count is INHERITED from the upstream bag (no Count param — PointGraph::nodeCount
// gives a modifier its input's count). Copies the TransformPoints modifier TEMPLATE shape.
//
// Self-contained leaf (its own capture vector + registerDrawOp). The cook reads scalar params
// via paramOr on the node being cooked (c.nodeId) and vector params via readVecN(*n,...).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                // calcDispatchCount
#include "runtime/graph.h"                   // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"             // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/randomizepoints_params.h"  // RandomizeParams, RandomizeBinding
#include "runtime/tixl_point.h"              // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

float paramOr(const Node* n, const char* id, float def) {
  if (!n) return def;
  auto it = n->params.find(id);
  return it != n->params.end() ? it->second : def;
}

// RandomizePoints modifier: dispatch the randomizepoints kernel input bag -> output bag.
// count comes from c.count (inherited from the upstream Points bag). No input bag = safe no-op.
void cookRandomizePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to randomize

  MTL::Function* fn = c.lib->newFunction(NS::String::string("randomizepoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const Node* n = c.graph ? c.graph->node(c.nodeId) : nullptr;
  RandomizeParams P{};
  P.Count = c.count;
  P.Strength = paramOr(n, "Strength", 1.0f);

  float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, str[3] = {0, 0, 0};
  float col[4] = {0, 0, 0, 0}, gb[2] = {0.5f, 0.5f};
  if (n) {
    readVecN(*n, "Position", pos, 3, pos);
    readVecN(*n, "Rotation", rot, 3, rot);
    readVecN(*n, "Stretch", str, 3, str);
    readVecN(*n, "ColorHSB", col, 4, col);
    readVecN(*n, "GainAndBias", gb, 2, gb);
  }
  P.RandomizePositionX = pos[0]; P.RandomizePositionY = pos[1]; P.RandomizePositionZ = pos[2];
  P.RandomizeRotationX = rot[0]; P.RandomizeRotationY = rot[1]; P.RandomizeRotationZ = rot[2];
  P.StretchX = str[0]; P.StretchY = str[1]; P.StretchZ = str[2];
  P.RandomizeColorX = col[0]; P.RandomizeColorY = col[1];
  P.RandomizeColorZ = col[2]; P.RandomizeColorW = col[3];
  P.GainAndBiasX = gb[0]; P.GainAndBiasY = gb[1];

  P.Scale = paramOr(n, "Scale", 0.0f);
  P.RandomizeF1 = paramOr(n, "F1", 0.0f);
  P.RandomizeF2 = paramOr(n, "F2", 0.0f);
  P.RandomSeed = paramOr(n, "RandomPhase", 0.0f);

  P.OffsetMode = (uint32_t)(paramOr(n, "OffsetMode", 0.0f) + 0.5f);
  P.UsePointSpace = (uint32_t)(paramOr(n, "Space", 0.0f) + 0.5f);
  P.Interpolation = (uint32_t)(paramOr(n, "Interpolation", 1.0f) + 0.5f);
  P.ClampColorsEtc = paramOr(n, "ClampColorsEtc", 0.0f) > 0.5f ? 1 : 0;
  P.Repeat = (int32_t)(paramOr(n, "Repeat", 0.0f) + 0.5f);
  P.StrengthFactor = (int32_t)(paramOr(n, "StrengthFactor", 0.0f) + 0.5f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, RANDOMIZE_SourcePoints);
  enc->setBuffer(c.output, 0, RANDOMIZE_ResultPoints);
  enc->setBytes(&P, sizeof(P), RANDOMIZE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capRnd = nullptr;
void captureDrawRnd(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capRnd || !pts || c.count == 0) return;
  g_capRnd->assign(c.count, SwPoint{});
  std::memcpy(g_capRnd->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerRandomizePointsOp() { registerPointOp("RandomizePoints", cookRandomizePoints); }

// Golden: SpherePoints(R at origin) -> RandomizePoints(ObjectSpace, Position=(P,P,P),
// OffsetMode=Scatter, Strength=1) -> capture. TEETH:
//  (1) count is PRESERVED (a modifier never changes the point count).
//  (2) the jitter actually MOVED points off the clean sphere: the per-point radius from origin
//      now SCATTERS (max |r-R| is well above 0 — a clean SpherePoints has every r==R exactly),
//      AND the displacements are not all identical (different points get different offsets),
//      AND the mean displacement is ~0 (Scatter centers the noise on [-1,1] -> no net drift).
// injectBug: Strength=0 -> the kernel adds 0 everywhere -> points stay exactly on the clean
// sphere (max |r-R| ~ 0, displacement spread ~ 0) -> the "it actually jittered" assertion FAILs.
int runRandomizePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024;
  const float R = 2.0f;
  const float JIT = 0.5f;  // per-axis position jitter amplitude

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-randomizepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();      // SpherePoints (the input generator) + friends
  registerRandomizePointsOp();    // RandomizePoints (this op; explicit -> self-contained)
  std::vector<SwPoint> captured;
  g_capRnd = &captured;
  registerDrawOp("DrawPoints", captureDrawRnd);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "SpherePoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  g.nodes.push_back(gen);

  Node rnd; rnd.id = 2; rnd.type = "RandomizePoints";
  rnd.params["Strength"] = injectBug ? 0.0f : 1.0f;  // bug: 0 strength -> identity passthrough
  rnd.params["StrengthFactor"] = 0.0f;               // None -> strength is the raw Strength
  rnd.params["Space"] = 1.0f;                        // ObjectSpace -> raw world-axis jitter
  rnd.params["OffsetMode"] = 1.0f;                   // Scatter -> centered [-1,1] noise
  rnd.params["Interpolation"] = 0.0f;                // None -> deterministic per-point
  rnd.params["Repeat"] = 0.0f;
  rnd.params["RandomPhase"] = 0.0f;
  rnd.params["Position.x"] = JIT; rnd.params["Position.y"] = JIT; rnd.params["Position.z"] = JIT;
  rnd.params["GainAndBias.x"] = 0.5f; rnd.params["GainAndBias.y"] = 0.5f;  // neutral remap
  g.nodes.push_back(rnd);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // SpherePoints.out -> RandomizePoints.in(port0)
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // RandomizePoints.out(port1) -> DrawPoints.in

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // Measure how far the modifier moved each point off the clean sphere (|pos|==R originally).
  bool countOk = captured.size() == N;
  float maxRadErr = 0.0f;           // peak |r - R| -> the jitter shoved points off the sphere
  float dispMin = 1e9f, dispMax = -1e9f;  // spread of per-point radial displacement
  double meanSignedDisp = 0.0;      // Scatter should center on ~0 (no net drift)
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y +
                        p.Position.z * p.Position.z);
    float disp = r - R;
    float e = std::fabs(disp);
    if (e > maxRadErr) maxRadErr = e;
    if (disp < dispMin) dispMin = disp;
    if (disp > dispMax) dispMax = disp;
    meanSignedDisp += disp;
  }
  if (!captured.empty()) meanSignedDisp /= (double)captured.size();
  float dispSpread = (captured.size() == N) ? (dispMax - dispMin) : 0.0f;

  // TEETH: with Strength=1 the jitter is real (points left the sphere by a meaningful amount AND
  // by DIFFERENT amounts) and centered (Scatter -> |mean| small). injectBug (Strength=0) makes
  // maxRadErr ~ 0 and dispSpread ~ 0 -> both jitter assertions FAIL.
  bool jittered = maxRadErr > 0.05f;                 // points actually left the clean sphere
  bool varied = dispSpread > 0.1f;                   // not a uniform shift -> per-point noise
  bool centered = std::fabs((float)meanSignedDisp) < 0.15f;  // Scatter -> ~no net drift
  bool pass = countOk && jittered && varied && centered;
  printf("[selftest-randomizepoints] n=%zu maxRadErr=%.4f(need>0.05) dispSpread=%.4f(need>0.1) "
         "meanDisp=%.4f(|.|<0.15) -> %s\n",
         captured.size(), maxRadErr, dispSpread, (float)meanSignedDisp, pass ? "PASS" : "FAIL");

  g_capRnd = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
