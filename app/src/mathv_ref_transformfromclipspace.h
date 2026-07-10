#pragma once
// mathv_ref_transformfromclipspace — CPU scalar oracle for the TransformFromClipSpace kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/TransformPointsFromClipspace.hlsl (NOTE: the file on
// disk is named "TransformPointsFromClipspace", lowercase trailing "space"; the TiXL op itself is
// "TransformFromClipSpace" -- same file, just flagging the name mismatch for anyone grepping) —
// NOT derived from sw shaders.
//   cbuffer Params      :4-7   (declared, but EMPTY -- this op authors zero scalar parameters)
//   cbuffer Transforms  :9-21  (10 float4x4; only CameraToWorld :14 is ever read, at :39/:47-49)
//   main() body         :27-56 -> mathvRefTransformFromClipSpace() / transformFromClipSpaceOne()
//                                  below, every statement carries its own :NN back-reference.
// Quaternion helpers transcribed from external/tixl/Operators/Lib/Assets/shaders/shared/
// quat-functions.hlsl (pulled in via this shader's own :2 include): qMul :31-36,
// qFromMatrix3Precise :263-303. Both are re-derived locally (not shared with
// mathv_ref_addnoise.h's copy of qFromMatrix3Precise) — no `mathv_ref_shared_quat.h` exists yet
// per MATH_VERIFY_WORKFLOW.md §1.4's "future" note; each ref currently carries its own copy.
//
// PROVENANCE discipline (same as mathv_ref_wrappointposition.h / mathv_ref_addnoise.h /
// GOLDEN_STANDARD.md "P5-safe oracle 判準"): transcribed straight from the TiXL HLSL text above,
// never from any sw port of this op. No sw file under app/shaders/ was opened while writing this
// header (out of scope for the R role's isolation — the whole point of mathv is to fuzz this CPU
// ref against an *independent* MSL implementation later; peeking at it here would self-certify a
// shared bug instead of catching one). Zero metal include, zero app/shaders/ reference, zero sw
// math helper.
//
// FLOAT (not double) on purpose (MATH_VERIFY_WORKFLOW.md §2.3 branchy-class rule): the
// qFromMatrix3Precise branch ladder (tr>0 / m00>m11&&m00>m22 / m11>m22 / else, :267/:276/:285) is
// a float comparison on the GPU; a double CPU ref could pick a different branch than the float GPU
// for a matrix sitting near a branch boundary (e.g. two near-equal diagonal entries), which would
// look like a kernel bug that is actually an oracle-precision bug.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// app/src/runtime/tixl_point.h is included, for the SwPoint host struct (LegacyPoint layout:
// Position=@0/W=FX1@12/Rotation@16/Color@32/Stretch=Scale@48/Selected=FX2@60, point.hlsl :9-17).
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0) (:4-7) -- declared empty in the HLSL text; this op takes no
//   authored scalar/vector parameters at all (a GPU fuzz driver still binds the (empty) buffer).
// cbuffer Transforms : register(b1) (:9-21) -- 10 float4x4 matrices, one 64B slot each (16 floats
//   per matrix, row-major storage matching HLSL's `_mRowCol` accessor). Row-vector convention
//   (v' = v * M, translation lives in row 3) is the same one already pinned by
//   mathv_ref_wrappointposition.h's CameraToWorld._m30/_m31/_m32-as-translation-row citation and
//   by the workflow doc's §9 trap-list note "mul(m,v) 順序/row-vs-col-major（TransformPoints host
//   矩陣 v·M）":
//     offset   0  CameraToClipSpace   (:11, UNUSED by this kernel)
//     offset  64  ClipSpaceToCamera   (:12, UNUSED)
//     offset 128  WorldToCamera       (:13, UNUSED)
//     offset 192  CameraToWorld       (:14, the ONLY matrix this kernel reads, :39/:47-49)
//     offset 256  WorldToClipSpace    (:15, UNUSED)
//     offset 320  ClipSpaceToWorld    (:16, UNUSED)
//     offset 384  ObjectToWorld       (:17, UNUSED)
//     offset 448  WorldToObject       (:18, UNUSED)
//     offset 512  ObjectToCamera      (:19, UNUSED)
//     offset 576  ObjectToClipSpace   (:20, UNUSED)
//   Only CameraToWorld is modeled below -- the other 9 are real cbuffer members a GPU fuzz driver
//   must still size/bind (full 640B Transforms buffer), but main() never reads them, so they
//   carry no math for this oracle to represent.
struct Mat4 {
  float m00, m01, m02, m03;
  float m10, m11, m12, m13;
  float m20, m21, m22, m23;
  float m30, m31, m32, m33;  // row 3 == translation, under the row-vector convention above.
};

struct TransformFromClipSpaceParams {
  Mat4 cameraToWorld;  // :14 -- the ONLY Transforms-cbuffer matrix this kernel reads.
};

namespace detail {

struct Vec4 { float x, y, z, w; };
struct Quat { float x, y, z, w; };
// 3x3, row-major, matching HLSL's float3x3 `_mRowCol` accessor (same naming as
// mathv_ref_addnoise.h's Mat3, independently re-derived here per the no-shared-header note above).
struct Mat3 { float m00, m01, m02, m10, m11, m12, m20, m21, m22; };

// HLSL `mul(vector, matrix)` (:39 call site) -- row-vector convention (v' = v * M): result[j] =
// sum_i v[i]*M[i][j]. See the cbuffer-table comment above for the provenance of this convention.
inline Vec4 mulPointByMat4(Vec4 v, const Mat4& M) {
  return {
      v.x * M.m00 + v.y * M.m10 + v.z * M.m20 + v.w * M.m30,
      v.x * M.m01 + v.y * M.m11 + v.z * M.m21 + v.w * M.m31,
      v.x * M.m02 + v.y * M.m12 + v.z * M.m22 + v.w * M.m32,
      v.x * M.m03 + v.y * M.m13 + v.z * M.m23 + v.w * M.m33,
  };
}

// qMul -- quat-functions.hlsl :31-36, transcribed verbatim (Hamilton product, xyzw layout, q1*q2).
//   return float4(q2.xyz*q1.w + q1.xyz*q2.w + cross(q1.xyz,q2.xyz), q1.w*q2.w - dot(q1.xyz,q2.xyz));
inline Quat qMul(Quat q1, Quat q2) {
  const float cx = q1.y * q2.z - q1.z * q2.y;
  const float cy = q1.z * q2.x - q1.x * q2.z;
  const float cz = q1.x * q2.y - q1.y * q2.x;
  return {
      q2.x * q1.w + q1.x * q2.w + cx,
      q2.y * q1.w + q1.y * q2.w + cy,
      q2.z * q1.w + q1.z * q2.w + cz,
      q1.w * q2.w - (q1.x * q2.x + q1.y * q2.y + q1.z * q2.z),
  };
}

// qFromMatrix3Precise -- quat-functions.hlsl :263-303 (Shepperd's method: branch-selects the
// numerically-largest diagonal term to avoid dividing by a near-zero S).
inline Quat qFromMatrix3Precise(const Mat3& m) {
  const float tr = m.m00 + m.m11 + m.m22;                                // :265
  if (tr > 0.0f) {                                                       // :267
    const float S = std::sqrt(tr + 1.0f) * 2.0f;                         // :269 S=4*qw
    return {(m.m21 - m.m12) / S, (m.m02 - m.m20) / S, (m.m10 - m.m01) / S, 0.25f * S};  // :270-274
  } else if (m.m00 > m.m11 && m.m00 > m.m22) {                           // :276
    const float S = std::sqrt(1.0f + m.m00 - m.m11 - m.m22) * 2.0f;      // :278 S=4*qx
    return {0.25f * S, (m.m01 + m.m10) / S, (m.m02 + m.m20) / S, (m.m21 - m.m12) / S};  // :279-283
  } else if (m.m11 > m.m22) {                                            // :285
    const float S = std::sqrt(1.0f + m.m11 - m.m00 - m.m22) * 2.0f;      // :287 S=4*qy
    return {(m.m01 + m.m10) / S, 0.25f * S, (m.m12 + m.m21) / S, (m.m02 - m.m20) / S};  // :288-292
  } else {                                                                // :294
    const float S = std::sqrt(1.0f + m.m22 - m.m00 - m.m11) * 2.0f;      // :296 S=4*qz
    return {(m.m02 + m.m20) / S, (m.m12 + m.m21) / S, 0.25f * S, (m.m10 - m.m01) / S};  // :297-301
  }
}

// HLSL builtin `normalize()` on a float4 (:51 call site). No zero-length guard, matching the GPU:
// normalize(0,0,0,0) is dot==0 -> sqrt(0)==0 -> 0/0 == NaN component-wise (MATH_VERIFY_WORKFLOW.md
// §9 "normalize(0)/rsqrt 精度").
inline Quat hlslNormalizeQuat(Quat q) {
  const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return {q.x / len, q.y / len, q.z / len, q.w / len};
}

}  // namespace detail

// transformFromClipSpaceOne — one thread of `main()` (:27-56), operating on a single point.
//
// `idx` / `numStructs` reproduce the dispatch's `i.x` / `GetDimensions(numStructs, stride)`
// (:30-31) so the `idx >= numStructs` guard (:32-35) can be exercised verbatim, same convention as
// mathv_ref_wrappointposition.h's wrapPointPositionOne().
inline void transformFromClipSpaceOne(const SwPoint& in, SwPoint& out, uint32_t idx,
                                       uint32_t numStructs,
                                       const TransformFromClipSpaceParams& prm) {
  // :30-35 -- GetDimensions guard: `if (i.x >= numStructs) return;` with NO write at all here
  // (unlike mathv_ref_wrappointposition.h's guard, which still wrote a NaN-tagged point on that
  // path). On the GPU this leaves ResultPoints[i.x] holding whatever was already in the backing
  // buffer -- SourcePoints[i.x] is never even read. AMBIGUITY (same class as
  // mathv_ref_wrappointposition.h's :48-52 note): a host CPU ref over an exactly-`count`-sized
  // array has no "leftover buffer contents" to reproduce, so this branch is unreachable through
  // mathvRefTransformFromClipSpace()'s loop below (idx always < numStructs == count there);
  // documented here for fidelity only. Modeled as a straight pass-through (closest host analogue
  // of "never touched").
  if (idx >= numStructs) {
    out = in;
    return;
  }

  out = in;  // :37 LegacyPoint p = SourcePoints[i.x]; -- start from a full copy. Only
             // Position (:39-42) and Rotation (:46-53) are overwritten below; W(FX1)/Color/
             // Scale(Stretch)/FX2(Selected) pass through untouched -- unlike WrapPointPosition or
             // AddNoise, this kernel never writes W at all.

  // :39 -- float4 pInClipSpace = mul(float4(p.Position,1), CameraToWorld);
  // NAMED QUIRK (flag for S-audit): the local variable is named `pInClipSpace` and the op itself
  // is "TransformFromClipSpace", but the matrix multiplied here is literally the register the
  // HLSL text calls `CameraToWorld` (:14/:39) -- not ClipSpaceToWorld or ClipSpaceToCamera (both
  // declared at :12/:16 but never read by this kernel). This oracle is pinned to the HLSL text as
  // written: whatever matrix the host op actually binds into the `CameraToWorld` cbuffer slot for
  // THIS op is what gets multiplied here; this ref does not assume it is geometrically a
  // camera-to-world matrix. Cannot be resolved further without opening the TiXL C# op, which is
  // out of scope for the R role's isolation on this ticket.
  const detail::Vec4 posIn{in.Position.x, in.Position.y, in.Position.z, 1.0f};
  const detail::Vec4 clip = detail::mulPointByMat4(posIn, prm.cameraToWorld);

  // :40-41 -- pInClipSpace.xyz /= pInClipSpace.w; pInClipSpace.w = 1;
  // EDGE (flagged per ticket): transcribed verbatim, no guard against w==0. IEEE-754 division
  // semantics apply per component: nonzero/0 -> signed Inf, 0/0 -> NaN. Both are exercised by
  // self-check case 4 below (a single degenerate matrix produces one Inf axis and two NaN axes
  // simultaneously). `clip.w` is then overwritten to 1 by :41, but that value is never read again
  // -- the very next statement (:42) assigns into a float3 field, discarding w entirely.
  const float w = clip.w;
  const float px = clip.x / w;
  const float py = clip.y / w;
  const float pz = clip.z / w;

  // :42 -- p.Position = pInClipSpace;  (float4 -> float3 implicit truncation; drops the
  // just-forced w=1 from :41, which is therefore dead by the time it would matter).
  out.Position.x = px;
  out.Position.y = py;
  out.Position.z = pz;

  // :46-49 -- float3x3 orientationDest = float3x3(CameraToWorld._m00_m01_m02,
  //                                                CameraToWorld._m10_m11_m12,
  //                                                CameraToWorld._m20_m21_m22);
  // HLSL's float3x3(rowVec0, rowVec1, rowVec2) constructor from float3 arguments fills BY ROW, so
  // orientationDest is exactly the upper-left 3x3 submatrix of CameraToWorld, copied verbatim.
  //
  // :51 -- ...qFromMatrix3Precise(transpose(orientationDest))...
  // NOTED-QUIRK (transcribed as an algebraic simplification, verified by hand -- same style as
  // mathv_ref_addnoise.h's getTranslationAndRotation() ex/ey/ez-transpose note): since
  // orientationDest.m[r][c] == CameraToWorld.m[r][c] for r,c in 0..2, transpose(orientationDest)
  // .m[r][c] == orientationDest.m[c][r] == CameraToWorld.m[c][r]. This oracle writes those 9
  // transposed entries directly instead of modeling "extract submatrix, then transpose" as two
  // separate steps -- the result is identical and sidesteps needing a general 3x3 transpose().
  const detail::Mat3 transposedOrientation{
      prm.cameraToWorld.m00, prm.cameraToWorld.m10, prm.cameraToWorld.m20,
      prm.cameraToWorld.m01, prm.cameraToWorld.m11, prm.cameraToWorld.m21,
      prm.cameraToWorld.m02, prm.cameraToWorld.m12, prm.cameraToWorld.m22,
  };

  // :51 -- float4 newRotation = normalize(qFromMatrix3Precise(transpose(orientationDest)));
  const detail::Quat qFromCam = detail::qFromMatrix3Precise(transposedOrientation);
  const detail::Quat qCamNormalized = detail::hlslNormalizeQuat(qFromCam);

  // :52 -- newRotation = qMul(newRotation, p.Rotation);   (q1=qCamNormalized, q2=point's rotation)
  const detail::Quat pRotIn{in.Rotation.x, in.Rotation.y, in.Rotation.z, in.Rotation.w};
  const detail::Quat qResult = detail::qMul(qCamNormalized, pRotIn);

  // :53 -- p.Rotation = newRotation;
  out.Rotation = SW_FLOAT4{qResult.x, qResult.y, qResult.z, qResult.w};

  // :55 -- ResultPoints[i.x] = p;  (Color/Scale/FX2 already carried over via the `out = in;` copy
  // above; FX1(W) likewise -- this kernel has no statement that ever assigns to p.W.)
}

// mathvRefTransformFromClipSpace — full-buffer CPU oracle (:27-56 dispatch loop over `count`
// points). `in` and `out` may alias the same array (the loop reads `in[i]` before writing
// `out[i]`, safe even if `in == out`), mirroring mathv_ref_wrappointposition.h's convention.
inline void mathvRefTransformFromClipSpace(const SwPoint* in, SwPoint* out, size_t count,
                                            const TransformFromClipSpaceParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    transformFromClipSpaceOne(in[i], out[i], static_cast<uint32_t>(i), static_cast<uint32_t>(count),
                               prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// All matrices below are `Mat4{m00,...,m33}` in row-major order (row 3 = translation row, per the
// cbuffer-table comment's row-vector convention).
//
// 1) CameraToWorld = identity, Position=(3,4,5), Rotation=(0,0,0,1) (identity quat):
//    v' = (3,4,5,1) * I = (3,4,5,1); w=1 -> Position_out=(3,4,5) (unchanged).
//    orientationDest = I (upper-left 3x3 of I); transpose(I) = I.
//    qFromMatrix3Precise(I): tr=3>0; S=sqrt(3+1)*2=4; q=((0-0)/4,(0-0)/4,(0-0)/4,0.25*4)=(0,0,0,1).
//    normalize((0,0,0,1)) = (0,0,0,1) (already unit).
//    qMul((0,0,0,1), (0,0,0,1)) = (0,0,0,1) (identity*identity=identity). Rotation_out=(0,0,0,1).
//
// 2) CameraToWorld = identity rotation + translation row (10,20,30) (standard row-vector affine:
//    m30=10,m31=20,m32=30,m33=1, m03=m13=m23=0), Position=(1,2,3):
//    v' = (1,2,3,1) * M: x'=1*1+2*0+3*0+1*10=11; y'=1*0+2*1+3*0+1*20=22; z'=1*0+2*0+3*1+1*30=33;
//    w'=1*0+2*0+3*0+1*1=1. Divide by w=1 (no-op) -> Position_out=(11,22,33). (Rotation part of M
//    is identity here, same as case 1 -> Rotation_out = Rotation_in unchanged.)
//
// 3) CameraToWorld's upper-left 3x3 = a 90-degree-about-Z rotation in row-vector form
//    (row0=(0,1,0), row1=(-1,0,0), row2=(0,0,1); verified v*R for v=(1,0,0) -> (0,1,0), the
//    expected 90-deg CCW-about-Z row-vector rotation), translation=(0,0,0), Position=(0,0,0)
//    (Position math is irrelevant to this case; it isolates the Rotation path),
//    Rotation_in = (0,0,1,0) (a pure 180-deg-about-Z quaternion):
//    transpose(R): row0=(0,-1,0), row1=(1,0,0), row2=(0,0,1).
//    qFromMatrix3Precise(transpose(R)): tr=0+0+1=1>0; S=sqrt(1+1)*2=2*sqrt(2)=2.8284271;
//      qx=(m21-m12)/S=(0-0)/S=0; qy=(m02-m20)/S=(0-0)/S=0; qz=(m10-m01)/S=(1-(-1))/S=2/S=0.7071068;
//      qw=0.25*S=0.7071068. Already unit length (0.7071068^2*2=1.0) -> normalize is a no-op.
//    qCamNormalized = (0, 0, 0.7071068, 0.7071068).
//    qMul(qCamNormalized, (0,0,1,0)):
//      cross(q1.xyz,q2.xyz) = cross((0,0,0.7071068),(0,0,1)) = (0,0,0) (parallel vectors).
//      out.xyz = q2.xyz*q1.w + q1.xyz*q2.w + cross = (0,0,1)*0.7071068 + (0,0,0.7071068)*0 + 0
//              = (0,0,0.7071068).
//      out.w = q1.w*q2.w - dot(q1.xyz,q2.xyz) = 0.7071068*0 - (0+0+0.7071068*1) = -0.7071068.
//    Rotation_out = (0, 0, 0.7071068, -0.7071068).
//
// 4) Degenerate matrix engineered to hit the w==0 edge: CameraToWorld = identity EXCEPT m33=0
//    (so the w-column entry that would normally read 1 for an untransformed w-input reads 0
//    instead), Position=(1,0,0):
//    v' = (1,0,0,1) * M: x'=1*1+0+0+1*m30(0)=1; y'=1*0+0+0+1*m31(0)=0; z'=0; w'=1*m03(0)+0+0+1*m33(0)=0.
//    clip=(1,0,0,0), w=0 -> x_out=1/0=+Inf (nonzero/0), y_out=0/0=NaN, z_out=0/0=NaN.
//    Position_out = (+Inf, NaN, NaN). (Rotation path untouched by m33 -- upper-left 3x3 is still
//    identity here, so Rotation_out follows case 1's identity-rotation result whatever Rotation_in
//    is fed; not re-derived, this case isolates the w==0 division edge.)
inline bool mathvRefTransformFromClipSpaceSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; };
  auto identityMat = []() {
    Mat4 m{};
    m.m00 = 1;
    m.m11 = 1;
    m.m22 = 1;
    m.m33 = 1;
    return m;
  };
  bool ok = true;

  {  // case 1
    SwPoint in{}, out{};
    in.Position = {3.0f, 4.0f, 5.0f};
    in.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    TransformFromClipSpaceParams prm{identityMat()};
    transformFromClipSpaceOne(in, out, 0, 1, prm);
    ok &= approxEq(out.Position.x, 3.0f) && approxEq(out.Position.y, 4.0f) &&
          approxEq(out.Position.z, 5.0f);
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, 0.0f) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, 1.0f);
  }
  {  // case 2
    SwPoint in{}, out{};
    in.Position = {1.0f, 2.0f, 3.0f};
    in.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    Mat4 m = identityMat();
    m.m30 = 10.0f;
    m.m31 = 20.0f;
    m.m32 = 30.0f;
    TransformFromClipSpaceParams prm{m};
    transformFromClipSpaceOne(in, out, 0, 1, prm);
    ok &= approxEq(out.Position.x, 11.0f) && approxEq(out.Position.y, 22.0f) &&
          approxEq(out.Position.z, 33.0f);
  }
  {  // case 3
    SwPoint in{}, out{};
    in.Position = {0.0f, 0.0f, 0.0f};
    in.Rotation = SW_FLOAT4{0.0f, 0.0f, 1.0f, 0.0f};
    Mat4 m{};
    m.m00 = 0.0f;
    m.m01 = 1.0f;
    m.m02 = 0.0f;
    m.m10 = -1.0f;
    m.m11 = 0.0f;
    m.m12 = 0.0f;
    m.m20 = 0.0f;
    m.m21 = 0.0f;
    m.m22 = 1.0f;
    m.m33 = 1.0f;
    TransformFromClipSpaceParams prm{m};
    transformFromClipSpaceOne(in, out, 0, 1, prm);
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, 0.0f) &&
          approxEq(out.Rotation.z, 0.7071068f, 1e-4f) &&
          approxEq(out.Rotation.w, -0.7071068f, 1e-4f);
  }
  {  // case 4
    SwPoint in{}, out{};
    in.Position = {1.0f, 0.0f, 0.0f};
    in.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    Mat4 m = identityMat();
    m.m33 = 0.0f;
    TransformFromClipSpaceParams prm{m};
    transformFromClipSpaceOne(in, out, 0, 1, prm);
    ok &= std::isinf(out.Position.x) && out.Position.x > 0.0f;
    ok &= std::isnan(out.Position.y) && std::isnan(out.Position.z);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
