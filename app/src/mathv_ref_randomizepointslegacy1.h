#pragma once
// mathv_ref_randomizepointslegacy1 — CPU scalar oracle for TiXL points/modify/
// RandomizePoints_Legacy1 (points island; owner: external/tixl/Operators/Lib/point/modify/
// _RandomizePoints_Legacy1.t3 — a legacy point randomizer, distinct from sw's existing
// `randomizepoints` kernel which ports the DIFFERENT, non-legacy external/tixl .../points/modify/
// RandomizePoints.hlsl: that kernel's cbuffer fields are Strength/RandomizeColor/Stretch/GainAndBias/
// OffsetMode/UsePointSpace/Interpolation/ClampColorsEtc/Repeat/StrengthFactor — none of which this
// kernel has; the only OVERLAP is the RandomizePosition/RandomizeRotation field NAMES, a coincidental
// TiXL vocabulary reuse, not a shared kernel).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/modify/RandomizePoints_Legacy1.hlsl — NOT derived from sw's MSL
// kernel (app/shaders/randomizepointslegacy1.metal intentionally never opened while writing this
// file). Every statement below carries its own :NN back-reference into that file. Helper closures
// (hash41u/GetSchlickBias/qMul/qFromAngleAxis/qRotateVec3) are transcribed fresh from their shared
// TiXL headers rather than imported from sibling mathv_ref_*.h files (P5-safe R-role isolation keeps
// each op's oracle self-contained; qRotateVec3 alone is reused from mathv_ref_shared_quat.h since
// that header is explicitly designed for cross-op reuse, MATH_VERIFY_WORKFLOW.md §1.4):
//   shared/hash-functions.hlsl :102-113 (hash41u)
//   shared/bias-functions.hlsl :6-9 (GetBias), :57-61 (GetSchlickBias float4 overload)
//   shared/quat-functions.hlsl :29-34 (qMul), :45-49 (qRotateVec3, "faster" variant), :65-70
//     (qFromAngleAxis)
//   RandomizePoints_Legacy1.hlsl main() body :28-84
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): `UseWAsSelection > 0.5` and
// `UseLocalSpace < 0.5` are float comparisons on the GPU; a double ref could land on a different
// branch than the float GPU for the same nominal input. This op is classified TRANSCENDENTAL overall
// (qMul/qFromAngleAxis pull in sin/cos/normalize, and the hash functions are noise-like), not
// Branchy — the two branches above are on user-set mode flags (0/1-ish semantics), not near-continuous
// thresholds an input sweep would land arbitrarily close to.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper (mathv_ref_shared_quat.h is itself a P5-safe oracle, not an sw math
// helper — see that file's own header).
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
// Only app/src/runtime/tixl_point.h is included, for the SwPoint host struct (data layout, not math;
// LegacyPoint and SwPoint share the identical 64-byte layout, see tixl_point.h header note and
// shared/point.hlsl's own "Points are particles share the same structure" comment).
#include "mathv_ref_shared_quat.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdint>

namespace sw {
namespace mathv_ref {
namespace rpl1 {

using quat::Quat;
using quat::Vec3;

// hash41u — hash-functions.hlsl :102-113. uint32_t arithmetic wraps mod 2^32 the same way HLSL's
// uint does (same treatment as mathv_ref_addnoise.h's detail::hash41u).
inline void hash41u(uint32_t x, float& ox, float& oy, float& oz, float& ow) {
  const uint32_t k = 1103515245u;
  x *= 13331u;                                  // _PRIME0
  x = ((x >> 8) ^ x) * k;
  uint32_t y = ((x >> 8) ^ x) * k;
  uint32_t z = ((y >> 8) ^ x) * k;
  uint32_t w = ((z >> 8) ^ y) * k;
  const float inv = 1.0f / 4294967295.0f;  // 1/0xffffffff
  ox = (float)x * inv;
  oy = (float)y * inv;
  oz = (float)z * inv;
  ow = (float)w * inv;
}

// GetBias — bias-functions.hlsl :6-9 (scalar) / :52-55 (float4 overload, same formula per-component).
inline float getBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }

// GetSchlickBias (float4 overload) — bias-functions.hlsl :57-61.
//   x < 0.5 ? GetBias(gain, x*2)/2 : GetBias(1-gain, x*2-1)/2 + 0.5
struct Float4 { float x, y, z, w; };
inline Float4 getSchlickBias4(Float4 v, float gain) {
  auto one = [&](float c) {
    return (c < 0.5f) ? (getBias(gain, c * 2.0f) * 0.5f)
                       : (getBias(1.0f - gain, c * 2.0f - 1.0f) * 0.5f + 0.5f);
  };
  return {one(v.x), one(v.y), one(v.z), one(v.w)};
}

// qMul — quat-functions.hlsl :29-34 (Hamilton product, xyzw layout, q1*q2).
inline Quat qMul(Quat q1, Quat q2) {
  Vec3 q1xyz{q1.x, q1.y, q1.z}, q2xyz{q2.x, q2.y, q2.z};
  Vec3 cr = quat::hlslCross3(q1xyz, q2xyz);
  return {q2xyz.x * q1.w + q1xyz.x * q2.w + cr.x, q2xyz.y * q1.w + q1xyz.y * q2.w + cr.y,
          q2xyz.z * q1.w + q1xyz.z * q2.w + cr.z, q1.w * q2.w - quat::hlslDot3(q1xyz, q2xyz)};
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

// HLSL builtin smoothstep(0,1,x) == t=clamp(x,0,1); t*t*(3-2t).
inline float smoothstep01(float x) {
  float t = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  return t * t * (3.0f - 2.0f * t);
}

inline Float4 lerp4(Float4 a, Float4 b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

constexpr float PI = 3.14159265359f;  // quat-functions.hlsl:5 `#define PI 3.14159265359f`

}  // namespace rpl1

// cbuffer field table — RandomizePoints_Legacy1.hlsl :7-22.
struct RandomizePointsLegacy1Params {
  float randomizePositionX, randomizePositionY, randomizePositionZ;  // :9
  float amount;                                                       // :10
  float randomizeRotationX, randomizeRotationY, randomizeRotationZ;  // :12
  float randomizeW;                                                   // :13
  float useLocalSpace;                                                // :15
  float seed;                                                         // :16
  float bias;                                                         // :18
  float offset;                                                       // :19
  float useWAsSelection;                                              // :21
};

// randomizePointsLegacy1One — one thread of `main()` (:28-84). `pointCount` reproduces the
// GetDimensions() result (:31-32) -- the guard that would use it (:33-36) is COMMENTED OUT in the
// source, so EVERY thread runs unconditionally; `pointCount` still feeds the `f`/`phase` formula
// (:42) and must match whatever the host's buffer element count really is (host-ABI substitution,
// same treatment as the kernel adapter's §10.5①).
inline void randomizePointsLegacy1One(const SwPoint& in, SwPoint& out, int32_t pointId,
                                       int32_t pointCount,
                                       const RandomizePointsLegacy1Params& P) {
  using namespace rpl1;

  // :42-44
  float f = (float)pointId / (float)pointCount;
  float phase = P.seed + 133.1123f * f + 999.0f;
  int32_t phaseId = (int32_t)phase;  // HLSL (int) cast of a float truncates toward zero

  // :45-48 -- normalizedScatter = lerp(hash41u(a), hash41u(a+1), smoothstep(0,1, phase-phaseId))
  float h0x, h0y, h0z, h0w, h1x, h1y, h1z, h1w;
  hash41u((uint32_t)(pointId * 12341 + phaseId), h0x, h0y, h0z, h0w);
  hash41u((uint32_t)(pointId * 12341 + phaseId + 1), h1x, h1y, h1z, h1w);
  float tScatter = smoothstep01(phase - (float)phaseId);
  Float4 normalizedScatter = lerp4({h0x, h0y, h0z, h0w}, {h1x, h1y, h1z, h1w}, tScatter);

  // :52-55 -- hashRot = lerp(hash41u(b), hash41u(b+1), smoothstep(...)) * 2 - 1
  float g0x, g0y, g0z, g0w, g1x, g1y, g1z, g1w;
  hash41u((uint32_t)(pointId * 2723 + phaseId), g0x, g0y, g0z, g0w);
  hash41u((uint32_t)(pointId * 2723 + phaseId + 1), g1x, g1y, g1z, g1w);
  Float4 hashRotLerp = lerp4({g0x, g0y, g0z, g0w}, {g1x, g1y, g1z, g1w}, tScatter);
  Float4 hashRot = {hashRotLerp.x * 2.0f - 1.0f, hashRotLerp.y * 2.0f - 1.0f,
                     hashRotLerp.z * 2.0f - 1.0f, hashRotLerp.w * 2.0f - 1.0f};

  // :58 -- hash4 = GetSchlickBias(normalizedScatter, Bias) * 2 - 1
  Float4 biased = getSchlickBias4(normalizedScatter, P.bias);
  Float4 hash4 = {biased.x * 2.0f - 1.0f, biased.y * 2.0f - 1.0f, biased.z * 2.0f - 1.0f,
                   biased.w * 2.0f - 1.0f};

  // :61-65
  Quat rot{in.Rotation.x, in.Rotation.y, in.Rotation.z, in.Rotation.w};
  float amount = P.amount * ((P.useWAsSelection > 0.5f) ? in.FX1 : 1.0f);  // LegacyPoint.W == SwPoint.FX1
  Vec3 offset{hash4.x * P.randomizePositionX * amount, hash4.y * P.randomizePositionY * amount,
              hash4.z * P.randomizePositionZ * amount};

  // :67-70
  if (P.useLocalSpace < 0.5f) {
    offset = quat::qRotateVec3(offset, rot);
  }

  // :72
  float outPosX = in.Position.x + offset.x;
  float outPosY = in.Position.y + offset.y;
  float outPosZ = in.Position.z + offset.z;

  // :74 -- randomRotate = (hashRot.xyz-0.5) * (RandomizeRotation/180*PI) * amount * hash4.xyz
  Vec3 randomRotate{(hashRot.x - 0.5f) * (P.randomizeRotationX / 180.0f * PI) * amount * hash4.x,
                     (hashRot.y - 0.5f) * (P.randomizeRotationY / 180.0f * PI) * amount * hash4.y,
                     (hashRot.z - 0.5f) * (P.randomizeRotationZ / 180.0f * PI) * amount * hash4.z};

  // :76-78 -- three successive qMul + normalize passes, one per axis
  rot = normalizeQuat(qMul(rot, qFromAngleAxis(randomRotate.x * P.offset, Vec3{1, 0, 0})));
  rot = normalizeQuat(qMul(rot, qFromAngleAxis(randomRotate.y * P.offset, Vec3{0, 1, 0})));
  rot = normalizeQuat(qMul(rot, qFromAngleAxis(randomRotate.z * P.offset, Vec3{0, 0, 1})));

  // :80-83
  out.Position = {outPosX, outPosY, outPosZ};
  out.Rotation = {rot.x, rot.y, rot.z, rot.w};
  out.Color = in.Color;    // never touched by this kernel -- passes through
  out.Scale = in.Scale;    // LegacyPoint.Stretch == SwPoint.Scale; never touched -- passes through
  out.FX1 = in.FX1 + hash4.w * P.randomizeW * amount;  // :82 LegacyPoint.W += ...
  out.FX2 = in.FX2;  // LegacyPoint.Selected (@60, same offset as SwPoint.FX2) -- never read/written
                      // by this kernel, passes through unchanged.
}

// mathvRefRandomizePointsLegacy1 — full-buffer CPU oracle over `dispatchedThreads` threads. Every
// thread runs unconditionally (no dispatch guard in the source, see header note) — `pointCount` is
// the SAME value for every thread (the host's declared buffer element count, matching what
// GetDimensions would return on the real GPU).
inline void mathvRefRandomizePointsLegacy1(const SwPoint* in, SwPoint* out, size_t dispatchedThreads,
                                            int32_t pointCount,
                                            const RandomizePointsLegacy1Params& P) {
  for (size_t i = 0; i < dispatchedThreads; ++i) {
    randomizePointsLegacy1One(in[i], out[i], (int32_t)i, pointCount, P);
  }
}

}  // namespace mathv_ref
}  // namespace sw
