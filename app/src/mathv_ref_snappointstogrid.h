#pragma once
// mathv_ref_snappointstogrid — CPU scalar oracle for the SnapPointsToGrid kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/_internal/SnapPointsToGrid.hlsl — NOT derived from sw shaders.
//   cbuffer Params(b0)    :7-18   (GridStretch/Amount/GridOffset/GridScale/Scatter/Mode/GainAndBias)
//   cbuffer IntParams(b1) :20-23  (StrengthFactor)
//   main()                :28-71 -> snapPointsToGridOne() / mathvRefSnapPointsToGrid() below,
//                                    every statement carries its own :NN back-reference.
// Every statement below carries its own :NN back-reference.
//
// Also reads SnapPointsToGrid.hlsl's own #include closure (opened only because the shader itself
// pulls them in — no sw shader file was opened to write this ref):
//   shared/hash-functions.hlsl :4 (_PRIME0), :102-113 (hash41u) -> detail::hash41u() below
//     (independently re-transcribed, same pattern as mathv_ref_addnoise.h's own local copy — no
//     shared oracle header exists for this tiny helper, so per-op duplication is precedent).
//   shared/quat-functions.hlsl :16 (`#define mod(x, y) ((x) - (y) * floor((x) / (y)))`) -> the
//     macro invoked at SnapPointsToGrid.hlsl:38. GLSL-style FLOORED mod, NOT HLSL's truncated
//     fmod/`%` — same NAMED FORK already documented in mathv_ref_wrappointposition.h (there for
//     wrappoints.metal's translation; here it's TiXL's own macro) -> detail::flooredMod1() below.
//   shared/bias-functions.hlsl :52-90 (the FLOAT4 overloads of GetBias/GetSchlickBias/
//     ApplyGainAndBias — SnapPointsToGrid.hlsl:61 calls the float4 overload via a `.xyzz` swizzle,
//     NOT the scalar overload at :26-49) -> detail::getBias()/getSchlickBiasVec4() below.
//   shared/point.hlsl :19-27 (struct Point; already ported host-side as SwPoint in
//     runtime/tixl_point.h, reused not re-derived). shared/noise-functions.hlsl is included by the
//     shader (line 2) but nothing in main() calls a noise function — dead include, not transcribed.
//
// Cross-checked against external/tixl/Operators/Lib/point/transform/SnapPointsToGrid.cs:10-49 —
// `Offset` (C#) is `GridOffset` (HLSL); `AmountFactor` (C#, enum StrengthFactors{None=0,F1=1,F2=2})
// is `StrengthFactor` (HLSL IntParams); `Mode` (C#, enum SnapModes{CenterDistance=0,
// CornersDistance=1,AxisCenterDistance=2,AxisEdgeDistance=3}) matches the :44-58 ladder 1:1.
// AMBIGUITY (informational only): C# names the Vector2 input `BiasAndGain` (bias first) but the
// HLSL field is `GainAndBias` and ApplyGainAndBias reads `.x` as gain, `.y` as bias — reversed word
// order; this ref follows the HLSL (gain=.x, bias=.y, :64-65). C# `UseWAsWeight`/`UseSelection` are
// NOT read anywhere in this shader — dead inputs from this kernel's perspective, not modeled.
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port
// (intentionally never opened while writing this header, per the Tier-L R-role isolation rule).
// Zero metal include, zero app/shaders/ reference, zero sw math helper.
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): every branch here (the Mode ladder, the
// g<0.5 branch in ApplyGainAndBias, saturate()'s implicit clamps) is a float compare on the GPU; a
// double ref could land on the opposite side of a branch boundary than the float GPU.
//
// AMBIGUITY (dispatch bounds): unlike WrapPointPosition.hlsl (guards `i.x >= numStructs`), this
// kernel's main() (:28-30) has NO bounds check at all — it unconditionally reads/writes
// `Points1[i.x]`/`ResultPoints[i.x]`. Since `[numthreads(64,1,1)]` rounds dispatch up to the next
// multiple of 64, over-dispatched threads touch past-count indices with zero defensive branch,
// relying entirely on caller over-allocation. mathvRefSnapPointsToGrid() below only ever calls
// snapPointsToGridOne() for `idx < count` (a host array has no "extra thread" concept); probing
// this edge is out of scope for this CPU ref.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// app/src/runtime/tixl_point.h is included, for the SwPoint host struct (data layout, not math).
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0)  (:7-18, HLSL cbuffer packing: a float3 rounds up to a 16B slot
//                                  unless a trailing scalar fits the leftover 4B — same rule
//                                  documented in mathv_ref_wrappointposition.h):
//   offset  0  float3 GridStretch   (:9)  -- slot0 lo
//   offset 12  float  Amount        (:10) -- slot0 hi (packs into GridStretch's float4 slot)
//   offset 16  float3 GridOffset    (:12) -- slot1 lo
//   offset 28  float  GridScale     (:13) -- slot1 hi
//   offset 32  float  Scatter       (:15) -- slot2 lo
//   offset 36  float  Mode          (:16) -- slot2 (enum SnapModes packed as float, cross-check
//                                             above; compared via <0.5/<1.5/<2.5/<3.5 thresholds)
//   offset 40  float2 GainAndBias   (:17) -- slot2 hi (8B, fits the remaining half of slot2
//                                             [32-47] without crossing the next 16B boundary)
// cbuffer IntParams : register(b1) (:20-23) -- StrengthFactor, a real HLSL `int` (not a
//   float-packed enum like Mode); compared with exact integer equality at :64-65 below.
struct SnapPointsToGridParams {
  float gridStretchX, gridStretchY, gridStretchZ;  // :9  GridStretch
  float amount;                                    // :10 Amount
  float gridOffsetX, gridOffsetY, gridOffsetZ;      // :12 GridOffset
  float gridScale;                                  // :13 GridScale
  float scatter;                                    // :15 Scatter
  float mode;  // :16 Mode (0=CenterDistance,1=CornersDistance,2=AxisCenterDistance,3=AxisEdgeDist.)
  float gainAndBiasX, gainAndBiasY;                 // :17 GainAndBias (.x=gain, .y=bias)
  int32_t strengthFactor;  // :22 StrengthFactor (0=None,1=F1,2=F2 — but the :64-65 ternary treats
                            // ANY non-0/non-1 value as "F2"; NOTED-QUIRK, transcribed below)
};

namespace detail {

inline float hlslSaturate(float x) {  // HLSL saturate(): clamp to [0,1] (:46,:50,:68 scalar uses).
  // Same divergence documented in mathv_ref_wrappointposition.h: DX saturate(NaN)=0 on real
  // hardware, this oracle returns NaN unchanged (neither x<0 nor x>1 is true for NaN). UNLIKE that
  // file's WrapPointPosition case (where the divergence was inert, only reachable via a dead
  // branch), here it IS load-bearing — see the ApplyGainAndBias NOTED-QUIRK below and self-check
  // case 3, which manufactures a real NaN through the ordinary (non-degenerate-input) math path.
  // AMBIGUITY: whether real Metal/DX hardware clamps this NaN to 0 or propagates it is unresolved
  // by this ref and should be probed on real hardware (S/X role) the same way the WrapPointPosition
  // isnan() guard was probed for fast-math-fold-to-false liveness.
  return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// bias-functions.hlsl:6-9 / :52-55 GetBias(bias,x) — IDENTICAL formula in both the scalar and the
// float4 overload (the float4 one just applies it component-wise), so one scalar helper transcribes
// both.
inline float getBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

// bias-functions.hlsl:57-61 GetSchlickBias(float4 x, float gain) — the VEC4 overload's per-component
// formula. NOTE this is the one SnapPointsToGrid.hlsl actually calls (via the float4
// ApplyGainAndBias path, :61); it is NOT the scalar overload GetSchlickBias(float g, float x)
// (:11-24), which has swapped ARGUMENT ORDER but an equivalent per-scalar `if(x<0.5)` branch —
// documented here only to flag the naming trap for anyone diffing the two overloads.
inline float getSchlickBiasVec4(float x, float gain) {
  return x < 0.5f ? getBias(gain, x * 2.0f) / 2.0f
                  : getBias(1.0f - gain, x * 2.0f - 1.0f) / 2.0f + 0.5f;
}

// bias-functions.hlsl:63-90 ApplyGainAndBias(float4 v4, float2 gainBias) — NOTED-QUIRK, the single
// most load-bearing fork in this file: the source computes a masked `result` (hiMask/loMask clamp
// values >=0.999f to 1 and <=0.00001f to 0, meant to dodge GetBias/GetSchlickBias's division
// blowing up at the extremes, :69-88) but its FINAL STATEMENT is `return v4;` (:89) — the UNCLAMPED
// value; `result` is dead code, never returned. Reads as an unambiguous TiXL hand-slip (the scalar
// overload at :26-49 gets this right, its early `if(value>0.9999) return 1; if(value<0.00001)
// return 0;` guards run BEFORE any division). This ref transcribes the BUGGY unclamped path
// verbatim ("TiXL 手滑也照抄") — it does NOT apply the apparently-intended clamp. Concretely:
// GetBias(bias,x) divides by `(1/bias-2)*(1-x)+1`, which is `Inf*0`(=NaN) when bias==0 and x==1
// simultaneously — a combo the scalar overload's early-return would dodge, but this vec4 path (the
// one SnapPointsToGrid actually calls, via :61's `.xyzz` swizzle) walks straight into. See
// self-check case 3 for a fully hand-derived NaN-producing instance. Since `result`/hiMask/loMask
// are provably dead for the value this kernel consumes, this ref skips computing them entirely —
// only the two-branch GetBias/GetSchlickBias ladder that determines the unclamped return survives.
inline void applyGainAndBiasVec4(float x, float y, float z, float gain, float bias, float& outX,
                                  float& outY, float& outZ) {
  // :64-65 -- float g = saturate(gainBias.x); float b = saturate(gainBias.y);
  const float g = hlslSaturate(gain);
  const float b = hlslSaturate(bias);
  float vx = x, vy = y, vz = z;
  if (g < 0.5f) {
    // :75-76 -- v4 = GetBias(b, v4); v4 = GetSchlickBias(v4, g);
    vx = getBias(b, vx);
    vy = getBias(b, vy);
    vz = getBias(b, vz);
    vx = getSchlickBiasVec4(vx, g);
    vy = getSchlickBiasVec4(vy, g);
    vz = getSchlickBiasVec4(vz, g);
  } else {
    // :80-81 -- v4 = GetSchlickBias(v4, g); v4 = GetBias(b, v4);
    vx = getSchlickBiasVec4(vx, g);
    vy = getSchlickBiasVec4(vy, g);
    vz = getSchlickBiasVec4(vz, g);
    vx = getBias(b, vx);
    vy = getBias(b, vy);
    vz = getBias(b, vz);
  }
  outX = vx;  // :89 return v4;  (unclamped — see NOTED-QUIRK above)
  outY = vy;
  outZ = vz;
}

// quat-functions.hlsl:16 `#define mod(x,y) ((x)-(y)*floor((x)/(y)))`, specialized to the y==1
// literal actually used at SnapPointsToGrid.hlsl:38. GLSL-style FLOORED mod (result always in
// [0,y) for y>0, for ANY finite x — NOT HLSL's truncated fmod/`%`, which would be sign-following).
inline float flooredMod1(float x) { return x - std::floor(x); }

// hash-functions.hlsl:102-113 hash41u(uint) — uint32_t arithmetic wraps mod 2^32 identically to
// HLSL's uint (defined behavior for unsigned overflow in C++), matching the AddNoise ref's
// independent transcription of the same TiXL function.
inline void hash41u(uint32_t x, float& ox, float& oy, float& oz, float& ow) {
  constexpr uint32_t kPrime0 = 13331u;      // hash-functions.hlsl:4 _PRIME0
  constexpr uint32_t kK = 1103515245u;      // hash-functions.hlsl:104 "GLIB C"
  x *= kPrime0;                              // :105
  x = ((x >> 8u) ^ x) * kK;                  // :106
  const uint32_t y = ((x >> 8u) ^ x) * kK;    // :107 (uses the :106-updated x)
  const uint32_t z = ((y >> 8u) ^ x) * kK;    // :108 (uses y and the SAME updated x, not raw)
  const uint32_t w = ((z >> 8u) ^ y) * kK;    // :109
  // :112 float(0xffffffffU) rounds to 2^32 exactly under IEEE754 round-to-nearest — same rounding
  // on host and GPU, no divergence.
  const float invMax = 1.0f / static_cast<float>(0xffffffffu);
  ox = static_cast<float>(x) * invMax;
  oy = static_cast<float>(y) * invMax;
  oz = static_cast<float>(z) * invMax;
  ow = static_cast<float>(w) * invMax;
}

}  // namespace detail

// snapPointsToGridOne — one thread of `main()` (:28-71), operating on a single point.
// `idx` reproduces `i.x` (:28) for the hash41u(i.x) call at :41; no `numStructs` guard exists in
// this kernel (see AMBIGUITY note in the file header) so there is nothing else to parametrize.
inline void snapPointsToGridOne(const SwPoint& in, SwPoint& out, uint32_t idx,
                                 const SnapPointsToGridParams& prm) {
  // :30 -- Point p = Points1[i.x];  (all non-Position fields pass through unchanged: this kernel
  // only ever assigns p.Position, then writes the whole point back at :70).
  out = in;

  // :32 -- float3 gridSize = GridScale * GridStretch;
  const float gridSizeX = prm.gridScale * prm.gridStretchX;
  const float gridSizeY = prm.gridScale * prm.gridStretchY;
  const float gridSizeZ = prm.gridScale * prm.gridStretchZ;

  // :33 -- float3 orgPosition = p.Position;  :35 -- float3 pos = orgPosition;  (pos/orgPosition are
  // the same value throughout main() — TiXL never mutates `pos` after this alias, so one set of
  // locals suffices).
  const float orgX = in.Position.x, orgY = in.Position.y, orgZ = in.Position.z;

  // :36 -- float3 normalizedPosition = pos / gridSize;
  const float normX = orgX / gridSizeX;
  const float normY = orgY / gridSizeY;
  const float normZ = orgZ / gridSizeZ;

  // :37 -- float3 normlizedOffsetPosition = normalizedPosition + 0.5 - GridOffset;  (TiXL's own
  // identifier is misspelled "normlized"; transcribed as a comment only, not as a C++ name.)
  const float noffX = normX + 0.5f - prm.gridOffsetX;
  const float noffY = normY + 0.5f - prm.gridOffsetY;
  const float noffZ = normZ + 0.5f - prm.gridOffsetZ;

  // :38 -- float3 signedFraction = (mod(normlizedOffsetPosition, 1) - 0.5) * 2;  NOTED-QUIRK:
  // FLOORED mod (detail::flooredMod1), not truncated fmod — see file-header note.
  const float sfX = (detail::flooredMod1(noffX) - 0.5f) * 2.0f;
  const float sfY = (detail::flooredMod1(noffY) - 0.5f) * 2.0f;
  const float sfZ = (detail::flooredMod1(noffZ) - 0.5f) * 2.0f;

  // :39 -- float3 centerPoint = pos - signedFraction * gridSize / 2;
  const float centerX = orgX - sfX * gridSizeX * 0.5f;
  const float centerY = orgY - sfY * gridSizeY * 0.5f;
  const float centerZ = orgZ - sfZ * gridSizeZ * 0.5f;

  // :41 -- float3 scatter = (hash41u(i.x) - 0.5) * Scatter;  NOTED-QUIRK: hash41u returns a float4;
  // assigning it to a float3 var is an implicit HLSL vector TRUNCATION to .xyz — the hash's .w lane
  // (computed here as `hw`) is discarded, never contributing to `scatter`.
  float hx, hy, hz, hw;
  detail::hash41u(idx, hx, hy, hz, hw);
  (void)hw;
  const float scatterX = (hx - 0.5f) * prm.scatter;
  const float scatterY = (hy - 0.5f) * prm.scatter;
  const float scatterZ = (hz - 0.5f) * prm.scatter;

  // :43-59 -- float3 snapAmount = 0; then the Mode ladder (float compares against half-integer
  // thresholds standing in for enum SnapModes: 0=CenterDistance, 1=CornersDistance,
  // 2=AxisCenterDistance, 3=AxisEdgeDistance — cross-checked SnapPointsToGrid.cs:44-49).
  // NOTED-QUIRK (mode-family asymmetry): Mode 0/1 (:46,:50) use ONLY `scatter.x` — a single scalar
  // — inside a vector-LENGTH ratio, BROADCAST to all 3 axes (radially symmetric, matching
  // "Center/Corners"). Mode 2/3 (:54,:58) use the FULL per-axis `scatter` vector inside a per-axis
  // abs() (axis-aligned, matching "AxisCenter/AxisEdge"). Not a bug — a real source asymmetry that
  // must not be "fixed" into uniform behavior.
  float snapX = 0.0f, snapY = 0.0f, snapZ = 0.0f;  // :43 default; ALSO the silent Mode>=3.5
                                                    // fallthrough (no branch covers it below).
  const float gridLenSq = gridSizeX * gridSizeX + gridSizeY * gridSizeY + gridSizeZ * gridSizeZ;
  const float gridLen = std::sqrt(gridLenSq);  // :46/:50 length(gridSize)
  const float sfScaledX = sfX * gridSizeX, sfScaledY = sfY * gridSizeY, sfScaledZ = sfZ * gridSizeZ;
  const float sfScaledLen =
      std::sqrt(sfScaledX * sfScaledX + sfScaledY * sfScaledY + sfScaledZ * sfScaledZ);
  // :46/:50 length(signedFraction * gridSize)
  if (prm.mode < 0.5f) {
    // :44-47 -- Mode 0 CenterDistance
    const float v = detail::hlslSaturate(sfScaledLen / gridLen + scatterX);
    snapX = snapY = snapZ = v;
  } else if (prm.mode < 1.5f) {
    // :48-51 -- Mode 1 CornersDistance
    const float v = 1.0f - detail::hlslSaturate(sfScaledLen / gridLen + scatterX);
    snapX = snapY = snapZ = v;
  } else if (prm.mode < 2.5f) {
    // :52-55 -- Mode 2 AxisCenterDistance
    snapX = std::fabs(sfX + scatterX);
    snapY = std::fabs(sfY + scatterY);
    snapZ = std::fabs(sfZ + scatterZ);
  } else if (prm.mode < 3.5f) {
    // :56-59 -- Mode 3 AxisEdgeDistance
    snapX = 1.0f - std::fabs(sfX + scatterX);
    snapY = 1.0f - std::fabs(sfY + scatterY);
    snapZ = 1.0f - std::fabs(sfZ + scatterZ);
  }
  // else (Mode >= 3.5): snapAmount stays (0,0,0) -- the :43 initializer, untouched.

  // :61 -- float3 biasedSnap = ApplyGainAndBias(snapAmount.xyzz, GainAndBias).xyz;  The `.xyzz`
  // swizzle duplicates snapZ into the (unused-after-.xyz) w lane, so this ref only computes x/y/z.
  // See detail::applyGainAndBiasVec4()'s NOTED-QUIRK doc for the `return v4;`-not-`result` bug this
  // call inherits.
  float biasedX, biasedY, biasedZ;
  detail::applyGainAndBiasVec4(snapX, snapY, snapZ, prm.gainAndBiasX, prm.gainAndBiasY, biasedX,
                                biasedY, biasedZ);

  // :63-66 -- float strength = Amount * (StrengthFactor==0 ? 1 : (StrengthFactor==1) ? p.FX1
  //                                                                                   : p.FX2);
  // NOTED-QUIRK: any StrengthFactor other than exactly 0 or 1 (including negatives or >2) falls
  // into the p.FX2 arm — transcribed verbatim, not narrowed to the C# enum's {0,1,2} domain.
  const float weight =
      (prm.strengthFactor == 0) ? 1.0f : (prm.strengthFactor == 1) ? in.FX1 : in.FX2;
  const float strength = prm.amount * weight;

  // :68 -- float3 ff = (1 - saturate(biasedSnap - Amount * 2 + 1)) * strength;
  const float ffX = (1.0f - detail::hlslSaturate(biasedX - prm.amount * 2.0f + 1.0f)) * strength;
  const float ffY = (1.0f - detail::hlslSaturate(biasedY - prm.amount * 2.0f + 1.0f)) * strength;
  const float ffZ = (1.0f - detail::hlslSaturate(biasedZ - prm.amount * 2.0f + 1.0f)) * strength;

  // :69 -- p.Position = lerp(orgPosition, centerPoint, ff);  HLSL lerp(a,b,t) = a + t*(b-a),
  // componentwise (ff is a float3).
  out.Position.x = orgX + ffX * (centerX - orgX);
  out.Position.y = orgY + ffY * (centerY - orgY);
  out.Position.z = orgZ + ffZ * (centerZ - orgZ);
  // :70 -- ResultPoints[i.x] = p;  (Rotation/Color/Scale/FX1/FX2 already carried over via the
  // `out = in` copy at the top of this function -- this kernel never touches them.)
}

// mathvRefSnapPointsToGrid — full-buffer CPU oracle (:28-30 dispatch loop over `count` points).
// `in` and `out` may alias the same array: every read of `in[i]`'s fields happens before the single
// write to `out[i].Position`, so in-place operation is safe even if `in == out`.
inline void mathvRefSnapPointsToGrid(const SwPoint* in, SwPoint* out, size_t count,
                                      const SnapPointsToGridParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    snapPointsToGridOne(in[i], out[i], static_cast<uint32_t>(i), prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// 1) Mode=0 (CenterDistance), GridScale=1/GridStretch=(1,1,1)->gridSize=(1,1,1), GridOffset=0,
//    Scatter=0, GainAndBias=(0.5,0.5) [g=b=0.5 -- GetBias/GetSchlickBias both reduce to IDENTITY at
//    0.5: GetBias(0.5,y)=y/((2-2)*(1-y)+1)=y/1=y], Amount=1, StrengthFactor=0, Position=(1.3,0,0):
//    normOffsetPos=(1.8,0.5,0.5); flooredMod1(1.8)=0.8, flooredMod1(0.5)=0.5;
//    signedFraction=((0.8-0.5)*2,0,0)=(0.6,0,0). centerPoint=(1.3-0.6*0.5,0,0)=(1.0,0,0).
//    snapAmount(mode0): len(sf*gridSize)=0.6; len(gridSize)=sqrt(3)=1.732051;
//      v=saturate(0.6/1.732051)=0.346410; snapAmount=(v,v,v)=biasedSnap (identity bias).
//    strength=1*1=1. ff=(1-saturate(biasedSnap-2+1))*1=(1-saturate(-0.65359))*1=(1-0)*1=1 (all
//      lanes, since Amount=1 always fully engages regardless of biasedSnap).
//    Position_out = lerp(org,center,1) = center = (1.0,0,0).
//
// 2) Mode=2 (AxisCenterDistance), same grid as (1), Scatter=0, GainAndBias=(0.5,0.5) (identity),
//    Amount=0.3, StrengthFactor=0, Position=(1.3,1.7,0.2):
//    normOffsetPos=(1.8,2.2,0.7); flooredMod1: 1.8->0.8, 2.2->0.2, 0.7->0.7.
//    signedFraction=(0.6,-0.6,0.4). centerPoint=(1.3-0.3,1.7+0.3,0.2-0.2)=(1.0,2.0,0.0).
//    snapAmount(mode2,per-axis abs,scatter=0)=(0.6,0.6,0.4)=biasedSnap (identity bias).
//    strength=0.3*1=0.3. ff_n=(1-saturate(biased_n-0.6+1))*0.3:
//      ff_x=(1-saturate(1.0))*0.3=0; ff_y=0 (same magnitude); ff_z=(1-saturate(0.8))*0.3=0.06.
//    Position_out=(1.3+0,1.7+0,0.2+0.06*(-0.2))=(1.3,1.7,0.188) -- x/y UNCHANGED, z partially
//      snapped: demonstrates Mode 2's per-axis independence.
//
// 3) ApplyGainAndBias `return v4` BUG made concrete: Mode=1 (CornersDistance), same grid as (1),
//    Position=(0,0,0), Scatter=0, GainAndBias=(0,0) [degenerate-but-in-range post-saturate], Amount=1,
//    StrengthFactor=0: normOffsetPos=(0.5,0.5,0.5); flooredMod1(0.5)=0.5; signedFraction=(0,0,0)
//    exactly -> centerPoint == orgPosition == (0,0,0). snapAmount(mode1): len(0)/len(gridSize)=0;
//    v=1-saturate(0)=1; snapAmount=(1,1,1).
//    applyGainAndBiasVec4(1,1,1,gain=0,bias=0): g=0<0.5 -> vx=getBias(b=0,x=1)
//      = 1/((1/0-2)*(1-1)+1) = 1/((Inf-2)*0+1) = 1/(NaN+1) = NaN  [Inf*0=NaN per IEEE754].
//      vx=getSchlickBiasVec4(NaN,gain=0): NaN<0.5 is FALSE -> else arm getBias(1,NaN)=NaN/(NaN+1)
//      =NaN; /2+0.5=NaN. biasedSnap=(NaN,NaN,NaN) all lanes.
//    strength=1. ff=(1-saturate(NaN-2+1))*1=(1-NaN)*1=NaN (this ref's hlslSaturate(NaN)=NaN, see
//      its AMBIGUITY note). Position_out=lerp(org,center,NaN)=org+NaN*(center-org)=org+NaN*0=NaN
//      (center==org exactly, but IEEE754 NaN*0=NaN, not 0 -- still propagates).
//    Position_out=(NaN,NaN,NaN) even though the "intended" (unbugged) clamp would have forced
//      biasedSnap=1 (hiMask fires >=0.999) -> ordinary ff=0 -> no movement. Concrete,
//      non-degenerate-input proof the `return v4;` bug is reachable through ordinary parameters
//      (Mode 1 at grid center + zero gain/bias), not a theoretical dead-code curiosity.
inline bool mathvRefSnapPointsToGridSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  {  // case 1
    SwPoint in{}, out{};
    in.Position = {1.3f, 0.0f, 0.0f};
    SnapPointsToGridParams p{1, 1, 1, 1.0f, 0, 0, 0, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0};
    snapPointsToGridOne(in, out, 0, p);
    ok &= approxEq(out.Position.x, 1.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f);
  }
  {  // case 2
    SwPoint in{}, out{};
    in.Position = {1.3f, 1.7f, 0.2f};
    SnapPointsToGridParams p{1, 1, 1, 0.3f, 0, 0, 0, 1.0f, 0.0f, 2.0f, 0.5f, 0.5f, 0};
    snapPointsToGridOne(in, out, 0, p);
    ok &= approxEq(out.Position.x, 1.3f) && approxEq(out.Position.y, 1.7f) &&
          approxEq(out.Position.z, 0.188f);
  }
  {  // case 3
    SwPoint in{}, out{};
    in.Position = {0.0f, 0.0f, 0.0f};
    SnapPointsToGridParams p{1, 1, 1, 1.0f, 0, 0, 0, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0};
    snapPointsToGridOne(in, out, 0, p);
    ok &= std::isnan(out.Position.x) && std::isnan(out.Position.y) && std::isnan(out.Position.z);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
