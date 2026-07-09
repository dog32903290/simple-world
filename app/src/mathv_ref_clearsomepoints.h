#pragma once
// mathv_ref_clearsomepoints — CPU scalar oracle for the ClearSomePoints kernel.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/ClearSomePoints.hlsl — NOT derived from sw's own
// Metal port of this kernel (intentionally not opened by the R role writing this header; the whole
// point of mathv is to fuzz the CPU ref against the *independent* MSL kernel, so any accidental peek
// would self-certify a bug instead of catching it).
//   cbuffer Params(b0) :6-9    (Ratio)
//   cbuffer Params(b1) :11-16 (Seed, Repeat, Resolution)
//   Mod()              :21-28 -> hlslMod() below
//   main() body        :30-45 -> mathvRefClearSomePoints() / clearSomePointsOne() below,
//                                 every statement carries its own :NN back-reference.
// Cross-checked against the shared hash helper at
// external/tixl/Operators/Lib/Assets/shaders/shared/hash-functions.hlsl:4-5 (_PRIME0/_PRIME1 defines)
// and :115-123 (hash11u) -- transcribed verbatim into hash11u() below -- and the shared struct
// layout at external/tixl/Operators/Lib/Assets/shaders/shared/point.hlsl:19-27 (Point: Position/
// FX1/Rotation/Color/Scale/FX2).
//
// PROVENANCE discipline (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5):
// this file is a P5-safe oracle, not a P5 self-consistent trap. Zero metal include, zero
// app/shaders/ reference, zero sw math helper.
//
// FLOAT (not double) on purpose (MATH_VERIFY_WORKFLOW.md §2.3 branchy-class rule): `hash <= Ratio`
// is a float comparison on the GPU and the uint pipeline feeding it wraps at 32 bits; a double CPU
// ref would neither reproduce the 32-bit wraparound nor risk landing on the opposite side of the
// comparison for the same nominal input.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency). Only
// app/src/runtime/tixl_point.h is included, for the SwPoint host struct (data layout, not math).
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sw {
namespace mathv_ref {

// --- cbuffer field table -----------------------------------------------------------------------
// cbuffer Params : register(b0)  (:6-9)
//   offset 0  float  Ratio       (:8)  -- threshold; a point clears iff hash11u(pointU) <= Ratio
// cbuffer Params : register(b1)  (:11-16)
//   offset 0  int    Seed        (:13) -- mixed into the hash pipeline (:36)
//   offset 4  int    Repeat      (:14) -- outer modulo divisor (0 -> 999999999, :36)
//   offset 8  int    Resolution  (:15) -- inner Mod() divisor; groups consecutive point indices
//                                         into the same random draw (:36)
struct ClearSomePointsParams {
  float ratio;          // :8  Ratio
  int32_t seed;         // :13 Seed
  int32_t repeat;       // :14 Repeat
  int32_t resolution;   // :15 Resolution
};

namespace detail {
// _PRIME0/_PRIME1 -- hash-functions.hlsl:4-5 (#define _PRIME0 13331U / _PRIME1 1345777U).
constexpr uint32_t kPrime0 = 13331u;
constexpr uint32_t kPrime1 = 1345777u;

inline float hlslNan() {  // HLSL `NAN` (:42): a compiler-builtin compile-time IEEE754 quiet NaN
  // literal, distinct from a runtime sqrt(-1)/0-div NaN elsewhere in this codebase's oracles, but
  // numerically the same NaN class (quiet, any payload).
  return std::numeric_limits<float>::quiet_NaN();
}

// hlslMod — transcribes `inline int Mod(int val, int repeat)` (:21-28) verbatim:
//   int x = val % repeat;
//   if (x < 0) x = repeat + x;
//   return x;
// C++'s `%` for int is truncating (result has the dividend's sign), matching HLSL/DXIL's integer
// modulo exactly for any nonzero `repeat` -- no divergence to model there.
//
// NOTED-QUIRK / AMBIGUITY (repeat==0, i.e. Resolution==0 at the call site :36): the HLSL text has
// NO caller-side guard against Resolution==0 here (contrast Repeat, which IS guarded two lines
// later by the `Repeat == 0 ? 999999999 : Repeat` ternary, :36). `val % 0` is an integer
// divide-by-zero.
//   - Per the D3D11 functional spec, HLSL/DXIL integer div-by-zero is DEFINED (not driver-UB):
//     both the quotient and remainder are 0xFFFFFFFF (all bits set), i.e. int32_t(-1). This oracle
//     pins THAT convention (`val % 0` -> -1) as the HLSL-domain-of-truth semantic, since this file
//     transcribes HLSL text, not Metal text.
//   - Metal Shading Language (the sw kernel's actual compile target) documents integer div/mod by
//     zero as UNDEFINED per the C++14 base spec MSL inherits -- so the ported .metal kernel's real
//     behavior at Resolution==0 is not guaranteed to match this pinned value. A fuzz mismatch
//     specifically at Resolution==0 is therefore an EXPECTED divergence class, not necessarily a
//     kernel bug -- route to X/S per MATH_VERIFY_WORKFLOW §7 "HLSL 本體歧義" rather than chasing it
//     as a plain mismatch. Recommend the D-role fuzz domain exclude Resolution==0 from the main
//     random/special-value sweep and instead carry it as a single documented xfail probe.
//   - C++ itself (unlike HLSL) has UB for `int % 0` (traps with SIGFPE on x86) -- the explicit
//     `repeat == 0` branch below exists so this oracle never actually executes that trap.
inline int32_t hlslMod(int32_t val, int32_t repeat) {
  int32_t x = (repeat == 0) ? -1 : (val % repeat);  // :23, with the div-by-zero pin above
  if (x < 0) x = repeat + x;                         // :24-25
  return x;                                          // :27
}

// computePointU — transcribes the :36 pointU expression:
//   uint pointU = ((i.x - Mod(i.x, Resolution) + 1) * _PRIME0 + Seed * _PRIME1)
//                 % (Repeat == 0 ? 999999999 : Repeat);
//
// TYPE-PROMOTION NOTE (transcribed faithfully, not simplified away): HLSL/DXIL binary ops mixing
// `int` and `uint` operands implicitly convert the `int` operand to `uint` by bit-reinterpretation
// (two's-complement bit pattern preserved -- the same rule C/C++ use for unsigned conversion)
// BEFORE performing the op in uint (mod-2^32 wraparound) domain. This expression mixes types at
// every step:
//   i.x (uint) - Mod(...) (int)         -> uint domain, wraps mod 2^32
//   (...) * _PRIME0 (uint literal)       -> uint domain
//   Seed (int) * _PRIME1 (uint literal)  -> uint domain
//   uint + uint                          -> uint domain
//   uint % (int ternary)                 -> ternary itself is int/int -> int, then promoted to
//                                            uint for the final modulo
// This oracle models the ENTIRE pipeline in uint32_t so C++'s unsigned wraparound (well-defined,
// unlike signed overflow) reproduces HLSL's uint arithmetic bit-for-bit.
inline uint32_t computePointU(uint32_t ix, int32_t seed, int32_t resolution, int32_t repeat) {
  const int32_t modVal = hlslMod(static_cast<int32_t>(ix), resolution);  // :36 inner Mod(i.x, Resolution)
  const uint32_t step1 = ix - static_cast<uint32_t>(modVal) + 1u;        // :36 (i.x - Mod(...) + 1)
  const uint32_t step2 = step1 * kPrime0;                                // :36 (* _PRIME0)
  const uint32_t seedTerm = static_cast<uint32_t>(seed) * kPrime1;       // :36 (Seed * _PRIME1)
  const uint32_t sum = step2 + seedTerm;                                 // :36 (+ ...)

  const int32_t divisorSigned = (repeat == 0) ? 999999999 : repeat;      // :36 ternary
  // NOTED-QUIRK: if Repeat is negative, HLSL bit-reinterprets it to a HUGE uint (~4.29e9 - |Repeat|)
  // before the modulo -- Repeat<0 does NOT behave like "no repeat"/"disabled", it silently becomes
  // an enormous modulus (in practice near-never triggering the outer wrap for realistic pointU
  // sums). Transcribed verbatim; not special-cased.
  const uint32_t divisor = static_cast<uint32_t>(divisorSigned);
  // divisor is never 0 here: repeat==0 is routed to 999999999 above; any other int32_t value's
  // bit-reinterpretation as uint32_t is 0 only when the value itself is 0, which is excluded.
  return sum % divisor;
}
}  // namespace detail

// clearSomePointsOne — one thread of `main()` (:30-45), operating on a single point.
// `idx` reproduces the dispatch's `i.x` (:36, used both as the pointU seed and the buffer index).
// Unlike WrapPointPosition.hlsl, this kernel has NO `idx >= pointCount` guard branch to transcribe:
// `SourcePoints.GetDimensions(pointCount, stride)` (:33-34) is called but pointCount/stride are
// never referenced again in the kernel body -- dead locals, noted for completeness, not modeled.
inline void clearSomePointsOne(const SwPoint& in, SwPoint& out, uint32_t idx,
                                const ClearSomePointsParams& prm) {
  // :39 -- Point p = SourcePoints[i.x];  (full copy-through baseline; only Scale may be overwritten)
  out = in;

  // :36 -- pointU pipeline (see detail::computePointU doc above)
  const uint32_t pointU =
      detail::computePointU(idx, prm.seed, prm.resolution, prm.repeat);

  // :37 -- float hash = hash11u(pointU);  (hash-functions.hlsl :115-123, transcribed verbatim)
  //   x *= _PRIME0; x = ((x>>8)^x)*k; x = ((x>>8)^x)*k; return float(x) * (1/float(0xffffffff));
  // Every step is finite uint32/float arithmetic -- hash11u can never produce NaN, and its output
  // range is the CLOSED interval [0, 1]: x=0 gives exactly 0.0 (0 through every step), and the
  // maximum representable float(uint32_t) rounds to exactly 2^32, so float(x)*2^-32 tops out at
  // exactly 1.0 for x near UINT32_MAX (the final multiply is by an exact power of two, so it
  // introduces no additional rounding). See self-check cases 4/5 below, which rely on this bound
  // to avoid hand-computing the hash mix itself.
  uint32_t x = pointU;
  x *= detail::kPrime0;                 // :118
  x = ((x >> 8u) ^ x) * 1103515245u;     // :119 (k, "GLIB C")
  x = ((x >> 8u) ^ x) * 1103515245u;     // :120
  const float hash =
      static_cast<float>(x) * (1.0f / static_cast<float>(0xffffffffu));  // :122

  // :40-43 -- if (hash <= Ratio) { p.Scale = NAN; }
  // `<=` is an IEEE float comparison: false whenever Ratio is NaN (since hash itself is always
  // finite/non-NaN per the bound above), so a NaN Ratio can only ever SUPPRESS clearing, never
  // force it -- the reverse of what a careless "NaN propagates" assumption would suggest.
  if (hash <= prm.ratio) {
    // `p.Scale = NAN;` (:42) assigns a scalar to a float3 lvalue -- HLSL broadcasts the scalar to
    // all three components (Scale.x = Scale.y = Scale.z = NAN), matching SwPoint::Scale's
    // packed_float3 layout (tixl_point.h @48).
    out.Scale.x = out.Scale.y = out.Scale.z = detail::hlslNan();
  }
  // :44 -- ResultPoints[i.x] = p;  Position/FX1/Rotation/Color/FX2 are untouched by this whole
  // kernel (only Scale is ever conditionally written); `out = in` above already carries them.
}

// mathvRefClearSomePoints — full-buffer CPU oracle (:30 dispatch loop over `count` points).
// `in` and `out` may alias the same array: the loop reads `in[i]` (via clearSomePointsOne's
// `out = in` copy) before writing `out[i]`, safe even if `in == out`.
inline void mathvRefClearSomePoints(const SwPoint* in, SwPoint* out, size_t count,
                                     const ClearSomePointsParams& prm) {
  for (size_t i = 0; i < count; ++i) {
    clearSomePointsOne(in[i], out[i], static_cast<uint32_t>(i), prm);
  }
}

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// 1) hlslMod() negative-input correction, in isolation (no hash involved):
//    Mod(-3, 5): x = -3 % 5 = -3 (C truncating mod keeps the dividend's sign); x<0 -> x = 5+(-3) = 2.
//
// 2) computePointU() uint pipeline, a case small enough to trace without touching hash11u:
//    idx=0, Seed=0, Resolution=5, Repeat=100.
//    Mod(0,5) = 0%5 = 0 (not negative, no correction) -> modVal=0.
//    step1 = 0 - 0 + 1 = 1.  step2 = 1*13331 = 13331.  seedTerm = 0*1345777 = 0.  sum = 13331.
//    divisor = 100 (Repeat!=0).  pointU = 13331 % 100 = 31  (133*100=13300, 13331-13300=31).
//
// 3) Repeat==1 collapse + hash11u(0)=0 exactly (a QUIRK worth pinning): whenever Repeat==1, the
//    divisor is 1 and ANY sum % 1 == 0, so pointU==0 regardless of idx/Seed/Resolution. Then
//    hash11u(0): x=0; x*=13331 -> 0; x=((0>>8)^0)*k -> 0; x=((0>>8)^0)*k -> 0 again;
//    return float(0)*(1/float(0xffffffff)) = 0.0 exactly. With Ratio=0.0: hash<=Ratio is
//    0.0<=0.0 -> true (the `<=` is inclusive) -> point clears. Uses idx=7, Seed=5, Resolution=3
//    to demonstrate those three inputs are irrelevant once Repeat==1.
//
// 4) Guaranteed-clear bound (Ratio above hash11u's provable max of 1.0, so the exact hash value
//    never has to be computed): Ratio=2.0 -> hash<=2.0 is true for ANY hash in [0,1] -> the point
//    always clears. Verifies Scale becomes (NaN,NaN,NaN) while Position/Rotation/Color/FX1/FX2
//    pass through unchanged from the input.
//
// 5) Guaranteed-not-clear bound (Ratio below hash11u's provable min of 0.0): Ratio=-1.0 ->
//    hash<=-1.0 is false for ANY hash in [0,1] -> the point never clears. Verifies the ENTIRE
//    point (including Scale) passes through unchanged.
inline bool mathvRefClearSomePointsSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  {  // case 1: hlslMod negative-input correction
    ok &= (detail::hlslMod(-3, 5) == 2);
  }
  {  // case 2: computePointU uint pipeline
    ok &= (detail::computePointU(0u, /*seed=*/0, /*resolution=*/5, /*repeat=*/100) == 31u);
  }
  {  // case 3: Repeat==1 collapse -> pointU==0 -> hash11u(0)==0.0 -> Ratio=0.0 clears (inclusive <=)
    SwPoint in{}, out{};
    in.Position = {1.0f, 2.0f, 3.0f};
    in.Scale = {1.0f, 1.0f, 1.0f};
    ClearSomePointsParams p{/*ratio=*/0.0f, /*seed=*/5, /*repeat=*/1, /*resolution=*/3};
    clearSomePointsOne(in, out, /*idx=*/7u, p);
    ok &= std::isnan(out.Scale.x) && std::isnan(out.Scale.y) && std::isnan(out.Scale.z);
    ok &= approxEq(out.Position.x, 1.0f) && approxEq(out.Position.y, 2.0f) &&
          approxEq(out.Position.z, 3.0f);
  }
  {  // case 4: guaranteed-clear bound (Ratio above provable hash max)
    SwPoint in{}, out{};
    in.Position = {4.0f, 5.0f, 6.0f};
    in.Rotation = {0.1f, 0.2f, 0.3f, 0.4f};
    in.Color = {0.5f, 0.6f, 0.7f, 0.8f};
    in.Scale = {2.0f, 3.0f, 4.0f};
    in.FX1 = 9.0f;
    in.FX2 = -9.0f;
    ClearSomePointsParams p{/*ratio=*/2.0f, /*seed=*/123, /*repeat=*/17, /*resolution=*/4};
    clearSomePointsOne(in, out, /*idx=*/42u, p);
    ok &= std::isnan(out.Scale.x) && std::isnan(out.Scale.y) && std::isnan(out.Scale.z);
    ok &= approxEq(out.Position.x, 4.0f) && approxEq(out.Position.y, 5.0f) &&
          approxEq(out.Position.z, 6.0f);
    ok &= approxEq(out.Rotation.x, 0.1f) && approxEq(out.Rotation.w, 0.4f);
    ok &= approxEq(out.Color.x, 0.5f) && approxEq(out.Color.w, 0.8f);
    ok &= approxEq(out.FX1, 9.0f) && approxEq(out.FX2, -9.0f);
  }
  {  // case 5: guaranteed-not-clear bound (Ratio below provable hash min)
    SwPoint in{}, out{};
    in.Position = {7.0f, 8.0f, 9.0f};
    in.Scale = {2.0f, 3.0f, 4.0f};
    ClearSomePointsParams p{/*ratio=*/-1.0f, /*seed=*/-5, /*repeat=*/0, /*resolution=*/9};
    clearSomePointsOne(in, out, /*idx=*/1000u, p);
    ok &= approxEq(out.Scale.x, 2.0f) && approxEq(out.Scale.y, 3.0f) && approxEq(out.Scale.z, 4.0f);
    ok &= approxEq(out.Position.x, 7.0f) && approxEq(out.Position.y, 8.0f) &&
          approxEq(out.Position.z, 9.0f);
  }

  return ok;
}

}  // namespace mathv_ref
}  // namespace sw
