#pragma once
// mathv_ref_meshverticestopoints — CPU scalar oracle for the MeshVerticesToPoints kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/generate/MeshVerticesToPoints.hlsl — NOT derived from sw
// shaders.
//   cbuffer Params : register(b0) :6-10  (OffsetByTBN, OffsetScale)
//   main()                        :15-31 -> meshVerticesToPointsOne() / mathvRefMeshVerticesToPoints()
//                                            below, every statement carries its own :NN reference.
// Also reads the shader's own #include closure (opened only because MeshVerticesToPoints.hlsl
// itself pulls them in — no sw shader file was opened to write this ref):
//   shared/point.hlsl :19-27      (struct Point; already ported host-side as SwPoint in
//                                   runtime/tixl_point.h, reused not re-derived).
//   shared/pbr.hlsl :18-28        (struct PbrVertex; already ported host-side as SwVertex in
//                                   runtime/sw_mesh.h, reused not re-derived — see the field
//                                   mapping table below).
//   shared/quat-functions.hlsl :263-303 (qFromMatrix3Precise — same function already transcribed
//                                   once in mathv_ref_addnoise.h; re-transcribed locally here in
//                                   this file's own `detail` namespace rather than shared, matching
//                                   the current one-op-one-file convention — MATH_VERIFY_WORKFLOW.md
//                                   §1.4 notes a future `mathv_ref_shared_quat.h` is not built yet).
//   shared/hash-functions.hlsl (:1) is #included by the shader's closure but nothing in main()
//   calls a hash function — dead include for this kernel, not modeled (same treatment as the
//   analogous dead-include note in mathv_ref_snaptopoints.h).
//
// Cross-checked against the C# op at
// external/tixl/Operators/Lib/point/generate/MeshVerticesToPoints.cs — field names OffsetByTBN /
// OffsetScale line up 1:1 with the cbuffer above.
//
// PROVENANCE discipline (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5):
// this file is a P5-safe oracle, transcribed straight from the TiXL HLSL text above, never from
// sw's own app/shaders/ port (that file is intentionally NOT opened by the R role writing this
// header — the whole point of mathv is to fuzz the CPU ref against the *independent* MSL port, so
// any accidental peek would self-certify a bug instead of catching it). Zero metal include, zero
// app/shaders/ reference, zero sw math helper.
//
// FLOAT (not double) on purpose (MATH_VERIFY_WORKFLOW.md §2.3 branchy-class rule, inherited from
// tixl_noise_oracle.h / mathv_ref_addnoise.h): every branch in qFromMatrix3Precise (`tr>0`,
// `m00>m11 && m00>m22`, `m11>m22`) is a float comparison on the GPU; a double CPU ref could land on
// a different branch than the float GPU for the same nominal input.
//
// ===== VERTEX FIELD MAPPING (TiXL PbrVertex <-> sw SwVertex) — data layout, NOT math =====
// runtime/sw_mesh.h's header comment + static_asserts already prove SwVertex is byte-exactly the
// same 80-byte / 20-float stride as TiXL's PbrVertex.cs (pbr.hlsl's PbrVertex struct matches that
// same C# layout — HLSL cbuffer/StructuredBuffer packing for this particular field sequence happens
// to fall on the same offsets as PbrVertex.cs's explicit FieldOffset attributes). So this is a pure
// RENAME table, not a currency conversion — every field the kernel reads exists at the identical
// offset under a different capitalization:
//   PbrVertex.Position  @0  <-> SwVertex.Position   @0
//   PbrVertex.Normal    @12 <-> SwVertex.Normal     @12
//   PbrVertex.Tangent   @24 <-> SwVertex.Tangent    @24
//   PbrVertex.Bitangent @36 <-> SwVertex.Bitangent  @36
//   PbrVertex.TexCoord  @48 <-> SwVertex.Texcoord   @48   (unread by this kernel)
//   PbrVertex.TexCoord2 @56 <-> SwVertex.Texcoord2  @56   (unread by this kernel)
//   PbrVertex.Selected  @64 <-> SwVertex.Selection  @64
//   PbrVertex.ColorRGB  @68 <-> SwVertex.ColorRgb   @68
// (offsets per runtime/sw_mesh.h's own static_assert block; TexCoord/TexCoord2 are carried in the
// struct but this kernel's main() body never references them.)
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// runtime/tixl_point.h (SwPoint host struct) and runtime/sw_mesh.h (SwVertex host struct) are
// included, for data layout — not math.
#include "runtime/sw_mesh.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0) (:6-10, HLSL cbuffer packing: float3 rounds up to a 16B slot
//                                 unless a trailing scalar fits in the leftover 4B — same pattern
//                                 documented in mathv_ref_wrappointposition.h's Center/UseCamera):
//   offset  0  float3 OffsetByTBN  (:8)  -- slot0 lo
//   offset 12  float  OffsetScale  (:9)  -- slot0 hi (packs into OffsetByTBN's float4 slot)
struct MeshVerticesToPointsParams {
  float offsetByTBNx, offsetByTBNy, offsetByTBNz;  // :8  OffsetByTBN
  float offsetScale;                               // :9  OffsetScale
};

namespace detail {

struct Vec3 { float x, y, z; };

// Quaternion 3x3-matrix entries, already given in the TRANSPOSED layout qFromMatrix3Precise
// expects (see the NAMED-FORK comment at the call site below for the hand-derivation of why
// {Tangent,Bitangent,Normal} as COLUMNS is the correct substitution for
// `transpose(float3x3(Tangent,Bitangent,Normal))`). Same struct shape as mathv_ref_addnoise.h's
// local Mat3 — re-declared here (not shared) per this file's own P5-isolation.
struct Mat3 { float m00, m01, m02, m10, m11, m12, m20, m21, m22; };
struct Quat { float x, y, z, w; };

// qFromMatrix3Precise — quat-functions.hlsl :263-303 (branch-selects the numerically-largest
// diagonal term to avoid dividing by a near-zero S, standard "Shepperd's method" quaternion
// extraction). Identical transcription to mathv_ref_addnoise.h's detail::qFromMatrix3Precise.
inline Quat qFromMatrix3Precise(const Mat3& m) {
  float tr = m.m00 + m.m11 + m.m22;                                    // :265
  if (tr > 0.0f) {                                                     // :267
    float S = std::sqrt(tr + 1.0f) * 2.0f;                             // :269 S=4*qw
    return {(m.m21 - m.m12) / S, (m.m02 - m.m20) / S, (m.m10 - m.m01) / S, 0.25f * S};  // :270-274
  } else if (m.m00 > m.m11 && m.m00 > m.m22) {                         // :276
    float S = std::sqrt(1.0f + m.m00 - m.m11 - m.m22) * 2.0f;          // :278 S=4*qx
    return {0.25f * S, (m.m01 + m.m10) / S, (m.m02 + m.m20) / S, (m.m21 - m.m12) / S};  // :279-283
  } else if (m.m11 > m.m22) {                                          // :285
    float S = std::sqrt(1.0f + m.m11 - m.m00 - m.m22) * 2.0f;          // :287 S=4*qy
    return {(m.m01 + m.m10) / S, 0.25f * S, (m.m12 + m.m21) / S, (m.m02 - m.m20) / S};  // :288-292
  } else {
    float S = std::sqrt(1.0f + m.m22 - m.m00 - m.m11) * 2.0f;          // :296 S=4*qz
    return {(m.m02 + m.m20) / S, (m.m12 + m.m21) / S, 0.25f * S, (m.m10 - m.m01) / S};  // :297-301
  }
}

// HLSL builtin normalize() on a float4 == v * rsqrt(dot(v,v)), modeled here as v / sqrt(dot(v,v))
// (same convention as mathv_ref_addnoise.h's hlslNormalizeQuat / mathv_ref_wrappointposition.h's
// notes on normalize(0)). normalize(0,0,0,0): dot==0 -> sqrt(0)==0 -> 0/0 == NaN component-wise.
// Unreachable through a valid orthonormal TBN input (qFromMatrix3Precise never returns the zero
// quaternion for a genuine rotation matrix), documented for completeness only.
inline Quat hlslNormalizeQuat(Quat q) {
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return {q.x / len, q.y / len, q.z / len, q.w / len};
}

}  // namespace detail

// meshVerticesToPointsOne — one thread of `main()` (:15-31), operating on a single vertex/point
// pair. Unlike mathv_ref_wrappointposition.h / mathv_ref_addnoise.h, this kernel's HLSL never
// calls GetDimensions() and has NO `idx >= count` dispatch guard at all (:15-31 is unconditional
// once-through math) — so there is no guard branch to transcribe here; a host loop over exactly
// `count` vertices (mathvRefMeshVerticesToPoints below) matches the source 1:1 with nothing elided.
inline void meshVerticesToPointsOne(const SwVertex& v, SwPoint& out,
                                     const MeshVerticesToPointsParams& prm) {
  // :20 -- ResultPoints[index].Position = v.Position
  //         + OffsetByTBN.x * v.Tangent   * OffsetScale
  //         + OffsetByTBN.y * v.Bitangent * OffsetScale
  //         + OffsetByTBN.z * v.Normal    * OffsetScale;
  out.Position.x = v.Position.x + prm.offsetByTBNx * v.Tangent.x * prm.offsetScale +
                    prm.offsetByTBNy * v.Bitangent.x * prm.offsetScale +
                    prm.offsetByTBNz * v.Normal.x * prm.offsetScale;
  out.Position.y = v.Position.y + prm.offsetByTBNx * v.Tangent.y * prm.offsetScale +
                    prm.offsetByTBNy * v.Bitangent.y * prm.offsetScale +
                    prm.offsetByTBNz * v.Normal.y * prm.offsetScale;
  out.Position.z = v.Position.z + prm.offsetByTBNx * v.Tangent.z * prm.offsetScale +
                    prm.offsetByTBNy * v.Bitangent.z * prm.offsetScale +
                    prm.offsetByTBNz * v.Normal.z * prm.offsetScale;

  // :22 -- ResultPoints[index].FX1 = v.Selected;  (SwPoint.FX1 <-> Point.FX1 @12, point.hlsl :22)
  out.FX1 = v.Selection;

  // :24 -- float3x3 m = float3x3(v.Tangent, v.Bitangent, v.Normal);
  // HLSL matrix-from-vectors constructor fills ROWS: row0=Tangent, row1=Bitangent, row2=Normal.
  // :25 -- transpose(m): NAMED-FORK/hand-derivation (same shape as mathv_ref_addnoise.h's
  // getTranslationAndRotation() call site, ex/ey/ez there == Tangent/Bitangent/Normal here) --
  // transpose(m) is the matrix whose COLUMNS are Tangent, Bitangent, Normal:
  //   m00=Tangent.x  m01=Bitangent.x  m02=Normal.x
  //   m10=Tangent.y  m11=Bitangent.y  m12=Normal.y
  //   m20=Tangent.z  m21=Bitangent.z  m22=Normal.z
  // This oracle writes those 9 entries directly instead of modeling "construct rows, then
  // transpose" as two separate steps — the result is identical, and this sidesteps needing a
  // general float3x3 type + transpose() for a single call site.
  detail::Mat3 m{v.Tangent.x, v.Bitangent.x, v.Normal.x,
                 v.Tangent.y, v.Bitangent.y, v.Normal.y,
                 v.Tangent.z, v.Bitangent.z, v.Normal.z};
  detail::Quat rot = detail::hlslNormalizeQuat(detail::qFromMatrix3Precise(m));  // :25

  // :27 -- ResultPoints[index].Rotation = normalize(rot);  NOTED-QUIRK: the HLSL normalizes
  // TWICE in a row — once building `rot` at :25 (normalize(qFromMatrix3Precise(...))), then again
  // at :27 (normalize(rot)) when writing it out. This is redundant for an exact-math rotation
  // quaternion (a unit vector renormalized is itself, up to rounding) but is what the source
  // literally does, so both normalize() calls are transcribed as two distinct steps rather than
  // collapsed into one — the second pass can still perturb the last ULP relative to a
  // single-normalize CPU ref, which matters at the mathv exact-class tolerance.
  detail::Quat rotOut = detail::hlslNormalizeQuat(rot);  // :27
  out.Rotation = {rotOut.x, rotOut.y, rotOut.z, rotOut.w};

  // :28 -- ResultPoints[index].Color = float4(v.ColorRGB, 1);
  out.Color = {v.ColorRgb.x, v.ColorRgb.y, v.ColorRgb.z, 1.0f};

  // :29 -- ResultPoints[index].FX2 = v.Selected;
  out.FX2 = v.Selection;

  // :30 -- ResultPoints[index].Scale = 1;  (HLSL scalar-to-float3 broadcast assignment)
  out.Scale = {1.0f, 1.0f, 1.0f};
}

// mathvRefMeshVerticesToPoints — full-buffer CPU oracle (:15-31 dispatch loop over `count`
// vertices -> points). `verts` and `out` are separate arrays on purpose (unlike
// mathv_ref_wrappointposition.h's in-place SwPoint buffer, this kernel reads a distinct
// StructuredBuffer<PbrVertex> `Vertices` (t0) and writes a distinct
// RWStructuredBuffer<Point> `ResultPoints` (u0) — no aliasing to model).
inline void mathvRefMeshVerticesToPoints(const SwVertex* verts, SwPoint* out, size_t count,
                                          const MeshVerticesToPointsParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    meshVerticesToPointsOne(verts[i], out[i], prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// 1) Identity TBN frame: Tangent=(1,0,0), Bitangent=(0,1,0), Normal=(0,0,1), Position=(0,0,0),
//    Selection=0.5, ColorRgb=(0.2,0.3,0.4), OffsetByTBN=(0,0,0) (OffsetScale irrelevant, multiplies
//    zero):
//    Position_out = (0,0,0) + 0 = (0,0,0). FX1=FX2=0.5. Color=(0.2,0.3,0.4,1). Scale=(1,1,1).
//    transpose(float3x3(T,B,N)) with T=(1,0,0),B=(0,1,0),N=(0,0,1) columns == identity matrix.
//    qFromMatrix3Precise(identity): tr=3>0; S=sqrt(4)*2=4; q=((0-0)/4,(0-0)/4,(0-0)/4,0.25*4)
//    =(0,0,0,1) -- already unit, normalize()x2 is a no-op -> Rotation_out=(0,0,0,1).
//
// 2) Position offset, same identity frame, Position=(1,2,3), OffsetByTBN=(1,0,0), OffsetScale=2
//    (only the Tangent term contributes):
//    Position_out = (1,2,3) + 1*(1,0,0)*2 + 0 + 0 = (3,2,3).
//
// 3) 90 deg rotation about Z: Tangent=(0,1,0), Bitangent=(-1,0,0), Normal=(0,0,1) (columns of the
//    standard Rz(90) matrix [[0,-1,0],[1,0,0],[0,0,1]]), OffsetByTBN=(0,0,0):
//    transpose(float3x3(T,B,N)) columns = T,B,N -> m00=T.x=0,m01=B.x=-1,m02=N.x=0;
//    m10=T.y=1,m11=B.y=0,m12=N.y=0; m20=T.z=0,m21=B.z=0,m22=N.z=1.
//    tr=0+0+1=1>0; S=sqrt(1+1)*2=2*sqrt(2)=2.828427.
//    qx=(m21-m12)/S=0; qy=(m02-m20)/S=0; qz=(m10-m01)/S=(1-(-1))/2.828427=0.707107;
//    qw=0.25*S=0.707107. q=(0,0,0.707107,0.707107) -- the known Rz(90) quaternion
//    (sin45=cos45=0.707107); |q|=1 already, normalize()x2 no-op.
//
// 4) 180 deg rotation about X (exercises the m00>m11 && m00>m22 branch, not the tr>0 branch):
//    Tangent=(1,0,0), Bitangent=(0,-1,0), Normal=(0,0,-1) (columns of Rx(180)):
//    m00=1,m11=-1,m22=-1 -> tr=-1 (not >0); m00>m11 (1>-1) and m00>m22 (1>-1) -> branch 2.
//    S=sqrt(1+1-(-1)-(-1))*2=sqrt(4)*2=4. qx=0.25*4=1.
//    m01=B.x=0,m10=T.y=0 -> qy=(0+0)/4=0. m02=N.x=0,m20=T.z=0 -> qz=(0+0)/4=0.
//    m21=B.z=0,m12=N.y=0 -> qw=(0-0)/4=0. q=(1,0,0,0) -- the known Rx(180) quaternion.
inline bool mathvRefMeshVerticesToPointsSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  auto mkVertex = [](float px, float py, float pz, float tx, float ty, float tz, float bx,
                      float by, float bz, float nx, float ny, float nz, float sel, float cr,
                      float cg, float cb) {
    SwVertex v{};
    v.Position = {px, py, pz};
    v.Tangent = {tx, ty, tz};
    v.Bitangent = {bx, by, bz};
    v.Normal = {nx, ny, nz};
    v.Selection = sel;
    v.ColorRgb = {cr, cg, cb};
    return v;
  };

  {  // case 1: identity frame
    SwVertex v = mkVertex(0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0.5f, 0.2f, 0.3f, 0.4f);
    SwPoint out{};
    MeshVerticesToPointsParams p{0, 0, 0, 0.0f};
    meshVerticesToPointsOne(v, out, p);
    ok &= approxEq(out.Position.x, 0.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f) && approxEq(out.FX1, 0.5f) && approxEq(out.FX2, 0.5f) &&
          approxEq(out.Color.x, 0.2f) && approxEq(out.Color.y, 0.3f) &&
          approxEq(out.Color.z, 0.4f) && approxEq(out.Color.w, 1.0f) &&
          approxEq(out.Scale.x, 1.0f) && approxEq(out.Scale.y, 1.0f) && approxEq(out.Scale.z, 1.0f) &&
          approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, 0.0f) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, 1.0f);
  }
  {  // case 2: position offset via tangent term
    SwVertex v = mkVertex(1, 2, 3, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0.0f, 0, 0, 0);
    SwPoint out{};
    MeshVerticesToPointsParams p{1.0f, 0.0f, 0.0f, 2.0f};
    meshVerticesToPointsOne(v, out, p);
    ok &= approxEq(out.Position.x, 3.0f) && approxEq(out.Position.y, 2.0f) &&
          approxEq(out.Position.z, 3.0f);
  }
  {  // case 3: 90deg about Z -> qz=qw=0.707107
    SwVertex v = mkVertex(0, 0, 0, 0, 1, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0);
    SwPoint out{};
    MeshVerticesToPointsParams p{0, 0, 0, 0.0f};
    meshVerticesToPointsOne(v, out, p);
    ok &= approxEq(out.Rotation.x, 0.0f, 1e-4f) && approxEq(out.Rotation.y, 0.0f, 1e-4f) &&
          approxEq(out.Rotation.z, 0.70710678f, 1e-4f) &&
          approxEq(out.Rotation.w, 0.70710678f, 1e-4f);
  }
  {  // case 4: 180deg about X -> q=(1,0,0,0), exercises the m00>m11&&m00>m22 branch
    SwVertex v = mkVertex(0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, 0);
    SwPoint out{};
    MeshVerticesToPointsParams p{0, 0, 0, 0.0f};
    meshVerticesToPointsOne(v, out, p);
    ok &= approxEq(out.Rotation.x, 1.0f, 1e-4f) && approxEq(out.Rotation.y, 0.0f, 1e-4f) &&
          approxEq(out.Rotation.z, 0.0f, 1e-4f) && approxEq(out.Rotation.w, 0.0f, 1e-4f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
