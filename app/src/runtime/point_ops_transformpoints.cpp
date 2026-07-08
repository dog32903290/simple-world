// TransformPoints — PARITY ORACLE (the flat atom RETIRED 2026-07-08, 廢棄節點退場 pilot).
//
// The flat TransformPoints modifier ATOM (cook fn + NodeSpec + its own --selftest-transformpoints) is
// GONE: its behaviour is now the nested .t3 compound (assets/catalog_t3/TransformPoints.t3). What STAYS
// here is the compute-parity ORACLE — runTransformPointsParityProbe (--selftest-xfprobe), the焊死
// host-matrix + fused-kernel reference that BOTH surviving goldens verify the .t3 replay against:
//   • t3import_transformpoints_golden.cpp (--selftest-t3-transformpoints) — line "oracle self-check".
//   • t3import_nestedcompound_golden.cpp (--selftest-t3-nestedcompound) — same oracle self-check.
// The oracle dispatches the "transformpoints" MSL kernel directly (runXfKernelDirect) and compares its
// GPU output to the .NET TransformMatrix host-matrix recomputed in C++ (independent of the shader's qMul
// shortcut). Retiring the atom does NOT touch this kernel, transformpoints_params.h, or the oracle math —
// so the two .t3 goldens keep their independent reference. See docs/agent/RETIREMENT_BATTLE_SPEC.md §7.
//
// ZONE: runtime (GPU oracle probe). No NodeSpec, no cook binding, no findSpec entry — pure verification.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"               // calcDispatchCount
#include "runtime/point_ops.h"              // runTransformPointsParityProbe decl
#include "runtime/tex_op_cache.h"           // clearTexOpCache (drop stale PSO before teardown)
#include "runtime/transformpoints_params.h" // TransformParams, TransformBinding
#include "runtime/tixl_point.h"             // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ============================================================================
// refuter-T GPU adversarial probe (batch 17, permanent bite tooth): falsifies the
// 承重斷言 that transformpoints.metal composes its Euler rotation in the SAME order
// as TiXL's host TransformMatrix (CreateFromYawPitchRoll(yaw=Rotation.Y, pitch=
// Rotation.X, roll=Rotation.Z), render/_/TransformMatrix.cs:30-39). Drives the REAL
// transformpoints kernel directly (hand-built input bag with NON-identity Position
// AND Rotation per point) with MULTI-AXIS non-equal Rotation + NON-UNIFORM Scale,
// captures GPU Position+Rotation, and compares each against the TiXL host-matrix
// reference recomputed in C++ (independent of the shader's qMul shortcut — uses the
// .NET matrix path verbatim). Tests Pivot=0 (rotation isolated) for BOTH PointSpace
// and ObjectSpace, then Pivot!=0 as a SEPARATE diagnostic (reported, NOT gated —
// see note below).
//   injectBug=true swaps yaw/roll in the C++ reference (Z as yaw, X as roll = the old
//   Z·Y·X shader bug) -> mismatches the correct Y·X·Z shader -> FAIL (bite).
//   RED face: Z·Y·X (old shader) -> maxRotErr/maxPosErr >> 1e-3.
//   GREEN face: Y·X·Z (fixed shader) -> both errors < threshold for both spaces.
// ============================================================================
namespace {

struct TV3 { double x, y, z; };
struct TV4 { double x, y, z, w; };
struct TM4 { double m[4][4]; };

// .NET Quaternion.CreateFromYawPitchRoll (verbatim from dotnet/runtime Quaternion.cs)
static TV4 tCfypr(double yaw, double pitch, double roll) {
  double sr = std::sin(roll * 0.5),  cr = std::cos(roll * 0.5);
  double sp = std::sin(pitch * 0.5), cp = std::cos(pitch * 0.5);
  double sy = std::sin(yaw * 0.5),   cy = std::cos(yaw * 0.5);
  return { cy*sp*cr + sy*cp*sr,
           sy*cp*cr - cy*sp*sr,
           cy*cp*sr - sy*sp*cr,
           cy*cp*cr + sy*sp*sr };
}
// Hamilton product q1*q2, float4(x,y,z,w) convention (matches quat.metal.h qMul).
static TV4 tqMul(TV4 a, TV4 b) {
  return { b.x*a.w + a.x*b.w + (a.y*b.z - a.z*b.y),
           b.y*a.w + a.y*b.w + (a.z*b.x - a.x*b.z),
           b.z*a.w + a.z*b.w + (a.x*b.y - a.y*b.x),
           a.w*b.w - (a.x*b.x + a.y*b.y + a.z*b.z) };
}
static TV3 tqRotate(TV3 v, TV4 q) {  // fast Rodrigues (== qRotateVec3)
  TV3 t = { 2.0*(q.y*v.z - q.z*v.y), 2.0*(q.z*v.x - q.x*v.z), 2.0*(q.x*v.y - q.y*v.x) };
  return { v.x + q.w*t.x + (q.y*t.z - q.z*t.y),
           v.y + q.w*t.y + (q.z*t.x - q.x*t.z),
           v.z + q.w*t.z + (q.x*t.y - q.y*t.x) };
}
static double tDot4(TV4 a, TV4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
// .NET Matrix4x4.CreateFromQuaternion (row-major, row-vector convention)
static TM4 tCfq(TV4 q) {
  double xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z, xy=q.x*q.y, wz=q.z*q.w,
         xz=q.z*q.x, wy=q.y*q.w, yz=q.y*q.z, wx=q.x*q.w;
  TM4 r{};
  r.m[0][0]=1-2*(yy+zz); r.m[0][1]=2*(xy+wz);   r.m[0][2]=2*(xz-wy);
  r.m[1][0]=2*(xy-wz);   r.m[1][1]=1-2*(zz+xx); r.m[1][2]=2*(yz+wx);
  r.m[2][0]=2*(xz+wy);   r.m[2][1]=2*(yz-wx);   r.m[2][2]=1-2*(yy+xx);
  r.m[3][3]=1;
  return r;
}
static TM4 tScale(double sx, double sy, double sz) {
  TM4 r{}; r.m[0][0]=sx; r.m[1][1]=sy; r.m[2][2]=sz; r.m[3][3]=1; return r;
}
static TM4 tTrans(double tx, double ty, double tz) {
  TM4 r{}; for(int i=0;i<4;i++) r.m[i][i]=1;
  r.m[3][0]=tx; r.m[3][1]=ty; r.m[3][2]=tz; return r;
}
static TM4 tMatMul(const TM4& a, const TM4& b) {
  TM4 r{};
  for (int i=0;i<4;i++) for (int j=0;j<4;j++) {
    double s=0; for(int k=0;k<4;k++) s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s;
  }
  return r;
}
// row-vector (v,1)*M, drop w  (== HLSL mul(float4(pos,1), M))
static TV3 tXform(TV3 v, const TM4& m) {
  return { v.x*m.m[0][0]+v.y*m.m[1][0]+v.z*m.m[2][0]+m.m[3][0],
           v.x*m.m[0][1]+v.y*m.m[1][1]+v.z*m.m[2][1]+m.m[3][1],
           v.x*m.m[0][2]+v.y*m.m[1][2]+v.z*m.m[2][2]+m.m[3][2] };
}

// Dispatches the transformpoints kernel directly over a hand-built input bag, returning the GPU
// output (Position+Rotation). Independent of the graph plumbing so the probe sets arbitrary orgRot.
static bool runXfKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                              const std::vector<SwPoint>& in, const TransformParams& P,
                              std::vector<SwPoint>& out) {
  MTL::Function* fn = lib->newFunction(NS::String::string("transformpoints", NS::UTF8StringEncoding));
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
  enc->setBuffer(src, 0, TRANSFORM_SourcePoints);
  enc->setBuffer(dst, 0, TRANSFORM_ResultPoints);
  enc->setBytes(&P, sizeof(P), TRANSFORM_Params);
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

}  // namespace (probe helpers)

int runTransformPointsParityProbe(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 24;
  // Adversarial: three NON-EQUAL non-zero Euler angles + NON-UNIFORM Scale + non-zero Translation.
  const float ROT[3] = {37.0f, 53.0f, 71.0f};   // RotationX=pitch, RotationY=yaw, RotationZ=roll
  const float SCL[3] = {1.7f, 0.6f, 2.3f};       // non-uniform Stretch (Scale uniform = 1)
  const float TRN[3] = {0.4f, -0.3f, 0.9f};
  const double DEG = M_PI / 180.0;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[xfprobe] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Build an input bag with NON-identity Position AND a NON-identity per-point Rotation, so the
  // newRot composition qMul(R,orgRot)/qMul(orgRot,R) is genuinely exercised (not orgRot=identity).
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwPoint{};
    in[i].Position = { (float)(std::cos(a * 6.2831853) * 1.3),
                       (float)(std::sin(a * 6.2831853) * 0.8),
                       (float)((a - 0.5) * 1.5) };
    // per-point orgRot: a yaw/pitch/roll that varies with i (kept non-degenerate, normalized).
    TV4 oq = tCfypr((20.0 + 90.0 * a) * DEG, (-15.0 + 40.0 * a) * DEG, (10.0 + 60.0 * a) * DEG);
    in[i].Rotation = SW_FLOAT4{ (float)oq.x, (float)oq.y, (float)oq.z, (float)oq.w };
  }

  // The correct R (or, with injectBug, the BUGGY Z·Y·X reference that mismatches the fixed shader).
  TV4 Rt = injectBug
    ? tCfypr(ROT[2]*DEG, ROT[0]*DEG, ROT[1]*DEG)   // BUGGY: yaw=Z, pitch=X, roll=Y  (simulates Z·Y·X)
    : tCfypr(ROT[1]*DEG, ROT[0]*DEG, ROT[2]*DEG);  // correct: yaw=Y, pitch=X, roll=Z

  // Two gated cases (Pivot=0): PointSpace and ObjectSpace.
  struct Case { int space; const char* name; };
  const Case cases[2] = { {0, "PointSpace"}, {1, "ObjectSpace"} };
  double gatedMaxPos = 0.0, gatedMaxRot = 0.0;
  for (const Case& cs : cases) {
    TransformParams P{};
    P.Count = N; P.Space = cs.space;
    P.RotationX = ROT[0]; P.RotationY = ROT[1]; P.RotationZ = ROT[2];
    P.StretchX = SCL[0]; P.StretchY = SCL[1]; P.StretchZ = SCL[2]; P.Scale = 1.0f;
    P.TranslationX = TRN[0]; P.TranslationY = TRN[1]; P.TranslationZ = TRN[2];
    P.PivotX = 0.0f; P.PivotY = 0.0f; P.PivotZ = 0.0f; P.Strength = 1.0f;

    std::vector<SwPoint> out;
    if (!runXfKernelDirect(dev, q, lib, in, P, out)) {
      printf("[xfprobe] FAIL: kernel dispatch (%s)\n", cs.name);
      lib->release(); q->release(); dev->release(); pool->release();
      return 1;
    }

    // TiXL host-matrix reference (pivot=0): M = Scale * CreateFromQuaternion(R) * Translation.
    TM4 M = tMatMul(tMatMul(tScale(SCL[0], SCL[1], SCL[2]), tCfq(Rt)),
                    tTrans(TRN[0], TRN[1], TRN[2]));
    double maxPos = 0.0, maxRot = 0.0; int worstI = -1;
    TV3 wExp{}, wGot{};
    for (uint32_t i = 0; i < N; ++i) {
      TV3 op = { in[i].Position.x, in[i].Position.y, in[i].Position.z };
      TV4 oq = { in[i].Rotation.x, in[i].Rotation.y, in[i].Rotation.z, in[i].Rotation.w };
      TV3 expPos; TV4 expRot;
      if (cs.space == 0) {  // PointSpace: offset=trans (pivot=0), newPos=qRotate(offset,orgRot)+orgPos
        TV3 offset = { TRN[0], TRN[1], TRN[2] };
        TV3 rp = tqRotate(offset, oq);
        expPos = { rp.x + op.x, rp.y + op.y, rp.z + op.z };
        expRot = tqMul(oq, Rt);          // newRot = qMul(orgRot, R)
      } else {                            // ObjectSpace: newPos=mul((orgPos,1),M); newRot=qMul(R,orgRot)
        expPos = tXform(op, M);
        expRot = tqMul(Rt, oq);
      }
      const SwPoint& gp = out[i];
      TV3 gotPos = { gp.Position.x, gp.Position.y, gp.Position.z };
      TV4 gotRot = { gp.Rotation.x, gp.Rotation.y, gp.Rotation.z, gp.Rotation.w };
      double ep = std::sqrt((expPos.x-gotPos.x)*(expPos.x-gotPos.x) +
                            (expPos.y-gotPos.y)*(expPos.y-gotPos.y) +
                            (expPos.z-gotPos.z)*(expPos.z-gotPos.z));
      double er = 1.0 - std::fabs(tDot4(expRot, gotRot));  // double-cover-safe quat distance
      if (ep > maxPos) { maxPos = ep; worstI = (int)i; wExp = expPos; wGot = gotPos; }
      if (er > maxRot) maxRot = er;
    }
    printf("[xfprobe] %s n=%u maxPosErr=%.6f(need<1e-3) maxRotErr=%.6f(need<1e-4) worstI=%d "
           "exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
           cs.name, N, maxPos, maxRot, worstI, wExp.x, wExp.y, wExp.z, wGot.x, wGot.y, wGot.z);
    if (maxPos > gatedMaxPos) gatedMaxPos = maxPos;
    if (maxRot > gatedMaxRot) gatedMaxRot = maxRot;
  }

  // --- Pivot!=0 DIAGNOSTIC (NOT gated — reported only, ObjectSpace). Compares the shader against
  // the FULL TiXL host matrix M = CreateTransformationMatrix(scalingCenter=pivot,
  // scalingRotation=Identity, scaling=s, rotationCenter=pivot, rotation=R, translation=t)
  // (GraphicsMath.cs:56-95). For scalingRotation=Identity & scalingCenter==rotationCenter==pivot the
  // cancelling T(+pivot)·T(-pivot) collapses M to: v' = ((v-pivot)*scale)*R + pivot + translation —
  // EXACTLY the shader's qRotate((v-pivot)*scale,R)+pivot+trans. So we expect ~0 divergence (the
  // orchestrator flagged pivot!=0 as a possible EXTRA divergence; this measures it directly). If a
  // future TiXL build sets scalingRotation!=Identity this number would grow -> a real fork to chase. ---
  {
    const float PV[3] = {0.5f, -0.4f, 0.3f};
    TransformParams P{};
    P.Count = N; P.Space = 1;  // ObjectSpace
    P.RotationX = ROT[0]; P.RotationY = ROT[1]; P.RotationZ = ROT[2];
    P.StretchX = SCL[0]; P.StretchY = SCL[1]; P.StretchZ = SCL[2]; P.Scale = 1.0f;
    P.TranslationX = TRN[0]; P.TranslationY = TRN[1]; P.TranslationZ = TRN[2];
    P.PivotX = PV[0]; P.PivotY = PV[1]; P.PivotZ = PV[2]; P.Strength = 1.0f;
    std::vector<SwPoint> out;
    double pivPos = -1.0;
    if (runXfKernelDirect(dev, q, lib, in, P, out)) {
      // Build the FULL host matrix (row-vector chain, scalingRotation=Identity):
      //   M = T(-piv) · Scale · T(+piv) · T(-piv) · R · T(+piv) · T(trans)
      TV4 Rok = tCfypr(ROT[1]*DEG, ROT[0]*DEG, ROT[2]*DEG);  // correct R (Euler order proven above)
      TM4 M = tMatMul(tTrans(-PV[0], -PV[1], -PV[2]), tScale(SCL[0], SCL[1], SCL[2]));
      M = tMatMul(M, tTrans(PV[0], PV[1], PV[2]));
      M = tMatMul(M, tTrans(-PV[0], -PV[1], -PV[2]));
      M = tMatMul(M, tCfq(Rok));
      M = tMatMul(M, tTrans(PV[0], PV[1], PV[2]));
      M = tMatMul(M, tTrans(TRN[0], TRN[1], TRN[2]));
      pivPos = 0.0;
      for (uint32_t i = 0; i < N; ++i) {
        TV3 op = { in[i].Position.x, in[i].Position.y, in[i].Position.z };
        TV3 expPos = tXform(op, M);   // host-matrix reference (NOT the shader's own formula)
        const SwPoint& gp = out[i];
        double ep = std::sqrt((expPos.x-gp.Position.x)*(expPos.x-gp.Position.x) +
                              (expPos.y-gp.Position.y)*(expPos.y-gp.Position.y) +
                              (expPos.z-gp.Position.z)*(expPos.z-gp.Position.z));
        if (ep > pivPos) pivPos = ep;
      }
    }
    printf("[xfprobe] DIAGNOSTIC Pivot!=0 (NOT gated, ObjectSpace): shader-vs-HOST-MATRIX maxPosErr=%.6f "
           "(~0 expected: shader pivot composition == TiXL CreateTransformationMatrix; no extra fork)\n",
           pivPos);
  }

  bool pass = (gatedMaxPos < 1e-3) && (gatedMaxRot < 1e-4);
  printf("[xfprobe] params: Rot(deg)=(%.0f,%.0f,%.0f) Stretch=(%.2f,%.2f,%.2f) Trans=(%.2f,%.2f,%.2f)"
         " orgRot=non-identity\n", ROT[0], ROT[1], ROT[2], SCL[0], SCL[1], SCL[2], TRN[0], TRN[1], TRN[2]);
  printf("[xfprobe] GATED(Pivot=0): maxPosErr=%.6f maxRotErr=%.6f -> %s "
         "(PASS=shader Euler order matches TiXL host matrix Y·X·Z; FAIL=承重斷言 BROKEN)\n",
         gatedMaxPos, gatedMaxRot, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
