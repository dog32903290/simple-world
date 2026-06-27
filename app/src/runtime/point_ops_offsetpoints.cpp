// OffsetPoints — batch 24 MODIFIER op (point_modify family): offset each point along
// Direction*Distance ROTATED BY THE POINT'S OWN Rotation. Faithful port of TiXL's _OffsetPoints.
//
// TiXL authority:
//   .cs : external/tixl/Operators/Lib/point/_internal/_OffsetPoints.cs (ports lines 10-17)
//   .hlsl: external/tixl/Operators/Lib/Assets/shaders/points/modify/OffsetPoints.hlsl (math lines 30-45)
//
// The cleanest count-preserving op — no simplification. Per point (.hlsl line 40):
//   Position += qRotateVec3(Direction * Distance, Point.Rotation)
//   Rotation / Color / Selected / W preserved (.hlsl 41-44). Op name drops TiXL's internal-namespace
//   leading underscore (_OffsetPoints is in Lib.point._internal); the node + slots are public.
//
// Rotation order: N/A — OffsetPoints builds NO new rotation. It rotates the offset vector by the
// point's EXISTING Rotation quaternion (qRotateVec3). No Euler/yaw-pitch-roll order question.
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
#include "runtime/graph.h"                // Graph/Node/pinId
#include "runtime/offsetpoints_params.h"  // OffsetPointsParams, OffsetPointsBinding
#include "runtime/point_graph.h"          // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tex_op_cache.h"         // cachedComputePSO
#include "runtime/tixl_point.h"           // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookOffsetPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "offsetpoints");
  if (!pso) return;

  OffsetPointsParams P{};
  P.Count = c.count;

  float dir[3] = {0.0f, 0.0f, 1.0f};  // .cs Direction (Vector3); UI default (0,0,1) — see params.h
  cookVecN(c, "Direction", dir, 3, dir);
  P.DirectionX = dir[0]; P.DirectionY = dir[1]; P.DirectionZ = dir[2];
  P.Distance = cookParam(c, "Distance", 0.0f);  // .cs Distance (float); 0 = identity no-op

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, OFFSETPOINTS_SourcePoints);
  enc->setBuffer(c.output,                         0, OFFSETPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P),                        OFFSETPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- C++ mirror of shared/quat.metal.h qRotateVec3 (fast Rodrigues), for the golden reference ---
// q = float4(x,y,z,w); v rotated by q. Byte-identical math to the .metal helper.
struct V3 { float x, y, z; };
static V3 cppQRotateVec3(V3 v, const float q[4]) {
  // t = 2 * cross(q.xyz, v)
  V3 qx{q[0], q[1], q[2]};
  V3 t{2.0f * (qx.y * v.z - qx.z * v.y),
       2.0f * (qx.z * v.x - qx.x * v.z),
       2.0f * (qx.x * v.y - qx.y * v.x)};
  // v + q.w*t + cross(q.xyz, t)
  V3 ct{qx.y * t.z - qx.z * t.y,
        qx.z * t.x - qx.x * t.z,
        qx.x * t.y - qx.y * t.x};
  return V3{v.x + q[3] * t.x + ct.x,
            v.y + q[3] * t.y + ct.y,
            v.z + q[3] * t.z + ct.z};
}

// Dispatch the offsetpoints kernel directly over a hand-built input bag (probe-style golden:
// arbitrary per-point Rotation so the qRotateVec3(offset, pointRot) path is genuinely exercised).
static bool runOffsetKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                  const std::vector<SwPoint>& in, const OffsetPointsParams& P,
                                  std::vector<SwPoint>& out) {
  MTL::Function* fn = lib->newFunction(NS::String::string("offsetpoints", NS::UTF8StringEncoding));
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
  enc->setBuffer(src, 0, OFFSETPOINTS_SourcePoints);
  enc->setBuffer(dst, 0, OFFSETPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), OFFSETPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(P.Count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  out.assign(in.size(), SwPoint{});
  std::memcpy(out.data(), dst->contents(), bytes);
  src->release(); dst->release(); pso->release();
  return true;
}

}  // namespace

void registerOffsetPointsOp() { registerPointOp("OffsetPoints", cookOffsetPoints); }

// Golden (hand-built probe): N points each with a DISTINCT non-identity Rotation quaternion +
// distinct Position/Color/Scale. Drive the REAL offsetpoints kernel with Direction=(1,0,0),
// Distance=D. Recompute the TiXL .hlsl formula in C++ (Position + qRotateVec3(Dir*Dist, Rot)) and
// compare per-point.
// TEETH:
//   (1) count PRESERVED (modifier: output count == input count).
//   (2) per-point Position matches the qRotateVec3 reference within 1e-4 (the rotation-of-offset
//       path is exercised because each point's Rotation is non-identity and distinct).
//   (3) the offset was ACTUALLY rotated: a "naive" (un-rotated) reference Position+Dir*Dist DIFFERS
//       from the GPU output for at least one point (proves qRotateVec3 is doing work, not a no-op).
//   (4) Rotation / Color / Scale preserved verbatim.
// injectBug: the C++ "expected" reference DROPS the rotation (offset = Dir*Distance, un-rotated) —
//   this is the real-logic-flip (offsetpoints.metal rotates; the bug reference doesn't) so the GPU
//   output no longer matches expected -> assertion (2) FAILS. (Mirrors "_OffsetPoints 漏旋轉".)
int runOffsetPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 24;
  const float DIST = 2.0f;
  const float DIR[3] = {1.0f, 0.0f, 0.0f};  // offset along +X in each point's LOCAL frame

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-offsetpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Build N points, each with a distinct non-identity Rotation (rotate angle a about a tilted axis)
  // so qRotateVec3(offset, Rotation) genuinely turns the +X offset into a per-point direction.
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double t = (double)i / (double)N;
    double ang = t * 6.2831853 + 0.3;          // distinct angle per point
    double s = std::sin(ang * 0.5), cw = std::cos(ang * 0.5);
    // tilted unit axis (not aligned to X) so the offset is rotated out of +X for every point
    double ax = 0.3, ay = 0.8, az = 0.5;
    double an = std::sqrt(ax * ax + ay * ay + az * az);
    ax /= an; ay /= an; az /= an;
    in[i] = SwPoint{};
    in[i].Position = {(float)(std::cos(t * 6.2831853) * 1.3),
                      (float)(std::sin(t * 6.2831853) * 0.8),
                      (float)(t * 0.5 - 0.25)};
    in[i].Rotation = {(float)(ax * s), (float)(ay * s), (float)(az * s), (float)cw};
    in[i].Color    = {(float)t, 0.25f, 0.75f, 1.0f};
    in[i].Scale    = {1.0f, 2.0f, 3.0f};
    in[i].FX1      = (float)i;
    in[i].FX2      = (float)(N - i);
  }

  OffsetPointsParams P{};
  P.Count = N;
  P.DirectionX = DIR[0]; P.DirectionY = DIR[1]; P.DirectionZ = DIR[2];
  P.Distance = DIST;

  std::vector<SwPoint> gpu;
  bool ran = runOffsetKernelDirect(dev, q, lib, in, P, gpu);

  bool countOk = ran && (gpu.size() == N);

  float maxPosErr = 0.0f;
  float maxNaiveDelta = 0.0f;   // how far the rotated result is from the un-rotated naive offset
  bool attrsPreserved = true;
  if (countOk) {
    for (uint32_t i = 0; i < N; ++i) {
      const float qrot[4] = {in[i].Rotation.x, in[i].Rotation.y, in[i].Rotation.z, in[i].Rotation.w};
      V3 offsetVec{DIR[0] * DIST, DIR[1] * DIST, DIR[2] * DIST};
      // EXPECTED reference. Correct path rotates the offset by the point's Rotation.
      // injectBug DROPS the rotation (naive un-rotated offset) -> mismatches the GPU -> FAIL.
      V3 rotated = cppQRotateVec3(offsetVec, qrot);
      V3 expected;
      if (injectBug) {
        expected = V3{in[i].Position.x + offsetVec.x,
                      in[i].Position.y + offsetVec.y,
                      in[i].Position.z + offsetVec.z};
      } else {
        expected = V3{in[i].Position.x + rotated.x,
                      in[i].Position.y + rotated.y,
                      in[i].Position.z + rotated.z};
      }
      float ex = std::fabs(gpu[i].Position.x - expected.x);
      float ey = std::fabs(gpu[i].Position.y - expected.y);
      float ez = std::fabs(gpu[i].Position.z - expected.z);
      float e = std::max(ex, std::max(ey, ez));
      if (e > maxPosErr) maxPosErr = e;

      // naive (un-rotated) vs actual GPU: prove rotation actually did work for at least one point
      V3 naive{in[i].Position.x + offsetVec.x,
               in[i].Position.y + offsetVec.y,
               in[i].Position.z + offsetVec.z};
      float nd = std::max({std::fabs(gpu[i].Position.x - naive.x),
                           std::fabs(gpu[i].Position.y - naive.y),
                           std::fabs(gpu[i].Position.z - naive.z)});
      if (nd > maxNaiveDelta) maxNaiveDelta = nd;

      // attrs preserved verbatim
      if (std::fabs(gpu[i].Rotation.x - in[i].Rotation.x) > 1e-5f ||
          std::fabs(gpu[i].Rotation.y - in[i].Rotation.y) > 1e-5f ||
          std::fabs(gpu[i].Rotation.z - in[i].Rotation.z) > 1e-5f ||
          std::fabs(gpu[i].Rotation.w - in[i].Rotation.w) > 1e-5f) attrsPreserved = false;
      if (std::fabs(gpu[i].Color.x - in[i].Color.x) > 1e-5f ||
          std::fabs(gpu[i].Color.w - in[i].Color.w) > 1e-5f) attrsPreserved = false;
      if (std::fabs(gpu[i].Scale.y - in[i].Scale.y) > 1e-5f) attrsPreserved = false;
    }
  }

  bool posMatches    = countOk && (maxPosErr < 1e-4f);
  bool rotationWorked = countOk && (maxNaiveDelta > 0.1f);  // offset truly rotated for some point
  bool pass = countOk && posMatches && rotationWorked && attrsPreserved;

  printf("[selftest-offsetpoints] n=%zu maxPosErr=%.6f(need<1e-4) naiveDelta=%.4f(need>0.1) "
         "attrsPreserved=%s -> %s\n",
         gpu.size(), maxPosErr, maxNaiveDelta, attrsPreserved ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
