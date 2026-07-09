#pragma once
// mathv_ref_blendpoints — CPU scalar oracle for the BlendPoints kernel (combine class: 2 input
// point buffers -> 1 output point buffer).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/combine/BlendPoints.hlsl — NOT derived from sw shaders.
//   cbuffer Params :5-12 (BlendFactor,BlendMode,PairingMode,Width,Scatter); SmootherStep() :18-22
//   -> detail::smootherStep(); main() body :24-106 -> mathvRefBlendPoints()/blendPointsOne()
//   below, every statement carries its own :NN back-reference; #include "shared/hash-functions.hlsl"
//   hash11() :10-16 -> detail::hash11(); #include "shared/quat-functions.hlsl" qSlerp() :162-215
//   -> detail::hlslQSlerp().
// Cross-checked against the C# op (parameter names, not order — the cbuffer packs in shader
// declaration order, which differs from the C# InputSlot order) at
// external/tixl/Operators/Lib/point/combine/BlendPoints.cs:10-29 (PointsA_/PointsB_/BlendFactor/
// Scatter/BlendMode/RangeWidth/Pairing, enums BlendModes{Mix,UseA_F1,UseB_F1,
// MixWithSlidingRange,MixWithSlidingRangeSmooth} / PairingModes{WrapAround,Adjust}), and the
// shared struct layout at .../shared/point.hlsl:19-27 (Point: Position/FX1/Rotation/Color/
// Scale/FX2).
//
// PROVENANCE discipline (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5):
// P5-safe oracle, not a P5 self-consistent trap — transcribed straight from the TiXL HLSL text
// above, never from sw's own Metal port (intentionally NOT opened by the R role writing this
// header — an accidental peek would self-certify a bug instead of catching one). Zero metal
// include, zero app/shaders/ reference, zero sw math helper.
//
// FLOAT (not double) on purpose (MATH_VERIFY_WORKFLOW.md §2.3 branchy-class rule): every branch
// (BlendMode thresholds, PairingMode gate, qSlerp's cosHalfAngle forks, isnan-noBlend collapse)
// is a float comparison on the GPU; a double CPU ref could land on the opposite side of a branch
// boundary than the float GPU for the same nominal input.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// app/src/runtime/tixl_point.h is included, for the SwPoint host struct (data layout, not math).
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0)  (:5-12) — 5 scalar floats, no float3/vector members, so this is
//   plain sequential packing with NO HLSL cbuffer slot-boundary surprise (unlike WrapPointPosition's
//   Center/Size float3s): BlendFactor(:7)@0, BlendMode(:8)@4, PairingMode(:9)@8, Width(:10)@12 fill
//   exactly one 16B slot; Scatter(:11)@16 starts the next slot alone.
struct BlendPointsParams {
  float blendFactor;  // :7   BlendFactor      (C#: BlendFactor)
  float blendMode;    // :8   BlendMode        (C#: BlendMode, enum BlendModes 0..4)
  float pairingMode;  // :9   PairingMode      (C#: Pairing, enum PairingModes 0=WrapAround/1=Adjust)
  float width;        // :10  Width            (C#: RangeWidth)
  float scatter;      // :11  Scatter          (C#: Scatter)
};

namespace detail {

// NOTE (low-risk, only reachable via fork[t-singular]): NaN fails both compares, so
// hlslSaturate(NaN)==NaN (not clamped) -- matches HLSL/Metal saturate()'s NaN pass-through, so a
// NaN `t` (resultCount==1 -> 0/0) propagates identically on both sides, no ref-vs-GPU risk.
inline float hlslSaturate(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

inline float hlslFrac(float x) { return x - std::floor(x); }  // HLSL frac()

inline float hlslSmoothstep01(float x) {  // HLSL smoothstep(0,1,x) (:90)
  float t = hlslSaturate(x);
  return t * t * (3.0f - 2.0f * t);
}

inline float smootherStep(float x) {  // SmootherStep() (:18-22)
  x = hlslSaturate(x);
  return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

inline float hash11(float p) {  // shared/hash-functions.hlsl :10-16
  p = hlslFrac(p * 0.1031f);
  p *= p + 33.33f;
  p *= p + p;
  return hlslFrac(p);
}

inline float lerpF(float a, float b, float t) { return a + t * (b - a); }  // HLSL lerp(a,b,t)

inline SW_PACKED3 lerpF3(SW_PACKED3 a, SW_PACKED3 b, float t) {
  return SW_PACKED3{lerpF(a.x, b.x, t), lerpF(a.y, b.y, t), lerpF(a.z, b.z, t)};
}

inline SW_FLOAT4 lerpF4(SW_FLOAT4 a, SW_FLOAT4 b, float t) {
  return SW_FLOAT4{lerpF(a.x, b.x, t), lerpF(a.y, b.y, t), lerpF(a.z, b.z, t), lerpF(a.w, b.w, t)};
}

// hlslQSlerp — quat-functions.hlsl :162-215, translated component-wise (no simd::dot/length
// reuse) so the arithmetic order matches the HLSL text exactly, not whatever order a vector
// intrinsic would pick.
inline SW_FLOAT4 hlslQSlerp(SW_FLOAT4 a, SW_FLOAT4 b, float t) {
  auto len4 = [](SW_FLOAT4 q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); };

  // :165-176 -- zero-length guards. BlendPoints hits this whenever PointsA/PointsB is read
  // out-of-bounds (see readPointOrZero() below): a zero-valued Point has Rotation=(0,0,0,0),
  // length 0, so this branch is a live, reachable path in this kernel, not just defensive code.
  float lenA = len4(a);
  if (lenA == 0.0f) {
    float lenB = len4(b);
    if (lenB == 0.0f) return SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // QUATERNION_IDENTITY
    return b;
  }
  if (len4(b) == 0.0f) return a;

  // :178
  float cosHalfAngle = a.w * b.w + (a.x * b.x + a.y * b.y + a.z * b.z);

  // :180-183
  if (cosHalfAngle >= 1.0f || cosHalfAngle <= -1.0f) return a;

  // :184-189 -- fold b onto the shorter arc (mutates a local copy, matching HLSL's `in` by-value b)
  float bx = b.x, by = b.y, bz = b.z, bw = b.w;
  if (cosHalfAngle < 0.0f) {
    bx = -bx;
    by = -by;
    bz = -bz;
    bw = -bw;
    cosHalfAngle = -cosHalfAngle;
  }

  // :191-207
  float blendA, blendB;
  if (cosHalfAngle < 0.99f) {
    float halfAngle = std::acos(cosHalfAngle);
    float sinHalfAngle = std::sin(halfAngle);
    float oneOverSinHalfAngle = 1.0f / sinHalfAngle;
    blendA = std::sin(halfAngle * (1.0f - t)) * oneOverSinHalfAngle;
    blendB = std::sin(halfAngle * t) * oneOverSinHalfAngle;
  } else {
    blendA = 1.0f - t;
    blendB = t;
  }

  // :209-214
  float rx = blendA * a.x + blendB * bx;
  float ry = blendA * a.y + blendB * by;
  float rz = blendA * a.z + blendB * bz;
  float rw = blendA * a.w + blendB * bw;
  float len = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
  if (len > 0.0f) {
    return SW_FLOAT4{rx / len, ry / len, rz / len, rw / len};
  }
  return SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
}

// readPointOrZero — models the D3D StructuredBuffer SRV contract: an out-of-range element read
// returns an all-zero value (out-of-range writes are silently discarded). This is a general
// HLSL/D3D guarantee, not sw-specific behavior — cited here (not read from any sw source) because
// PointsA[aIndex]/PointsB[bIndex] (:54-55) are indexed by raw i.x with NO clamp against
// countA/countB (see NOTED-QUIRK in blendPointsOne() below), so this path is genuinely reachable
// whenever resultCount doesn't equal both countA and countB.
inline SwPoint readPointOrZero(const SwPoint* buf, uint32_t count, uint32_t idx) {
  if (idx < count) return buf[idx];
  SwPoint zero{};  // Position/Scale=(0,0,0), FX1/FX2=0, Rotation/Color=(0,0,0,0)
  return zero;
}

}  // namespace detail

// blendPointsOne — one thread of `main()` (:24-106), operating on a single result index. Returns
// false if the HLSL thread would have returned early WITHOUT writing (:31-32, :50-51) — callers
// must leave `out` (== ResultPoints[idx]'s prior content) untouched in that case, like a GPU
// thread that never issues the write. `idx`/`resultCount` reproduce `i.x`/GetDimensions() (:26-27)
// so the `idx > resultCount` guard (:31-32) can be probed verbatim; mathvRefBlendPoints() below
// always calls with `idx < resultCount` (see AMBIGUITY note inline).
inline bool blendPointsOne(const SwPoint* pointsA, uint32_t countA, const SwPoint* pointsB,
                            uint32_t countB, uint32_t resultCount, uint32_t idx,
                            const BlendPointsParams& prm, SwPoint& out) {
  // :31-32 -- fork[guard-off-by-one] (blendpoints.metal / blendpoints_params.h NAMED-FORKS):
  // transcribed verbatim, HLSL guard is `>` NOT `>=` -- a real source off-by-one, only reachable
  // via a direct call (not mathvRefBlendPoints()'s loop). Not "fixed" -- the kernel's `>=` guard is.
  if (idx > resultCount) return false;

  // :34-35 -- aIndex/bIndex are raw i.x, NOT scaled to countA/countB. See NOTED-QUIRK below.
  uint32_t aIndex = idx;
  uint32_t bIndex = idx;

  // :37 -- HLSL `i.x / ((float)resultCount-1)`: cast-then-subtract (not `(float)(resultCount-1)`);
  // matters only when resultCount==0 (float(0)-1 == -1, vs. wrapping to UINT_MAX-1 if the
  // subtraction happened in uint first). resultCount==1 makes this 0/0 == NaN for idx==0 (not
  // specially guarded by the kernel; propagates into hash11(t) below whenever Scatter!=0).
  float t = static_cast<float>(idx) / (static_cast<float>(resultCount) - 1.0f);

  // :39-52 -- Adjust-mode gating (PairingMode>0.5 && countA!=countB). NOTED-QUIRK: despite
  // PairingMode==0 being named "WrapAround", NEITHER mode ever applies a modulo/wrap to
  // aIndex/bIndex — both stay == idx unconditionally (:34-35, :54-55 are untouched by this
  // branch). Direct algebra on the formula below: whenever resultCount >= max(countA,countB) (the
  // common real-world setup, e.g. resultCount==the larger count), floor(idx*resultCount/count)>=idx
  // for BOTH A and B, so `firstIx>=idx` ALWAYS and the `idx>firstIx` gate on :50-51 never fires —
  // i.e. in that configuration this whole branch computes two divisions and then never skips
  // anything. It only actually skips writes when resultCount is SMALLER than one of countA/countB.
  // Transcribed verbatim; not "fixed" to match what the enum name or source comment (:43-44)
  // implies the intent might have been.
  if (prm.pairingMode > 0.5f && countA != countB) {
    // :41 -- `uint maxCount = max(countA,countB);` is computed by the HLSL but never read anywhere
    // in the kernel body — dead value in the source; not modeled as a variable here (nothing uses
    // it).
    float firstIxAf =
        (static_cast<float>(aIndex) * static_cast<float>(resultCount)) / static_cast<float>(countA);
    float firstIxBf =
        (static_cast<float>(bIndex) * static_cast<float>(resultCount)) / static_cast<float>(countB);
    // UB GUARD: countA/countB==0 -> firstIxAf/Bf +-inf/NaN; (uint)non-finite is C++ UB -- route to 0.
    auto safeCastU32 = [](float v) -> uint32_t { return std::isfinite(v) ? static_cast<uint32_t>(v) : 0u; };
    uint32_t firstIxA = safeCastU32(firstIxAf);  // :45
    uint32_t firstIxB = safeCastU32(firstIxBf);  // :46
    uint32_t firstIx = firstIxA > firstIxB ? firstIxA : firstIxB;  // :48

    if (idx > firstIx) return false;  // :50-51 -- thread returns; out[idx] left untouched
  }

  // :54-55 -- NOTE: reads are raw-indexed by aIndex/bIndex==idx with NO clamp to countA/countB;
  // faithfully modeled via detail::readPointOrZero()'s D3D OOB=0 contract (documented there).
  SwPoint A = detail::readPointOrZero(pointsA, countA, aIndex);
  SwPoint B = detail::readPointOrZero(pointsB, countB, bIndex);

  // :57-88 -- blend factor f, selected by BlendMode threshold ladder.
  float f = 0.0f;
  if (prm.blendMode < 0.5f) {
    f = prm.blendFactor;  // :61 Mix
  } else if (prm.blendMode < 1.5f) {
    f = A.FX1;  // :65 UseA_F1
  } else if (prm.blendMode < 2.5f) {
    f = 1.0f - B.FX1;  // :69 UseB_F1
  } else if (prm.blendMode < 3.5f) {
    // :76 MixWithSlidingRange -- see https://www.desmos.com/calculator/zxs1fy06uh
    f = 1.0f - detail::hlslSaturate((t - prm.blendFactor) / prm.width - prm.blendFactor + 1.0f);
  } else {
    // :80-87 MixWithSlidingRangeSmooth -- fold BlendFactor%2, then SmootherStep. :84 `t = 1 - t`
    // mutates the FUNCTION-SCOPE t, still live at :92's hash11(t) -- written back to the SAME t
    // (an earlier revision used a throwaway `tt` copy that hid this from hash11(t): a transcription bug).
    float b = std::fmod(prm.blendFactor, 2.0f);  // :80
    if (b > 1.0f) {  // :81-85
      b = 2.0f - b;
      t = 1.0f - t;  // :84 -- writes back to the shared `t`; see hash11(t) at :92 below
    }
    f = 1.0f - detail::smootherStep(detail::hlslSaturate((t - b) / prm.width - b + 1.0f));  // :87
  }

  // :90-92 -- scatter jitter, weighted by a smoothstep falloff toward f==0.5 (center of the blend).
  float fallOffFromCenter = detail::hlslSmoothstep01(1.0f - std::fabs(f - 0.5f) * 2.0f);
  f += (detail::hash11(t) - 0.5f) * prm.scatter * fallOffFromCenter;

  // :94-97 -- noBlend collapse: if either side's Scale.x is NaN, snap f to a hard 0/1 instead of
  // blending.
  bool noBlend = std::isnan(A.Scale.x * B.Scale.x);
  f = noBlend ? (f < 0.5f ? 0.0f : 1.0f) : f;

  // :99-105 -- writes. NOTED-QUIRK: FX1 is written TWICE (:101 then :105) — the :101 lerp is dead,
  // fully overwritten by :105's raw `f`. Transcribed verbatim so a diff against a port that
  // "cleaned up" the dead write (and therefore emits lerp(A.FX1,B.FX1,f) instead of f) is caught.
  out.Scale = noBlend ? (f < 0.1f ? A.Scale : B.Scale) : detail::lerpF3(A.Scale, B.Scale, f);  // :99
  out.Rotation = detail::hlslQSlerp(A.Rotation, B.Rotation, f);                                // :100
  const float deadFX1 = detail::lerpF(A.FX1, B.FX1, f);  // :101 -- computed, then discarded by :105
  (void)deadFX1;
  out.FX2 = detail::lerpF(A.FX2, B.FX2, f);              // :102
  out.Color = detail::lerpF4(A.Color, B.Color, f);       // :103
  out.Position = detail::lerpF3(A.Position, B.Position, f);  // :104
  out.FX1 = f;                                            // :105 -- the value that actually survives
  return true;
}

// mathvRefBlendPoints — full-buffer CPU oracle (:24-32 dispatch loop over `resultCount` threads).
// Skipped indices (blendPointsOne returns false) are left at their PRIOR content in
// `resultPoints`, exactly mirroring an early-returning GPU thread that never issues its write.
inline void mathvRefBlendPoints(const SwPoint* pointsA, uint32_t countA, const SwPoint* pointsB,
                                 uint32_t countB, SwPoint* resultPoints, uint32_t resultCount,
                                 const BlendPointsParams& prm) {
  for (uint32_t i = 0; i < resultCount; ++i) {
    SwPoint out = resultPoints[i];
    if (blendPointsOne(pointsA, countA, pointsB, countB, resultCount, i, prm, out)) {
      resultPoints[i] = out;
    }
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back). All three cases pin Scatter=0
// so the hash11() term contributes exactly 0 regardless of hash11's actual value (0*finite==0),
// keeping the arithmetic hand-checkable without transcribing hash11's bit-twiddling by hand.
//
// 1) Mix (BlendMode=0), no gating, no noBlend, identity rotations. countA=countB=resultCount=3,
//    idx=1 -> t=1/(3-1)=0.5. f=BlendFactor=0.5 (:61; Scatter=0 keeps it at 0.5).
//    A: FX1=0.2,Scale=(1,1,1),FX2=0.3,Color=(1,0,0,1); B: FX1=0.9,Scale=(3,3,3),FX2=0.7,
//    Color=(0,1,0,1); both Rotation=id, Position A=(0,0,0)/B=(10,10,10). noBlend=isnan(1*3)=false.
//    -> Scale=(2,2,2), Rotation=id (cosHalfAngle=1>=1 -> qSlerp returns A, :180-183),
//    FX2=0.5, Color=(0.5,0.5,0,1), Position=(5,5,5). dead FX1 lerp=lerp(0.2,0.9,0.5)=0.55 but
//    FX1_out(final,:105)=f=0.5 -- DIFFERENT from 0.55, proving the double-write quirk survives
//    on raw `f`, not the lerped value.
//
// 2) MixWithSlidingRangeSmooth (BlendMode=4), b>1 mirror-fold (:81-85). countA=countB=
//    resultCount=11, idx=3 -> t=3/10=0.3. BlendFactor=3.5,Width=1,Scatter=0.
//    b=fmod(3.5,2)=1.5>1 -> b=2-1.5=0.5, tt=1-0.3=0.7. inner=saturate((0.7-0.5)/1-0.5+1)=0.7.
//    SmootherStep(0.7)=0.343*(0.7*-10.8+10)=0.343*2.44=0.83692 -> f=1-0.83692=0.16308 (verified
//    float32 via python above). A: Position=(0,0,0),Color=(0,0,0,0),FX2=0,Scale=(1,1,1),
//    Rotation=id; B: Position=(1,1,1),Color=(1,1,1,1),FX2=1,Scale=(1,1,1),Rotation=id (equal
//    Scale/Rotation on both sides sidesteps needing to re-derive those channels). Every lerp
//    target spans 0->1, so Position=Color=FX2=FX1_out=f=0.16308 directly.
//
// 3) OOB read (D3D SRV contract) + Adjust-mode gating dead-branch + qSlerp zero-quaternion
//    branch. countA=2,countB=5,resultCount=5,idx=4,PairingMode=1(Adjust),BlendMode=0,
//    BlendFactor=0.5,Scatter=0. aIndex=bIndex=4; A OOB (countA=2) -> zero Point.
//    Gating: firstIxA=floor(4*5/2)=10, firstIxB=floor(4*5/5)=4, firstIx=10; idx(4)>firstIx(10)
//    is false -> NOT skipped (confirms the dead-branch algebra above: resultCount==max(countA,
//    countB) here, so the gate can't fire). A.Rotation=(0,0,0,0) (zero quat from the OOB read);
//    qSlerp: length(A.Rotation)==0 && length(B.Rotation)!=0 -> returns B=id (:171, exercised for
//    real here, not just defensively). noBlend=isnan(0*B.Scale.x)=false. f=BlendFactor=0.5.
//    B: Position=(4,4,4),Color=(0,0,1,1),Scale=(2,2,2),FX2=0.8.
//    -> Scale=(1,1,1), Position=(2,2,2), Color=(0,0,0.5,0.5), FX2=0.4, FX1_out(final)=f=0.5.
inline bool mathvRefBlendPointsSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  {  // case 1 -- aIndex==bIndex==idx (:34-35), so pointsA/pointsB must be real
     // `countA`/`countB`-sized arrays with the meaningful point at index `idx`, not a bare
     // single-object pointer (a bare pointer here would make pointsA[idx] for idx!=0 read
     // adjacent stack/heap garbage -- exactly the class of bug this array shape guards against).
    SwPoint arrA[3]{}, arrB[3]{}, out{};
    SwPoint& a = arrA[1];
    SwPoint& b = arrB[1];
    a.Position = {0, 0, 0};
    a.FX1 = 0.2f;
    a.Rotation = {0, 0, 0, 1};
    a.Color = {1, 0, 0, 1};
    a.Scale = {1, 1, 1};
    a.FX2 = 0.3f;
    b.Position = {10, 10, 10};
    b.FX1 = 0.9f;
    b.Rotation = {0, 0, 0, 1};
    b.Color = {0, 1, 0, 1};
    b.Scale = {3, 3, 3};
    b.FX2 = 0.7f;
    BlendPointsParams p{0.5f, 0.0f, 0.0f, 1.0f, 0.0f};
    bool wrote = blendPointsOne(arrA, 3, arrB, 3, 3, 1, p, out);
    ok &= wrote;
    ok &= approxEq(out.Scale.x, 2.0f) && approxEq(out.Scale.y, 2.0f) && approxEq(out.Scale.z, 2.0f);
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, 0.0f) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, 1.0f);
    ok &= approxEq(out.FX2, 0.5f);
    ok &= approxEq(out.Color.x, 0.5f) && approxEq(out.Color.y, 0.5f) && approxEq(out.Color.z, 0.0f);
    ok &= approxEq(out.Position.x, 5.0f) && approxEq(out.Position.y, 5.0f) &&
          approxEq(out.Position.z, 5.0f);
    ok &= approxEq(out.FX1, 0.5f);  // NOT 0.55 -- proves the double-write quirk
  }
  {  // case 2 -- idx=3, so arrays must have >= 4 elements with the point at index 3.
    SwPoint arrA[11]{}, arrB[11]{}, out{};
    SwPoint& a = arrA[3];
    SwPoint& b = arrB[3];
    a.Position = {0, 0, 0};
    a.Rotation = {0, 0, 0, 1};
    a.Color = {0, 0, 0, 0};
    a.Scale = {1, 1, 1};
    a.FX2 = 0.0f;
    b.Position = {1, 1, 1};
    b.Rotation = {0, 0, 0, 1};
    b.Color = {1, 1, 1, 1};
    b.Scale = {1, 1, 1};
    b.FX2 = 1.0f;
    BlendPointsParams p{3.5f, 4.0f, 0.0f, 1.0f, 0.0f};
    bool wrote = blendPointsOne(arrA, 11, arrB, 11, 11, 3, p, out);
    ok &= wrote;
    const float fExpected = 0.16308f;
    ok &= approxEq(out.Position.x, fExpected) && approxEq(out.Position.y, fExpected) &&
          approxEq(out.Position.z, fExpected);
    ok &= approxEq(out.Color.x, fExpected) && approxEq(out.Color.w, fExpected);
    ok &= approxEq(out.FX2, fExpected);
    ok &= approxEq(out.FX1, fExpected);
    ok &= approxEq(out.Scale.x, 1.0f) && approxEq(out.Rotation.w, 1.0f);
  }
  {  // case 3 -- aIndex=4 >= countA=2, so arrA is genuinely never dereferenced there (any
     // 2-element array works); bIndex=4 < countB=5 IS a real in-bounds read, so arrB must have
     // 5 elements with the point at index 4.
    SwPoint arrA[2]{}, arrB[5]{}, out{};
    SwPoint& b = arrB[4];
    b.Position = {4, 4, 4};
    b.FX1 = 0.0f;
    b.Rotation = {0, 0, 0, 1};
    b.Color = {0, 0, 1, 1};
    b.Scale = {2, 2, 2};
    b.FX2 = 0.8f;
    BlendPointsParams p{0.5f, 0.0f, 1.0f, 1.0f, 0.0f};  // PairingMode=1 (Adjust)
    bool wrote = blendPointsOne(arrA, 2, arrB, 5, 5, 4, p, out);
    ok &= wrote;  // gate does NOT skip (see derivation: firstIx=10 >= idx=4)
    ok &= approxEq(out.Scale.x, 1.0f) && approxEq(out.Scale.y, 1.0f) && approxEq(out.Scale.z, 1.0f);
    ok &= approxEq(out.Position.x, 2.0f) && approxEq(out.Position.y, 2.0f) &&
          approxEq(out.Position.z, 2.0f);
    ok &= approxEq(out.Color.z, 0.5f) && approxEq(out.Color.w, 0.5f);
    ok &= approxEq(out.FX2, 0.4f);
    ok &= approxEq(out.FX1, 0.5f);
    // qSlerp zero-quaternion branch: A.Rotation length==0 -> result is exactly B.Rotation (id).
    ok &= approxEq(out.Rotation.x, 0.0f) && approxEq(out.Rotation.y, 0.0f) &&
          approxEq(out.Rotation.z, 0.0f) && approxEq(out.Rotation.w, 1.0f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
