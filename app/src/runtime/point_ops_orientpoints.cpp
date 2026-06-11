// OrientPoints — lane-A MODIFIER op: cook fn + register + golden. Faithful port of
// external/tixl .../point/transform/OrientPoints (.cs ports, .hlsl math, the file lives under
// Assets/shaders/points/modify/OrientPoints.hlsl). A count-preserving orientation modifier:
// reads an input bag (c.inputs[0]) and writes the SAME points with each Rotation quaternion
// re-aimed so local +Z points toward a Target (mode 0). Count is INHERITED from the upstream bag
// (no Count param). A batch-2 leaf copying the transformpoints.cpp template exactly.
//
// Self-contained leaf (its own capture vector + registerDrawOp). The cook reads scalar params via
// paramOr on the node being cooked (c.nodeId) and the vector params via readVecN(*n,...).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"            // calcDispatchCount
#include "runtime/graph.h"              // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"        // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/orientpoints_params.h"  // OrientParams, OrientBinding
#include "runtime/tixl_point.h"         // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// OrientPoints modifier: dispatch the orientpoints kernel input bag -> output bag.
// count comes from c.count (inherited from the upstream Points bag). No input bag = safe no-op.
void cookOrientPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to orient

  MTL::Function* fn = c.lib->newFunction(NS::String::string("orientpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  OrientParams P{};
  P.Count = c.count;
  P.AmountFactor = (int)(cookParam(c, "AmountFactor", 0.0f) + 0.5f);
  P.Flip = (int)(cookParam(c, "Flip", 0.0f) + 0.5f);
  P.OrientationMode = (int)(cookParam(c, "OrientationMode", 0.0f) + 0.5f);
  P.Amount = cookParam(c, "Amount", 1.0f);
  float target[3] = {0, 0, 0}, up[3] = {0, 1, 0};
  cookVecN(c, "Target", target, 3, target);
  cookVecN(c, "UpVector", up, 3, up);
  P.TargetX = target[0]; P.TargetY = target[1]; P.TargetZ = target[2];
  P.UpVectorX = up[0]; P.UpVectorY = up[1]; P.UpVectorZ = up[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, ORIENT_SourcePoints);
  enc->setBuffer(c.output, 0, ORIENT_ResultPoints);
  enc->setBytes(&P, sizeof(P), ORIENT_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capOrient = nullptr;
void captureDrawOrient(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capOrient || !pts || c.count == 0) return;
  g_capOrient->assign(c.count, SwPoint{});
  std::memcpy(g_capOrient->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// CPU mirror of quat.metal.h's qRotateVec3 (float4 = x,y,z imaginary, w real) — to assert the
// orientation the kernel produced (rotate the point's local +Z and check where it aims).
void qRotateVec3Host(const float q[4], const float v[3], float out[3]) {
  // t = 2 * cross(q.xyz, v); out = v + q.w*t + cross(q.xyz, t)
  float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  float cx = q[1] * tz - q[2] * ty;
  float cy = q[2] * tx - q[0] * tz;
  float cz = q[0] * ty - q[1] * tx;
  out[0] = v[0] + q[3] * tx + cx;
  out[1] = v[1] + q[3] * ty + cy;
  out[2] = v[2] + q[3] * tz + cz;
}

}  // namespace

void registerOrientPointsOp() { registerPointOp("OrientPoints", cookOrientPoints); }

// Golden: SpherePoints(radius R around Center C) -> OrientPoints(LookAtTarget at Target=C,
// Amount=1, Flip=0) -> capture. TiXL mode 0 aims each point's local +Z toward the Target, so for
// a cloud sitting ON a sphere of radius R around C, every rotated +Z must point from the point
// INWARD toward C, i.e. parallel to normalize(C - pos). TEETH: for every (non-degenerate) point,
// dot(rotate(+Z, Rotation), normalize(C - pos)) ~= +1. Proves the modifier input-bag flow (reads
// the upstream bag, writes a re-oriented one) + the qLookAt/180°-align/slerp math end to end.
// injectBug: Amount=0 -> slerp(old, new, 0) = old = SpherePoints' baked (identity) orientation (NOT
// aimed at the target), so the +Z-toward-center assertion FAILs (a REAL degeneracy, not a flip).
int runOrientPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024;
  const float R = 2.0f;
  const float CX = 3.0f, CY = -1.0f, CZ = 2.0f;  // sphere center == orient Target

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-orientpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();  // SpherePoints (the input generator) — explicit, self-contained
  registerOrientPointsOp();   // OrientPoints (this op)
  std::vector<SwPoint> captured;
  g_capOrient = &captured;
  registerDrawOp("DrawPoints", captureDrawOrient);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "SpherePoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  gen.params["Center.x"] = CX; gen.params["Center.y"] = CY; gen.params["Center.z"] = CZ;
  g.nodes.push_back(gen);
  Node mod; mod.id = 2; mod.type = "OrientPoints";
  mod.params["OrientationMode"] = 0.0f;  // LookAtTarget
  mod.params["AmountFactor"] = 0.0f;     // None -> weight 1
  mod.params["Flip"] = 0.0f;
  mod.params["Amount"] = injectBug ? 0.0f : 1.0f;  // bug: slerp t=0 -> keeps the baked orientation
  mod.params["Target.x"] = CX; mod.params["Target.y"] = CY; mod.params["Target.z"] = CZ;
  mod.params["UpVector.x"] = 0.0f; mod.params["UpVector.y"] = 1.0f; mod.params["UpVector.z"] = 0.0f;
  g.nodes.push_back(mod);
  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // SpherePoints.points -> OrientPoints.points(port0)
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // OrientPoints.out(port1) -> DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  // TEETH: every point's rotated local +Z aims toward the Target (== sphere Center). For a point
  // ON the sphere, normalize(C - pos) is well defined (|C - pos| == R > 0). After OrientPoints,
  // dot(rotate(+Z), normalize(C - pos)) must be ~+1 across the cloud.
  //
  // Honest caveat (TiXL's own behavior, NOT a port defect): qLookAt is undefined when forward ∥ up
  // (cross(forward,up)==0 -> normalize(0) -> NaN). On a full sphere the aim direction sweeps EVERY
  // direction, so a handful of points near the up-axis poles are degenerate for ANY fixed UpVector.
  // We skip those (|forward × up| < 1e-3) from the per-point gate — but they must be FEW (TEETH 2),
  // and EVERY non-degenerate point must aim true (TEETH 1), and the mean over all valid points ≈ 1.
  const float up3[3] = {0.0f, 1.0f, 0.0f};
  bool aimed = captured.size() == N;
  float minDot = 1e9f, meanDot = 0.0f;
  int valid = 0, skipped = 0;
  const float Z[3] = {0.0f, 0.0f, 1.0f};
  for (const SwPoint& p : captured) {
    float toC[3] = {CX - p.Position.x, CY - p.Position.y, CZ - p.Position.z};
    float len = std::sqrt(toC[0] * toC[0] + toC[1] * toC[1] + toC[2] * toC[2]);
    if (len < 1e-5f) { aimed = false; continue; }
    toC[0] /= len; toC[1] /= len; toC[2] /= len;
    // forward (== normalize(Target-pos)) is -toC; degenerate iff forward ∥ up <=> toC ∥ up.
    float cx = toC[1] * up3[2] - toC[2] * up3[1];
    float cy = toC[2] * up3[0] - toC[0] * up3[2];
    float cz = toC[0] * up3[1] - toC[1] * up3[0];
    if (std::sqrt(cx * cx + cy * cy + cz * cz) < 1e-3f) { ++skipped; continue; }  // pole: qLookAt undef
    float rq[4] = {p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w};
    float fz[3];
    qRotateVec3Host(rq, Z, fz);
    float d = fz[0] * toC[0] + fz[1] * toC[1] + fz[2] * toC[2];
    meanDot += d;
    if (d < minDot) minDot = d;
    aimed = aimed && (d > 0.999f);
    ++valid;
  }
  if (valid > 0) meanDot /= (float)valid;
  // TEETH 2: the skipped (degenerate) set is tiny — the op really oriented the cloud, it didn't
  // bail wholesale. TEETH 1: all valid points aim true AND the mean is ≈ 1.
  bool fewSkipped = skipped <= 4;
  bool pass = (captured.size() == N) && aimed && fewSkipped && valid > 0 && meanDot > 0.999f;
  printf("[selftest-orientpoints] n=%zu valid=%d skipped=%d minDot=%.4f meanDot=%.4f (need>0.999) -> %s\n",
         captured.size(), valid, skipped, minDot, meanDot, pass ? "PASS" : "FAIL");

  g_capOrient = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
