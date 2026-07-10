#pragma once
// mathv_ref_gridwalkpoints — CPU scalar oracle for TiXL points/sim/grid-walk-points (points island;
// owner UNRESOLVED, see runtime/gridwalkpoints_params.h header note — an orphaned compute shader,
// no owning .t3 found in the full external/tixl/Operators/Lib tree).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/sim/grid-walk-points.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/gridwalkpoints.metal intentionally never opened while writing this file). Helper
// closures (hash11/qFromAngleAxis) are transcribed fresh from their shared TiXL headers; qRotateVec3
// is reused from mathv_ref_shared_quat.h (explicitly designed for cross-op reuse, §1.4):
//   shared/hash-functions.hlsl :9-15 (hash11)
//   shared/quat-functions.hlsl :45-49 (qRotateVec3), :65-70 (qFromAngleAxis)
//   main() body :32-66
//
// DEAD CODE (provably unreachable output, transcribed HERE as a documentation note, NOT modeled):
//   :56 `newLocalPosition = clamp(numStructs, 0, GridSize)` -- reassigns a local that is NEVER READ
//     afterward (the branch only subsequently reads `hash`/`axisAndAngle`, never newLocalPosition
//     again); a genuine dead store in the TiXL source itself.
//   :58-61 `uint r = ...; float3 axis = r==0 ? ... : ...;` -- `axis` is computed but never used
//     anywhere (the branch's actual output uses `axisAndAngle.xyz`/`.w`, an INDEPENDENT computation
//     from a different hash-derived index (:62), not `axis`/`r`). Both are dead stores.
//
// `mod(a,b)` (:47, float3 overload) and the scalar `%` operator (:50, `... * 421 % 1231`) are BOTH
// FLOORED in this file (`mod(a,b) = a - b*floor(a/b)`) -- NOT an assumption, HLSL has no builtin
// `mod()` (only `%`/`fmod()`), so this .hlsl's `mod()` call resolves to a project-wide shared helper
// this single-file transpile can't see; but HLSL's *spec'd* `%` on floats is TRUNCATED (matches C's
// `fmod`, sign follows dividend) -- a genuine spec-vs-actual-source-behavior gap this ref cannot
// resolve from the HLSL text alone (AMBIGUITY, same class as updatechunksizes' int-mod note). This
// ref implements the HLSL-SPEC form (truncated, via C++'s built-in `%`/`std::fmod`) and the fuzz TU
// deliberately keeps the `mod(a,b)`/`%` DIVIDENDS non-negative (Position/GridOffset/Seed domains
// chosen so `Position-GridOffset` and `Seed+i.x+Position.x+y+z` stay >=0) -- floored and truncated
// mod are IDENTICAL whenever the dividend is non-negative and divisor is positive, so this
// restriction makes the spec-vs-actual gap unobservable without asserting which one is "right".
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): the newLocalPosition boundary checks
// (:51-53) and `TriggerTurn > 0.5` are float comparisons on the GPU; a double ref could land on a
// different branch than the float GPU for the same nominal input. Classified TRANSCENDENTAL overall
// (qRotateVec3/normalize/hash11 chain). The WRAPPED branch's axisAngles[] table-index pick (:62) is
// NOT asserted via mathv_compare.h's built-in exemption channel (§2 2b) -- that channel caps the
// global exempt fraction at 1%, sized for a residual tail, but hash11's repeated squaring (:64-66)
// amplifies ordinary fast-math ULP drift in the :50 input sum into a ~7% GPU-vs-CPU disagreement
// rate on WHICH table row gets picked (measured, see gridWalkPointsHash() below) -- far past what
// exemptMax tolerates. The fuzz TU instead pre-filters: it only asserts exact Rotation equality on
// WRAPPED samples whose `hash*6` sits comfortably far from every integer boundary (a TU-local
// distance check using gridWalkPointsHash(), not mathv_compare.h's global branchDist/envelopeOk
// machinery) -- Position (hash-independent) is still asserted on every sample unconditionally.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper (mathv_ref_shared_quat.h is itself a P5-safe oracle).
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
// Only app/src/runtime/tixl_point.h is included, for the SwPoint host struct (LegacyPoint and
// SwPoint share the identical 64-byte layout, see tixl_point.h header note).
#include "mathv_ref_shared_quat.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdint>

namespace sw {
namespace mathv_ref {
namespace gwp {

using quat::Quat;
using quat::Vec3;

// hash11 — hash-functions.hlsl :9-15. frac()==x-floor(x) (always in [0,1) for finite x, matches
// both HLSL's frac() and MSL's fract()).
inline float frac(float x) { return x - std::floor(x); }
inline float hash11(float p) {
  p = frac(p * 0.1031f);
  p *= p + 33.33f;
  p *= p + p;
  return frac(p);
}

// mod(a,b) float3 overload -- FLOORED (see file header AMBIGUITY note; used only where the ref keeps
// the dividend non-negative, so floored==truncated and the choice here is moot in practice, but
// written as floored to match the formula this .hlsl's `mod()` calls are conventionally defined as).
inline Vec3 mod3(Vec3 a, Vec3 b) {
  return {a.x - b.x * std::floor(a.x / b.x), a.y - b.y * std::floor(a.y / b.y),
          a.z - b.z * std::floor(a.z / b.z)};
}

// qFromAngleAxis — quat-functions.hlsl :65-70.
inline Quat qFromAngleAxis(float angle, Vec3 axis) {
  float sn = std::sin(angle * 0.5f);
  float cs = std::cos(angle * 0.5f);
  return {axis.x * sn, axis.y * sn, axis.z * sn, cs};
}

// HLSL builtin normalize(float4) == v * rsqrt(dot(v,v)), modeled as v/sqrt(dot(v,v)).
inline Quat normalizeQuat(Quat q) {
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return {q.x / len, q.y / len, q.z / len, q.w / len};
}

constexpr float PI = 3.14159265359f;  // quat-functions.hlsl:5

// axisAngles[] -- grid-walk-points.hlsl :20-28, verbatim static table.
constexpr float AXIS_ANGLES[6][4] = {
    {0, 1, 0, 0}, {0, 1, 0, 1}, {0, 1, 0, 2}, {0, 1, 0, 3}, {1, 0, 0, -1}, {1, 0, 0, 1},
};

}  // namespace gwp

// cbuffer field table -- grid-walk-points.hlsl :6-16.
struct GridWalkPointsParams {
  float gridSizeX, gridSizeY, gridSizeZ;  // :8
  float speed;                             // :9
  float gridOffsetX, gridOffsetY, gridOffsetZ;  // :11
  // speedVariation (:12) intentionally omitted -- never read by the kernel (params.h header note).
  float triggerTurn;  // :14
  float seed;          // :15
};

// gridWalkPointsHash — exposes the :50 hash11(...) result standalone (same formula as the inline
// computation inside gridWalkPointsOne below). Used by the fuzz TU to detect samples whose
// `hash*6` value sits within a hair of an integer boundary -- hash11's two internal squaring
// rounds (:64-66) amplify any GPU-vs-CPU fast-math ULP divergence in the input sum by several
// orders of magnitude (measured: a 4096-sample sweep showed ~7% of the WRAPPED branch's table-index
// picks disagreeing between the GPU and this ref -- too large a fraction for mathv_compare.h's
// existing exemptMax=1% ill-conditioned-lookup channel, §2 2b, which is sized for a residual tail,
// not a systematic ~7% rate). The TU pre-filters: only samples with `hash*6` comfortably far from
// every integer boundary get their WRAPPED Rotation asserted exact; near-boundary samples are
// EXPECTED to diverge and are excluded from that assertion (Position, which never depends on hash,
// is still asserted for every sample unconditionally).
inline float gridWalkPointsHash(float positionX, float positionY, float positionZ, int32_t idx,
                                 float seed) {
  using namespace gwp;
  float dividend = (seed + (float)idx + positionX + positionY + positionZ) * 421.0f;
  float modded = std::fmod(dividend, 1231.0f);
  return hash11(modded);
}

// gridWalkPointsWrapped — exposes the :51-54 boundary-check bool standalone, for the fuzz TU to
// decide whether a given (in,idx) pair takes the WRAPPED branch WITHOUT duplicating the velocity/
// mod3 computation inline in the TU. Hash-independent (only Rotation/Position/gridSize/speed/
// gridOffset/triggerTurn), so this is safe to assert on both sides unconditionally (unlike the
// axisAngles[] table-index pick).
inline bool gridWalkPointsWrapped(const SwPoint& in, const GridWalkPointsParams& P) {
  using namespace gwp;
  Quat rotN = normalizeQuat({in.Rotation.x, in.Rotation.y, in.Rotation.z, in.Rotation.w});
  Vec3 forward{0.0f, 0.0f, -1.0f};
  Vec3 velRaw = quat::qRotateVec3(forward, rotN);
  Vec3 velocity{velRaw.x * P.speed, velRaw.y * P.speed, velRaw.z * P.speed};
  Vec3 posMinusOffset{in.Position.x - P.gridOffsetX, in.Position.y - P.gridOffsetY,
                       in.Position.z - P.gridOffsetZ};
  Vec3 gridSize{P.gridSizeX, P.gridSizeY, P.gridSizeZ};
  Vec3 localPosition = mod3(posMinusOffset, gridSize);
  Vec3 newLocalPosition{localPosition.x + velocity.x, localPosition.y + velocity.y,
                         localPosition.z + velocity.z};
  return (newLocalPosition.x <= 0.0f) || (newLocalPosition.x >= gridSize.x) ||
         (newLocalPosition.y <= 0.0f) || (newLocalPosition.y >= gridSize.y) ||
         (newLocalPosition.z <= 0.0f) || (newLocalPosition.z >= gridSize.z) ||
         (P.triggerTurn > 0.5f);
}

// gridWalkPointsOne — one thread of `main()` (:32-66). `idx`/`count` reproduce the GetDimensions
// guard (:34-39); DEAD stores (:56/:58-61, see file header) are NOT modeled.
inline void gridWalkPointsOne(const SwPoint& in, SwPoint& out, int32_t idx, int32_t count,
                               const GridWalkPointsParams& P) {
  using namespace gwp;

  // :36-39 -- GetDimensions guard: out-of-declared-range threads still write W(FX1)=0 before
  // returning (a real, non-dead side effect, unlike the two dead stores inside the main branch).
  if (idx >= count) {
    out = in;
    out.FX1 = 0.0f;  // LegacyPoint.W == SwPoint.FX1
    return;
  }

  // :42-43 -- velocity = qRotateVec3((0,0,-1), normalize(Rotation)) * Speed
  Quat rotN = normalizeQuat({in.Rotation.x, in.Rotation.y, in.Rotation.z, in.Rotation.w});
  Vec3 forward{0.0f, 0.0f, -1.0f};
  Vec3 velRaw = quat::qRotateVec3(forward, rotN);
  Vec3 velocity{velRaw.x * P.speed, velRaw.y * P.speed, velRaw.z * P.speed};

  // :47-48 -- localPosition = mod(Position-GridOffset, GridSize); newLocalPosition = localPosition+velocity
  Vec3 posMinusOffset{in.Position.x - P.gridOffsetX, in.Position.y - P.gridOffsetY,
                       in.Position.z - P.gridOffsetZ};
  Vec3 gridSize{P.gridSizeX, P.gridSizeY, P.gridSizeZ};
  Vec3 localPosition = mod3(posMinusOffset, gridSize);
  Vec3 newLocalPosition{localPosition.x + velocity.x, localPosition.y + velocity.y,
                         localPosition.z + velocity.z};

  // :50 -- hash = hash11((Seed+i.x+Position.x+Position.y+Position.z)*421 % 1231). `%` modeled as
  // HLSL-spec TRUNCATED (C++ built-in `%` semantics via fmod for floats) -- see file header
  // AMBIGUITY note; the fuzz TU keeps the dividend non-negative so this never actually forks vs the
  // transpiled kernel's floored lowering.
  float dividend = (P.seed + (float)idx + in.Position.x + in.Position.y + in.Position.z) * 421.0f;
  float modded = std::fmod(dividend, 1231.0f);  // HLSL-spec truncated float `%` (fmod matches: sign
                                                  // follows dividend, C-style)
  float hash = hash11(modded);

  bool wrapped = (newLocalPosition.x <= 0.0f) || (newLocalPosition.x >= gridSize.x) ||
                 (newLocalPosition.y <= 0.0f) || (newLocalPosition.y >= gridSize.y) ||
                 (newLocalPosition.z <= 0.0f) || (newLocalPosition.z >= gridSize.z) ||
                 (P.triggerTurn > 0.5f);

  out = in;
  if (wrapped) {
    // :62-63 -- axisAndAngle = axisAngles[(int)(hash*6) % 6]; Rotation = qFromAngleAxis(w*PI/1.5, xyz)
    int32_t idx6 = (int32_t)(hash * 6.0f) % 6;  // hash in [0,1) via frac() -> hash*6 in [0,6), always
                                                  // non-negative -- truncated==floored here, no
                                                  // ambiguity (see file header note on hash11's range).
    const float* aa = gwp::AXIS_ANGLES[idx6];
    Quat newRot = gwp::qFromAngleAxis(aa[3] * PI / 1.5f, Vec3{aa[0], aa[1], aa[2]});
    out.Rotation = {newRot.x, newRot.y, newRot.z, newRot.w};
  }
  // :65 -- ResultPoints[i.x].Position += velocity (unconditional, outside the wrapped branch)
  out.Position.x = in.Position.x + velocity.x;
  out.Position.y = in.Position.y + velocity.y;
  out.Position.z = in.Position.z + velocity.z;
}

// mathvRefGridWalkPoints — full-buffer CPU oracle over `dispatchedThreads` threads (padded up to a
// multiple of 64, matching the real dispatch's threadgroup rounding; `count` is the logical point
// count from GetDimensions, may be < dispatchedThreads to exercise the tail-guard branch).
inline void mathvRefGridWalkPoints(const SwPoint* in, SwPoint* out, size_t dispatchedThreads,
                                    int32_t count, const GridWalkPointsParams& P) {
  for (size_t i = 0; i < dispatchedThreads; ++i) {
    gridWalkPointsOne(in[i], out[i], (int32_t)i, count, P);
  }
}

}  // namespace mathv_ref
}  // namespace sw
