#pragma once
// mathv_ref_addnoise — CPU scalar oracle for the AddNoise kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/AddNoise.hlsl — NOT derived from sw shaders.
//   cbuffer Params(b0) :6-19 (Amount/Frequency/Phase/Variation/AmountDistribution/
//                              RotationLookupDistance/NoiseOffset/__padding)
//   cbuffer Params(b1) :21-24 (StrengthMode)
//   GetNoise()                   :29-33  -> getNoise() below
//   GetTranslationAndRotation()  :64-108 -> getTranslationAndRotation() below (the *only* one of
//     the file's two same-purpose functions actually called by main(); see DEAD-CODE note below)
//   main()                       :110-133 -> addNoiseOne()/mathvRefAddNoise() below
// Every statement below carries its own :NN back-reference.
//
// Also reads AddNoise.hlsl's own #include closure (opened only because AddNoise.hlsl itself pulls
// them in — no sw shader file was opened to write this ref):
//   shared/hash-functions.hlsl :4 (_PRIME0), :102-113 (hash41u) -> detail::hash41u() below.
//   shared/point.hlsl :19-27 (struct Point layout; already ported host-side as SwPoint in
//     runtime/tixl_point.h, reused not re-derived).
//   shared/quat-functions.hlsl :47-51 (qRotateVec3, the *compiled* "faster" variant — the :40-44
//     commented-out variant is dead HLSL source, not modeled) and :263-303 (qFromMatrix3Precise).
//   noise-functions.hlsl's snoiseVec3 is NOT re-transcribed here: it is already a qualifying P5-safe
//   oracle at tixl_noise_oracle.h (TRANSCRIBED from the same pinned SHA, :254-261), reused verbatim
//   via `sw::tixl_noise::snoiseVec3()` per MATH_VERIFY_WORKFLOW.md's "共享 oracle 復用" instruction
//   — this file supplies only the AddNoise-specific kernel body around it.
//
// Cross-checked against the C# op at external/tixl/Operators/Lib/point/modify/AddNoise.cs:11-35 —
// C# `Strength` maps to the HLSL cbuffer's `Amount`, C# `StrengthFactor` (enum
// FModes{None=0,F1=1,F2=2}) maps to `StrengthMode` (:23); naming differs, values line up 1:1
// (None/0 -> weight=1, F1/1 -> p.FX1, F2/2-or-else -> p.FX2, matching :122's ternary).
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port
// (intentionally never opened while writing this header). Zero metal include, zero app/shaders/
// reference, zero sw math helper.
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule / float discipline inherited from
// tixl_noise_oracle.h): every comparison in this kernel (degeneracy `all(abs(ez)<1e-6)` guard, the
// `tr>0`/`m00>m11` ladder in qFromMatrix3Precise, snoise's own floor()/step() cell boundaries) is a
// float comparison on the GPU; a double ref could land on a different branch than the float GPU.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// runtime/tixl_point.h (SwPoint host struct) and the sibling tixl_noise_oracle.h are included.
#include "runtime/tixl_point.h"
#include "tixl_noise_oracle.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0) (:6-19, HLSL cbuffer packing, same float3+scalar-fills-slot pattern
//                                 documented in mathv_ref_wrappointposition.h):
//   offset  0  float  Amount                 (:8)  slot0 | offset  4  Frequency  (:9)  slot0
//   offset  8  float  Phase                  (:10) slot0 | offset 12  Variation  (:11) slot0
//     (4 scalars exactly fill 16B slot0)
//   offset 16  float3 AmountDistribution     (:13) slot1 lo
//   offset 28  float  RotationLookupDistance (:14) slot1 hi (packs into AmountDistribution's
//     float4 slot, same pattern as WrapPointPosition Center/UseCamera)
//   offset 32  float3 NoiseOffset            (:16) slot2 lo
//   offset 44  float  __padding              (:17) slot2 hi, alignment filler, never read by the
//     kernel, no host field carries it. (:18's commented-out `UseSelection` is dead source, no
//     cbuffer space.)
// cbuffer Params : register(b1) (:21-24):
//   offset  0  int    StrengthMode           (:23) separate cbuffer/register, own 16B slot.
struct AddNoiseParams {
  float amount;                                       // :8  Amount
  float frequency;                                     // :9  Frequency
  float phase;                                         // :10 Phase
  float variation;                                     // :11 Variation
  float amountDistX, amountDistY, amountDistZ;         // :13 AmountDistribution
  float rotationLookupDistance;                        // :14 RotationLookupDistance
  float noiseOffsetX, noiseOffsetY, noiseOffsetZ;      // :16 NoiseOffset
  int32_t strengthMode;                                // :23 StrengthMode (register b1)
};

namespace detail {

using Vec3 = tixl_noise::V3;  // reuse the already-qualified oracle's {x,y,z} float triple.

inline Vec3 vAdd(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 vSub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 vScale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 vMul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }  // component-wise

// HLSL builtin `cross()` intrinsic (standard right-handed formula; not TiXL source, a language
// builtin used at AddNoise.hlsl :90/:97/:101).
inline Vec3 hlslCross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// HLSL builtin `normalize(v)` == `v * rsqrt(dot(v,v))`, modeled here as `v / sqrt(dot(v,v))`
// (used at :86/:87/:90/:93-98/:101). normalize(0,0,0): dot==0 -> sqrt(0)==0 -> 0/0 == NaN
// component-wise, matching the GPU's 0 * rsqrt(0) == 0 * Inf == NaN outcome (known pitfall,
// MATH_VERIFY_WORKFLOW.md §9 "normalize(0)/rsqrt 精度"). See the AMBIGUITY note in
// getTranslationAndRotation() below for how this NaN interacts with the degeneracy guard.
inline Vec3 hlslNormalize(Vec3 v) {
  float len = std::sqrt(tixl_noise::dot3(v, v));
  return {v.x / len, v.y / len, v.z / len};
}

struct Quat { float x, y, z, w; };

// qRotateVec3 — quat-functions.hlsl :47-51 ("faster" variant, the one actually compiled; the
// commented-out :40-44 variant is dead HLSL source and is not modeled).
inline Vec3 qRotateVec3(Vec3 v, Quat q) {
  Vec3 qxyz{q.x, q.y, q.z};
  Vec3 t = vScale(hlslCross(qxyz, v), 2.0f);          // :49  float3 t = 2*cross(q.xyz, v);
  return vAdd(vAdd(v, vScale(t, q.w)), hlslCross(qxyz, t));  // :50 v + q.w*t + cross(q.xyz, t)
}

// Quaternion 3x3-matrix entries, already given in the TRANSPOSED layout qFromMatrix3Precise
// expects — see the NOTED-QUIRK comment at the getTranslationAndRotation() call site for the
// hand-derivation of why {ex,ey,ez} as columns is the correct substitution for
// `transpose(float3x3(ex,ey,ez))`.
struct Mat3 { float m00, m01, m02, m10, m11, m12, m20, m21, m22; };

// qFromMatrix3Precise — quat-functions.hlsl :263-303 (branch-selects the numerically-largest
// diagonal term to avoid dividing by a near-zero S, standard "shepperd's method" quaternion
// extraction).
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

inline Quat hlslNormalizeQuat(Quat q) {  // HLSL builtin normalize() on a float4 (used at :107).
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return {q.x / len, q.y / len, q.z / len, q.w / len};
}

// hash41u — hash-functions.hlsl :102-113. uint32_t arithmetic wraps mod 2^32 the same way HLSL's
// `uint` (unsigned 32-bit, wrap-on-overflow, no UB) does, so this transcription is bit-exact.
inline void hash41u(uint32_t x, float& ox, float& oy, float& oz, float& ow) {
  const uint32_t k = 1103515245u;       // :104 "GLIB C" constant
  const uint32_t kPrime0 = 13331u;      // hash-functions.hlsl :4  _PRIME0
  x *= kPrime0;                          // :105
  x = ((x >> 8u) ^ x) * k;               // :106
  uint32_t y = ((x >> 8u) ^ x) * k;      // :107
  uint32_t z = ((y >> 8u) ^ x) * k;      // :108
  uint32_t w = ((z >> 8u) ^ y) * k;      // :109
  const float inv = 1.0f / float(0xffffffffu);  // :112  (1.0 / float(0xffffffffU))
  ox = float(x) * inv;
  oy = float(y) * inv;
  oz = float(z) * inv;
  ow = float(w) * inv;
}

}  // namespace detail

// getNoise — AddNoise.hlsl :29-33 `float3 GetNoise(float3 pos, float3 variation)`.
inline detail::Vec3 getNoise(detail::Vec3 pos, detail::Vec3 variation, const AddNoiseParams& prm) {
  // :31 float3 noiseLookup = (pos * 0.91 + variation + Phase) * Frequency;
  //     (Phase is a scalar broadcast-added to all 3 axes, standard HLSL scalar+vector promotion)
  detail::Vec3 phased = detail::vAdd(detail::vScale(pos, 0.91f), variation);
  phased = {phased.x + prm.phase, phased.y + prm.phase, phased.z + prm.phase};
  detail::Vec3 noiseLookup = detail::vScale(phased, prm.frequency);
  // :32 return snoiseVec3(noiseLookup) * Amount / 10 * AmountDistribution;
  //     (shared oracle call — reused verbatim, not re-derived; see file header)
  // MINOR-1 (mathv fixer S-verdict): HLSL evaluates left-to-right — `n*Amount/10` is `(n*Amount)/10`,
  // NOT `n*(Amount/10)` (differ in fp ROUNDING, not just notation) — written literally to match.
  tixl_noise::V3 n = tixl_noise::snoiseVec3(noiseLookup);
  detail::Vec3 scaled = {n.x * prm.amount / 10.0f, n.y * prm.amount / 10.0f,
                         n.z * prm.amount / 10.0f};
  return detail::vMul(scaled, {prm.amountDistX, prm.amountDistY, prm.amountDistZ});
}

// getTranslationAndRotation — AddNoise.hlsl :64-108 `void GetTranslationAndRotation(...)`.
//
// DEAD-CODE note: AddNoise.hlsl declares a second, near-identical function
// `GetTranslationAndRotation_` (:37-62, underscore suffix) never called anywhere in the file
// (main() at :127 calls only the un-underscored `GetTranslationAndRotation`). Not modeled here —
// dead source doesn't affect kernel output; flagged for S/X in case a future TiXL edit starts
// calling it (this ref would need updating then).
inline void getTranslationAndRotation(float weight, detail::Vec3 pointPos, detail::Quat rotation,
                                       const detail::Vec3& variationOffset,
                                       const AddNoiseParams& prm, detail::Vec3& offset,
                                       detail::Quat& newRotation) {
  // :72-73 -- base noise offset. `variationOffset` is a per-thread `static float3` in the HLSL
  // (:35, assigned once in main() at :119) that GetNoise's call sites read directly rather than
  // receiving as a parameter; this oracle threads it through explicitly instead (same value, no
  // behavior difference — HLSL's per-thread "static" has no cross-thread persistence here).
  detail::Vec3 noiseOffset{prm.noiseOffsetX, prm.noiseOffsetY, prm.noiseOffsetZ};
  offset = detail::vScale(getNoise(detail::vAdd(pointPos, noiseOffset), variationOffset, prm), weight);
  // :76-78 -- rotated local X direction lookup.
  detail::Vec3 xDir = detail::qRotateVec3({prm.rotationLookupDistance, 0.0f, 0.0f}, rotation);
  detail::Vec3 offsetAtPosX = detail::vScale(getNoise(detail::vAdd(pointPos, xDir), variationOffset, prm), weight);
  detail::Vec3 rotatedXDir = detail::vSub(detail::vAdd(detail::vAdd(pointPos, xDir), offsetAtPosX),
                                           detail::vAdd(pointPos, offset));
  // :81-83 -- rotated local Y direction lookup.
  detail::Vec3 yDir = detail::qRotateVec3({0.0f, prm.rotationLookupDistance, 0.0f}, rotation);
  detail::Vec3 offsetAtPosY = detail::vScale(getNoise(detail::vAdd(pointPos, yDir), variationOffset, prm), weight);
  detail::Vec3 rotatedYDir = detail::vSub(detail::vAdd(detail::vAdd(pointPos, yDir), offsetAtPosY),
                                           detail::vAdd(pointPos, offset));

  // :86-87 -- normalize base directions (can yield NaN on an exact-zero input; see hlslNormalize).
  detail::Vec3 ex = detail::hlslNormalize(rotatedXDir);
  detail::Vec3 ey = detail::hlslNormalize(rotatedYDir);

  // :90 -- right-handed orthonormal basis third axis.
  detail::Vec3 ez = detail::hlslNormalize(detail::hlslCross(ex, ey));

  // :93-98 -- degeneracy guard (ex/ey nearly collinear -> fall back to the un-noised xDir/yDir
  // directions). AMBIGUITY (flag for S): HLSL `all(abs(ez) < 1e-6)` is false whenever ANY
  // component of `ez` is NaN (IEEE-754 comparisons with NaN are always false; this oracle models
  // that literally: `std::fabs(NaN) < 1e-6f` is `false` in C++ too). So this guard does NOT catch
  // ez==NaN, only finite-and-collinear ez. E.g. RotationLookupDistance==0.0f collapses
  // xDir==yDir==(0,0,0); combined with Amount==0 (or any Amount whose sampled offsets cancel) that
  // drives rotatedXDir/rotatedYDir to the zero vector, so ex/ey/ez all become NaN via
  // hlslNormalize(0), and the NaN then rides straight through qFromMatrix3Precise's branch ladder
  // (every NaN comparison is false -> always falls to the final `else`) into newRotation as an
  // all-NaN quaternion. RotationLookupDistance==0 is a plausible authored value ("disable
  // directional lookup"), so this is a genuine TiXL kernel trap, not a translation artifact --
  // transcribed verbatim (NAMED-FORK/NOTED-QUIRK discipline), not "fixed". Whether sw's Metal
  // fast-math preserves this same false-on-NaN comparison is a kernel-port concern for D/S, not a
  // ref-fidelity one (MATH_VERIFY_WORKFLOW.md §9 fast-math/NaN class of traps).
  if (std::fabs(ez.x) < 1e-6f && std::fabs(ez.y) < 1e-6f && std::fabs(ez.z) < 1e-6f) {
    ex = detail::hlslNormalize(xDir);
    ey = detail::hlslNormalize(yDir);
    ez = detail::hlslNormalize(detail::hlslCross(ex, ey));
  }

  // :101 -- re-orthogonalize ey against the (possibly just-recomputed) ez/ex.
  ey = detail::hlslNormalize(detail::hlslCross(ez, ex));

  // :104,107 -- NOTED-QUIRK (transcribed as an algebraic simplification, verified by hand, not a
  // behavior change): HLSL `float3x3(ex, ey, ez)` builds a matrix whose ROWS are ex, ey, ez
  // (standard HLSL row-vector matrix constructor); `transpose(...)` of that is the matrix whose
  // COLUMNS are ex, ey, ez:
  //   m00=ex.x  m01=ey.x  m02=ez.x
  //   m10=ex.y  m11=ey.y  m12=ez.y
  //   m20=ex.z  m21=ey.z  m22=ez.z
  // This oracle writes those 9 entries directly (detail::Mat3 below) instead of modeling
  // "construct rows, then transpose" as two separate steps -- the result is identical, and this
  // sidesteps needing a general float3x3 type + transpose() for a single call site.
  detail::Mat3 m{ex.x, ey.x, ez.x, ex.y, ey.y, ez.y, ex.z, ey.z, ez.z};
  newRotation = detail::hlslNormalizeQuat(detail::qFromMatrix3Precise(m));  // :107
}

// addNoiseOne — one thread of `main()` (:110-133), operating on a single point.
inline void addNoiseOne(const SwPoint& in, SwPoint& out, uint32_t idx, uint32_t numStructs,
                         const AddNoiseParams& prm) {
  // :112-117 -- GetDimensions/idx>=numStructs dispatch guard. Same AMBIGUITY as
  // mathv_ref_wrappointposition.h's guard note: a host loop over exactly `count` points (as
  // mathvRefAddNoise below does) never trips this branch, since numStructs == count here, so it is
  // transcribed for documentation/fidelity but unreachable through this file's own driver loop.
  if (idx >= numStructs) {
    out = in;
    return;
  }

  // :119 -- variationOffset = hash41u(i.x).xyz * Variation;
  float hx, hy, hz, hw;
  detail::hash41u(idx, hx, hy, hz, hw);
  (void)hw;  // hash41u's .w lane is computed (bit-exact to the HLSL) but AddNoise only reads .xyz.
  detail::Vec3 variationOffset{hx * prm.variation, hy * prm.variation, hz * prm.variation};

  // :121 -- Point p = SourcePoints[i.x]; (copy every field; only Position/Rotation change below,
  // matching the fact that main() only ever assigns p.Position and p.Rotation before the :132
  // writeback -- Color/Scale/FX1/FX2 pass through unchanged).
  out = in;

  // :122 -- weight = StrengthMode==0 ? 1 : ((StrengthMode==1) ? p.FX1 : p.FX2);
  // StrengthMode outside {0,1} (including negative values) falls through to the FX2 arm, exactly
  // like the HLSL ternary chain -- not clamped or asserted against, transcribed verbatim.
  float weight = prm.strengthMode == 0 ? 1.0f : (prm.strengthMode == 1 ? in.FX1 : in.FX2);

  // :124-125 -- float3 offset; float4 newRotation = p.Rotation; (this initializer is moot: :107
  // unconditionally overwrites newRotation on every code path through getTranslationAndRotation(),
  // there is no early-return branch that would leave it at its initial value).
  detail::Vec3 position{in.Position.x, in.Position.y, in.Position.z};
  detail::Quat rotation{in.Rotation.x, in.Rotation.y, in.Rotation.z, in.Rotation.w};
  detail::Vec3 offset{};
  detail::Quat newRotation = rotation;

  // :127 -- GetTranslationAndRotation(weight, p.Position + variationOffset, p.Rotation, offset,
  // newRotation); NOTE: `variationOffset` perturbs only the noise-lookup coordinate (`pointPos`)
  // fed into the function below -- the FINAL Position write-back at :129 adds the resulting
  // `offset` to the ORIGINAL p.Position, not to `pointPos`. Do not conflate the two.
  detail::Vec3 pointPos = detail::vAdd(position, variationOffset);
  getTranslationAndRotation(weight, pointPos, rotation, variationOffset, prm, offset, newRotation);

  // :129-130 -- p.Position += offset; p.Rotation = newRotation;
  out.Position.x = in.Position.x + offset.x;
  out.Position.y = in.Position.y + offset.y;
  out.Position.z = in.Position.z + offset.z;
  out.Rotation.x = newRotation.x;
  out.Rotation.y = newRotation.y;
  out.Rotation.z = newRotation.z;
  out.Rotation.w = newRotation.w;
  // :132 -- ResultPoints[i.x] = p; (Color/Scale/FX1/FX2 already carried over via `out = in` above)
}

// mathvRefAddNoise — full-buffer CPU oracle (:110-133 dispatch loop over `count` points).
// `in` and `out` may alias the same array (HLSL operates SourcePoints -> ResultPoints as distinct
// buffers, but addNoiseOne reads every needed `in` field into locals before writing `out`, so
// in-place aliasing is safe, same convention as mathv_ref_wrappointposition.h).
inline void mathvRefAddNoise(const SwPoint* in, SwPoint* out, size_t count,
                              const AddNoiseParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    addNoiseOne(in[i], out[i], static_cast<uint32_t>(i), static_cast<uint32_t>(count), prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// 1) Amount=0 identity round-trip: GetNoise (:32) always returns (0,0,0) when Amount=0 (whatever
//    Frequency/Phase/Variation/AmountDistribution/the noise field are), so offset/offsetAtPosX/
//    offsetAtPosY are all exactly (0,0,0). With RotationLookupDistance=5, Rotation=identity
//    (0,0,0,1): qRotateVec3(v,identity)=v (q.xyz=0 -> t=0 -> v+1*0+cross(0,0)=v), so xDir=(5,0,0),
//    yDir=(0,5,0). rotatedXDir=(pointPos+xDir+0)-(pointPos+0)=xDir -> ex=normalize((5,0,0))=(1,0,0).
//    rotatedYDir=yDir -> ey=(0,1,0). ez=normalize(cross(ex,ey))=normalize((0,0,1))=(0,0,1); abs(ez)
//    not all <1e-6 -> degeneracy branch skipped. ey=normalize(cross(ez,ex))=(0,1,0) (unchanged,
//    basis already orthonormal). m={ex.x,ey.x,ez.x,ex.y,ey.y,ez.y,ex.z,ey.z,ez.z}=identity 3x3.
//    qFromMatrix3Precise(identity): tr=3>0; S=sqrt(4)*2=4; q=(0,0,0,1); normalize->(0,0,0,1).
//    => Position_out==Position_in (offset=0), Rotation_out==(0,0,0,1)==Rotation_in.
//
// 2) RotationLookupDistance=0 NaN quirk (see AMBIGUITY above the degeneracy guard): same Amount=0
//    setup but RotationLookupDistance=0, identity rotation: xDir=qRotateVec3((0,0,0),identity)=
//    (0,0,0); yDir=(0,0,0). rotatedXDir=rotatedYDir=(0,0,0) -> ex=ey=normalize(0,0,0)=NaN,NaN,NaN.
//    ez=normalize(cross(ex,ey))=NaN (cross of NaN vectors is NaN). abs(ez)=NaN,NaN,NaN; `NaN<1e-6`
//    is false per-component (IEEE-754) -> `all(...)` false -> degeneracy recovery SKIPPED (the NaN
//    slips past the guard meant to catch exactly this). ey=normalize(cross(ez,ex))-> still NaN.
//    m=all-NaN. qFromMatrix3Precise: tr=NaN; every branch condition involving NaN is false -> falls
//    to the final `else`: S=sqrt(NaN)=NaN; q=all-NaN; normalize(NaN quat) -> still all-NaN.
//    => Position_out==Position_in (offset still 0), but Rotation_out is (NaN,NaN,NaN,NaN) even
//       though every input was finite -- a genuine TiXL kernel trap, not a translation bug.
//
// 3) hash41u(0) == (0,0,0,0) exactly: x=0 -> every step multiplies zero by a constant (x*=PRIME0,
//    then three rounds of `(shift^x)*k`), staying zero throughout -> hash41u(0)=(0,0,0,0),
//    independent of Variation -> variationOffset for point index 0 is always (0,0,0).
inline bool mathvRefAddNoiseSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  {  // case 1: Amount=0 identity round-trip
    SwPoint in{}, out{};
    in.Position = {2.0f, 3.0f, -1.0f};
    in.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    AddNoiseParams p{};
    p.amount = 0.0f;
    p.frequency = 1.3f;
    p.phase = 0.7f;
    p.variation = 0.4f;
    p.amountDistX = p.amountDistY = p.amountDistZ = 1.0f;
    p.rotationLookupDistance = 5.0f;
    p.noiseOffsetX = p.noiseOffsetY = p.noiseOffsetZ = 0.0f;
    p.strengthMode = 0;
    addNoiseOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, 2.0f) && approxEq(out.Position.y, 3.0f) &&
          approxEq(out.Position.z, -1.0f) && approxEq(out.Rotation.x, 0.0f) &&
          approxEq(out.Rotation.y, 0.0f) && approxEq(out.Rotation.z, 0.0f) &&
          approxEq(out.Rotation.w, 1.0f);
  }
  {  // case 2: RotationLookupDistance=0 NaN quirk
    SwPoint in{}, out{};
    in.Position = {2.0f, 3.0f, -1.0f};
    in.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    AddNoiseParams p{};
    p.amount = 0.0f;
    p.frequency = 1.3f;
    p.phase = 0.7f;
    p.variation = 0.4f;
    p.amountDistX = p.amountDistY = p.amountDistZ = 1.0f;
    p.rotationLookupDistance = 0.0f;
    p.noiseOffsetX = p.noiseOffsetY = p.noiseOffsetZ = 0.0f;
    p.strengthMode = 0;
    addNoiseOne(in, out, 0, 1, p);
    ok &= approxEq(out.Position.x, 2.0f) && approxEq(out.Position.y, 3.0f) &&
          approxEq(out.Position.z, -1.0f) && std::isnan(out.Rotation.x) &&
          std::isnan(out.Rotation.y) && std::isnan(out.Rotation.z) && std::isnan(out.Rotation.w);
  }
  {  // case 3: hash41u(0) == (0,0,0,0)
    float hx, hy, hz, hw;
    detail::hash41u(0u, hx, hy, hz, hw);
    ok &= approxEq(hx, 0.0f) && approxEq(hy, 0.0f) && approxEq(hz, 0.0f) && approxEq(hw, 0.0f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
