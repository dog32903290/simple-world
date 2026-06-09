// TransformPoints — lane-A MODIFIER op: cook fn + register + golden. Faithful port of
// external/tixl .../point/transform/TransformPoints (.cs ports, .hlsl math). Reads an input
// bag (c.inputs[0]) and writes a TRS-transformed bag (c.output); the point count is INHERITED
// from the upstream bag (no Count param — PointGraph::nodeCount gives a modifier its input's
// count). This is the first modifier and the TEMPLATE the batch-2 fan-out copies.
//
// Self-contained leaf (its own capture vector + registerDrawOp). The cook reads scalar params
// via paramOr on the node being cooked (c.nodeId) and the vector params via readVecN(*n,...).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"               // calcDispatchCount
#include "runtime/graph.h"                  // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"            // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/transformpoints_params.h" // TransformParams, TransformBinding
#include "runtime/tixl_point.h"             // SwPoint (64B) + EvaluationContext

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

// TransformPoints modifier: dispatch the transformpoints kernel input bag -> output bag.
// count comes from c.count (inherited from the upstream Points bag). No input bag = safe no-op.
void cookTransformPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to transform

  MTL::Function* fn = c.lib->newFunction(NS::String::string("transformpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const Node* n = c.graph ? c.graph->node(c.nodeId) : nullptr;
  TransformParams P{};
  P.Count = c.count;
  P.Space = (int)(paramOr(n, "Space", 0.0f) + 0.5f);
  float t[3] = {0, 0, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1}, pv[3] = {0, 0, 0};
  if (n) {
    readVecN(*n, "Translation", t, 3, t);
    readVecN(*n, "Rotation", r, 3, r);
    readVecN(*n, "Stretch", s, 3, s);
    readVecN(*n, "Pivot", pv, 3, pv);
  }
  P.TranslationX = t[0]; P.TranslationY = t[1]; P.TranslationZ = t[2];
  P.RotationX = r[0]; P.RotationY = r[1]; P.RotationZ = r[2];
  P.StretchX = s[0]; P.StretchY = s[1]; P.StretchZ = s[2];
  P.PivotX = pv[0]; P.PivotY = pv[1]; P.PivotZ = pv[2];
  P.Scale = paramOr(n, "Scale", 1.0f);
  P.Strength = paramOr(n, "Strength", 1.0f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, TRANSFORM_SourcePoints);
  enc->setBuffer(c.output, 0, TRANSFORM_ResultPoints);
  enc->setBytes(&P, sizeof(P), TRANSFORM_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capXf = nullptr;
void captureDrawXf(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capXf || !pts || c.count == 0) return;
  g_capXf->assign(c.count, SwPoint{});
  std::memcpy(g_capXf->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerTransformPointsOp() { registerPointOp("TransformPoints", cookTransformPoints); }

// Golden: RadialPoints(ring R at origin) -> TransformPoints(ObjectSpace, Stretch=2, Translate=
// (5,0,0), Strength=1) -> capture. The whole ring must scale x2 and shift +5 in x: every point
// sits radius 2R from (5,0,0), and mean x ~= 5. Proves the modifier input-bag flow (reads the
// upstream bag, writes a transformed one) + the TRS math end to end. injectBug: Strength=0 ->
// identity passthrough -> ring stays radius R at origin -> the radius/center assertion FAILs.
int runTransformPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float R = 2.0f;
  const float SCALE = 2.0f, TX = 5.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-transformpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();    // RadialPoints (the input generator)
  registerTransformPointsOp();  // TransformPoints (this op; explicit -> self-contained)
  std::vector<SwPoint> captured;
  g_capXf = &captured;
  registerDrawOp("DrawPoints", captureDrawXf);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  gen.params["Cycles"] = 1.0f;
  g.nodes.push_back(gen);
  Node xf; xf.id = 2; xf.type = "TransformPoints";
  xf.params["Space"] = 1.0f;  // ObjectSpace
  xf.params["Stretch.x"] = SCALE; xf.params["Stretch.y"] = SCALE; xf.params["Stretch.z"] = SCALE;
  xf.params["Scale"] = 1.0f;
  xf.params["Translation.x"] = TX;
  xf.params["Strength"] = injectBug ? 0.0f : 1.0f;  // bug: identity passthrough
  g.nodes.push_back(xf);
  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points(out) -> TransformPoints.points(in, port0)
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // TransformPoints.out(port1) -> DrawPoints.points(in)

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool onScaled = captured.size() == N;
  float meanX = 0.0f, maxRadErr = 0.0f;
  const float wantR = R * SCALE;  // 4
  for (const SwPoint& p : captured) {
    meanX += p.Position.x;
    float dx = p.Position.x - TX, dy = p.Position.y;  // distance from the translated center
    float e = std::fabs(std::sqrt(dx * dx + dy * dy) - wantR);
    if (e > maxRadErr) maxRadErr = e;
    onScaled = onScaled && e < 0.05f;
  }
  if (!captured.empty()) meanX /= (float)captured.size();
  bool pass = (captured.size() == N) && std::fabs(meanX - TX) < 0.1f && onScaled;
  printf("[selftest-transformpoints] n=%zu meanX=%.3f(need~%.1f) ringR=%.2f(need~%.1f maxErr=%.4f) -> %s\n",
         captured.size(), meanX, TX, wantR, wantR, maxRadErr, pass ? "PASS" : "FAIL");

  g_capXf = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
