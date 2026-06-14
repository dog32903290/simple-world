// ReorientLinePoints — lane point_modify MODIFIER op: align point Rotation to the line tangent.
// Faithful port of external/tixl .../point/transform/ReorientLinePoints (.cs ports, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output): each live point's
// Rotation is qSlerp'd (by Amount) toward a quaternion whose +Z forward follows the local line
// tangent = dir(prevLiveNeighbour -> nextLiveNeighbour).  Count is INHERITED from upstream.
//
// TiXL parity (ReorientLinePoints.cs / .hlsl):
//   - ports: Points, Amount(float,1), Center(Vec3), UpVector(Vec3), WIsWeight(bool), Flip(bool).
//     Only Amount is read by main(); Center/UpVector/WIsWeight/Flip are dead in the kernel.
//   - math: per-index neighbour scan (skip dead NAN-Scale points), tangent dir, qAlignForward2,
//           qSlerp(p.Rotation, aligned, Amount).
//   - FORK (see reorientlinepoints.metal):
//       * dead/isolated/degenerate points COPY-THROUGH (separate src/dst bags; TiXL bare-return
//         leaves the live buffer untouched — same observable result).
//       * dropped dead ports Center/UpVector/WIsWeight/Flip (kernel ignores them).
//   - BAKED: none beyond the dropped dead ports.
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                   // calcDispatchCount
#include "runtime/graph.h"                      // Graph/Node/pinId
#include "runtime/point_graph.h"                // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/reorientlinepoints_params.h"  // ReorientLineParams, ReorientLineBinding
#include "runtime/tixl_point.h"                 // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ReorientLinePoints modifier: dispatch the reorientlinepoints kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookReorientLinePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to reorient

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("reorientlinepoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  ReorientLineParams P{};
  P.Count  = c.count;
  P.Amount = cookParam(c, "Amount", 1.0f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, REORIENTLINE_SourcePoints);
  enc->setBuffer(c.output, 0, REORIENTLINE_ResultPoints);
  enc->setBytes(&P, sizeof(P), REORIENTLINE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capReorient = nullptr;
void captureDrawReorient(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capReorient || !pts || c.count == 0) return;
  g_capReorient->assign(c.count, SwPoint{});
  std::memcpy(g_capReorient->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Direct single-dispatch runner for precise teeth (no graph plumbing).
bool runReorientKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                             const std::vector<SwPoint>& in, const ReorientLineParams& P,
                             std::vector<SwPoint>& out) {
  MTL::Function* fn = lib->newFunction(NS::String::string("reorientlinepoints", NS::UTF8StringEncoding));
  if (!fn) return false;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return false;
  const size_t bytes = in.size() * sizeof(SwPoint);
  MTL::Buffer* src = dev->newBuffer(in.data(), bytes, MTL::ResourceStorageModeShared);
  MTL::Buffer* dst = dev->newBuffer(bytes, MTL::ResourceStorageModeShared);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(src, 0, REORIENTLINE_SourcePoints);
  enc->setBuffer(dst, 0, REORIENTLINE_ResultPoints);
  enc->setBytes(&P, sizeof(P), REORIENTLINE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(P.Count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  out.assign(in.size(), SwPoint{});
  std::memcpy(out.data(), dst->contents(), bytes);
  src->release(); dst->release(); pso->release();
  return true;
}

// host qRotateVec3 (fast Rodrigues form, matches shared/quat.metal.h).
std::array<float, 3> hQRotate(std::array<float, 3> v, std::array<float, 4> q) {
  // t = 2*cross(q.xyz, v)
  float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  // v + q.w*t + cross(q.xyz, t)
  return {v[0] + q[3] * tx + (q[1] * tz - q[2] * ty),
          v[1] + q[3] * ty + (q[2] * tx - q[0] * tz),
          v[2] + q[3] * tz + (q[0] * ty - q[1] * tx)};
}

}  // namespace

void registerReorientLinePointsOp() {
  registerPointOp("ReorientLinePoints", cookReorientLinePoints);
}

// =============================================================================
// Golden: LinePoints(N=8, Direction=(0,1,0) default) -> ReorientLinePoints(Amount=1) -> capture.
//   The line runs along +Y, so each INTERIOR point's tangent = (0,1,0).  With Amount=1 the
//   rotation is fully aligned: qRotateVec3((0,0,1), outRot) ≈ (0,1,0) (the +Z forward now
//   points along the line tangent).
// TEETH:
//   (1) COUNT PRESERVED: output bag still has N points.
//   (2) TANGENT ALIGN (graph): for every interior point, the rotated +Z forward · tangent > 0.99.
//       The endpoints have only one live neighbour each (prev==index or next==index) but BOTH
//       neighbours along a contiguous line still differ -> endpoints DO align too (prevIndex !=
//       nextIndex). We assert all N points align.
//   (3) DIRECT SINGLE-POINT TOOTH: a 3-point line on +X with identity rotations.  The middle
//       point's tangent is (1,0,0); qRotateVec3((0,0,1), out[1].Rotation) ≈ (1,0,0).
//   (4) AMOUNT=0 PASSTHROUGH: Amount=0 leaves rotation == identity (qSlerp(id, aligned, 0) = id).
// injectBug: assert that the rotated forward points AWAY from the tangent (dot < -0.99).  The
//   correct shader aligns forward TO the tangent (dot ≈ +1), so the inverted assertion fails -> RED.
// =============================================================================
int runReorientLinePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 8;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-reorientlinepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerReorientLinePointsOp();
  std::vector<SwPoint> captured;
  g_capReorient = &captured;
  registerDrawOp("DrawPoints", captureDrawReorient);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "LinePoints";
  gen.params["Count"]  = (float)N;
  gen.params["Length"] = 5.0f;     // line spans +Y by 5 units
  g.nodes.push_back(gen);
  Node ro; ro.id = 2; ro.type = "ReorientLinePoints";
  ro.params["Amount"] = 1.0f;
  g.nodes.push_back(ro);
  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == N);
  // tangent of the +Y line is (0,1,0); rotated forward should align (dot ~ +1 normal, -1 bug).
  float minAlign = 9.9f;  // most-misaligned interior point's dot
  for (size_t k = 0; k < captured.size(); ++k) {
    const SwPoint& p = captured[k];
    std::array<float, 4> rq = {p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w};
    std::array<float, 3> fwd = hQRotate({0, 0, 1}, rq);  // +Z forward in world
    float dot = fwd[1];  // dot with tangent (0,1,0)
    if (dot < minAlign) minAlign = dot;
  }
  // injectBug flips the alignment test direction: require forward to OPPOSE the tangent.
  bool graphAlign = injectBug ? (minAlign < -0.99f) : (minAlign > 0.99f);
  bool graphPass = countOk && graphAlign;
  printf("[selftest-reorientlinepoints] graph N=%u minAlign(dot fwd·tangent)=%.4f -> %s%s\n",
         N, minAlign, graphPass ? "PASS" : "FAIL", injectBug ? " (bug-mode: expect FAIL)" : "");

  // --- TOOTH: direct 3-point line on +X; middle point's tangent = (1,0,0) ---
  bool directPass = false;
  float directDot = -9.9f;
  {
    std::vector<SwPoint> line(3);
    for (int j = 0; j < 3; ++j) {
      line[j] = SwPoint{};
      line[j].Position = SW_PACKED3{(float)j, 0.0f, 0.0f};  // along +X
      line[j].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f}; // identity
      line[j].Scale    = SW_PACKED3{1.0f, 1.0f, 1.0f};      // finite (alive)
    }
    ReorientLineParams RP{};
    RP.Count = 3; RP.Amount = 1.0f;
    std::vector<SwPoint> out;
    if (runReorientKernelDirect(dev, q, lib, line, RP, out) && out.size() == 3) {
      std::array<float, 4> rq = {out[1].Rotation.x, out[1].Rotation.y,
                                 out[1].Rotation.z, out[1].Rotation.w};
      std::array<float, 3> fwd = hQRotate({0, 0, 1}, rq);
      directDot = fwd[0];  // dot with tangent (1,0,0)
    }
    directPass = injectBug ? (directDot < -0.99f) : (directDot > 0.99f);
    printf("[selftest-reorientlinepoints] directTooth +X line mid fwd·tangent=%.4f -> %s\n",
           directDot, directPass ? "PASS" : "FAIL");
  }

  // --- TOOTH: Amount=0 passthrough (rotation stays identity) ---
  bool passThruPass = false;
  float identErr = 9.9f;
  {
    std::vector<SwPoint> line(3);
    for (int j = 0; j < 3; ++j) {
      line[j] = SwPoint{};
      line[j].Position = SW_PACKED3{(float)j, 0.0f, 0.0f};
      line[j].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity
      line[j].Scale    = SW_PACKED3{1.0f, 1.0f, 1.0f};
    }
    ReorientLineParams RP{};
    RP.Count = 3; RP.Amount = 0.0f;  // no blend -> identity preserved
    std::vector<SwPoint> out;
    if (runReorientKernelDirect(dev, q, lib, line, RP, out) && out.size() == 3) {
      // |dot(out.rot, identity)| ~ 1 means rotation unchanged.
      float dot = std::fabs(out[1].Rotation.w);  // identity = (0,0,0,1) -> w=1
      identErr = std::fabs(1.0f - dot);
    }
    // injectBug doesn't change this tooth's expectation; it stays a passthrough check.
    passThruPass = (identErr < 1e-3f);
    printf("[selftest-reorientlinepoints] passThruTooth Amount=0 |w|err=%.5f -> %s\n",
           identErr, passThruPass ? "PASS" : "FAIL");
  }

  bool pass = graphPass && directPass && passThruPass;
  printf("[selftest-reorientlinepoints] -> %s\n", pass ? "PASS" : "FAIL");

  g_capReorient = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
