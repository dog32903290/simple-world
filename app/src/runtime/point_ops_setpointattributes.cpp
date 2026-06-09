// SetPointAttributes — lane-A MODIFIER op: cook fn + register + golden. Faithful port of
// external/tixl .../point/modify/SetPointAttributes (.cs ports, .hlsl math). Reads an input bag
// (c.inputs[0]) and writes a count-preserving bag (c.output) where each enabled Set* flag lerps
// (Rotation: slerps) the named attribute toward a supplied target by a per-point `strength`. The
// count is INHERITED from the upstream bag (no Count param — modifiers get their input's count).
//
// Self-contained leaf (its own capture vector + registerDrawOp). Copies the TransformPoints
// modifier template exactly: scalar params via paramOr on the cooked node (c.nodeId), vector
// params (Position/RotationAxis/Stretch/Color) via readVecN(*n,...).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                     // calcDispatchCount
#include "runtime/graph.h"                        // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"                  // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/setpointattributes_params.h"    // SetPointAttributesParams, SetPtAttrBinding
#include "runtime/tixl_point.h"                   // SwPoint (64B) + EvaluationContext

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

// SetPointAttributes modifier: dispatch the setpointattributes kernel input bag -> output bag.
// count comes from c.count (inherited from the upstream Points bag). No input bag = safe no-op.
void cookSetPointAttributes(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to modify

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("setpointattributes", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const Node* n = c.graph ? c.graph->node(c.nodeId) : nullptr;
  SetPointAttributesParams P{};
  P.Count = c.count;
  P.Amount = paramOr(n, "Amount", 1.0f);
  P.AmountFactor = (int)(paramOr(n, "AmountFactor", 0.0f) + 0.5f);

  P.SetPosition = paramOr(n, "SetPosition", 0.0f) > 0.5f ? 1 : 0;
  P.SetRotation = paramOr(n, "SetRotation", 0.0f) > 0.5f ? 1 : 0;
  P.SetStretch = paramOr(n, "SetStretch", 0.0f) > 0.5f ? 1 : 0;
  P.SetFx1 = paramOr(n, "SetFx1", 0.0f) > 0.5f ? 1 : 0;
  P.SetFx2 = paramOr(n, "SetFx2", 0.0f) > 0.5f ? 1 : 0;
  P.SetColor = paramOr(n, "SetColor", 0.0f) > 0.5f ? 1 : 0;

  float pos[3] = {0, 0, 0}, axis[3] = {0, 1, 0}, str[3] = {1, 1, 1}, col[4] = {1, 1, 1, 1};
  if (n) {
    readVecN(*n, "Position", pos, 3, pos);
    readVecN(*n, "RotationAxis", axis, 3, axis);
    readVecN(*n, "Stretch", str, 3, str);
    readVecN(*n, "Color", col, 4, col);
  }
  P.PositionX = pos[0]; P.PositionY = pos[1]; P.PositionZ = pos[2];
  P.RotationAxisX = axis[0]; P.RotationAxisY = axis[1]; P.RotationAxisZ = axis[2];
  P.RotationAngle = paramOr(n, "RotationAngle", 0.0f);
  P.StretchX = str[0]; P.StretchY = str[1]; P.StretchZ = str[2];
  P.Fx1 = paramOr(n, "Fx1", 0.0f);
  P.Fx2 = paramOr(n, "Fx2", 0.0f);
  P.ColorR = col[0]; P.ColorG = col[1]; P.ColorB = col[2]; P.ColorA = col[3];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SETPTATTR_SourcePoints);
  enc->setBuffer(c.output, 0, SETPTATTR_ResultPoints);
  enc->setBytes(&P, sizeof(P), SETPTATTR_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capSpa = nullptr;
void captureDrawSpa(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSpa || !pts || c.count == 0) return;
  g_capSpa->assign(c.count, SwPoint{});
  std::memcpy(g_capSpa->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSetPointAttributesOp() {
  registerPointOp("SetPointAttributes", cookSetPointAttributes);
}

// Golden: SpherePoints(radius R, default white-ish color) -> SetPointAttributes(SetColor=on with
// a distinctive target, SetStretch=on with a non-default scale, Amount=1) -> capture. With full
// strength the write is a REPLACE (lerp(old,new,1)==new): EVERY captured point must carry the
// exact target Color AND target Scale, while its Position is UNTOUCHED (still on the sphere of
// radius R about Center — proves the modifier is count/position-preserving and only overwrites the
// gated attributes). injectBug: Amount=0 -> strength 0 -> lerp(old,new,0)==old -> no attribute
// changes -> the Color/Scale assertion FAILs (a REAL no-op degeneracy, not a flipped assert).
int runSetPointAttributesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024;
  const float R = 2.0f;
  const float CX = 3.0f, CY = -1.0f, CZ = 2.0f;
  // distinctive targets, chosen to differ from any plausible generator default.
  const float TCR = 0.20f, TCG = 0.70f, TCB = 0.95f, TCA = 0.50f;  // target Color
  const float TSX = 4.0f, TSY = 0.25f, TSZ = 7.0f;                 // target Stretch -> Scale
  const float amount = injectBug ? 0.0f : 1.0f;  // bug: strength 0 -> lerp keeps the old attrs

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-setpointattributes] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();        // SpherePoints (the input generator) + draw
  registerSetPointAttributesOp();   // this op (explicit -> self-contained)
  std::vector<SwPoint> captured;
  g_capSpa = &captured;
  registerDrawOp("DrawPoints", captureDrawSpa);  // capture-only draw for the assertion

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "SpherePoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  gen.params["Center.x"] = CX; gen.params["Center.y"] = CY; gen.params["Center.z"] = CZ;
  g.nodes.push_back(gen);

  Node mod; mod.id = 2; mod.type = "SetPointAttributes";
  mod.params["Amount"] = amount;
  mod.params["AmountFactor"] = 0.0f;       // None -> factor 1 (so strength == Amount)
  mod.params["SetColor"] = 1.0f;
  mod.params["Color.x"] = TCR; mod.params["Color.y"] = TCG;
  mod.params["Color.z"] = TCB; mod.params["Color.w"] = TCA;
  mod.params["SetStretch"] = 1.0f;
  mod.params["Stretch.x"] = TSX; mod.params["Stretch.y"] = TSY; mod.params["Stretch.z"] = TSZ;
  g.nodes.push_back(mod);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // SpherePoints.out -> SetPtAttr.in(port0)
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // SetPtAttr.out(port1) -> DrawPoints.in

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // TEETH 1: with strength 1 the gated attributes are REPLACED -> every point carries the exact
  //          target Color and target Scale.
  // TEETH 2: ungated Position is preserved -> every point still sits radius R from Center.
  bool ok = captured.size() == N;
  float maxColErr = 0.0f, maxScaleErr = 0.0f, maxRadErr = 0.0f;
  for (const SwPoint& p : captured) {
    float ce = std::fabs(p.Color.x - TCR) + std::fabs(p.Color.y - TCG) +
               std::fabs(p.Color.z - TCB) + std::fabs(p.Color.w - TCA);
    float se = std::fabs(p.Scale.x - TSX) + std::fabs(p.Scale.y - TSY) + std::fabs(p.Scale.z - TSZ);
    float dx = p.Position.x - CX, dy = p.Position.y - CY, dz = p.Position.z - CZ;
    float re = std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - R);
    if (ce > maxColErr) maxColErr = ce;
    if (se > maxScaleErr) maxScaleErr = se;
    if (re > maxRadErr) maxRadErr = re;
    ok = ok && ce < 1e-3f && se < 1e-3f && re < 0.02f;
  }
  bool pass = (captured.size() == N) && ok;
  printf("[selftest-setpointattributes] n=%zu colErr=%.4f scaleErr=%.4f posRadErr=%.4f"
         "(need col/scale<1e-3, rad<0.02) -> %s\n",
         captured.size(), maxColErr, maxScaleErr, maxRadErr, pass ? "PASS" : "FAIL");

  g_capSpa = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
