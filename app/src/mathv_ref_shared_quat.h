#pragma once
// mathv_ref_shared_quat — shared CPU scalar oracle for the quaternion helper closure that TiXL's
// point/particle kernels pull in via `#include "shared/quat-functions.hlsl"`.
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/shared/quat-functions.hlsl — NOT derived from sw shaders.
//   qRotateVec3(v,q) :47-51  (the *compiled* "faster" variant; the :40-44 commented-out variant is
//                             dead HLSL source, not modeled — same call already made in
//                             mathv_ref_addnoise.h's own local copy of this function)
//   qLookAt(forward,up) :103-160
//   qSlerp(a,b,t)        :162-215
// Every statement below carries its own :NN back-reference into quat-functions.hlsl.
//
// SPLIT RATIONALE (MATH_VERIFY_WORKFLOW.md §1.4 "大依賴閉包（noise/quat）獨立成共享 oracle header
// 一份多 op 引用"): mathv_ref_addnoise.h already needed qRotateVec3 + qFromMatrix3Precise and chose
// to keep a local private copy (its own closure was small: 2 functions). ReorientLinePoints needs
// qRotateVec3 + qLookAt + qSlerp — a 3-function, ~120-line closure that a second future quat-heavy
// op (any TiXL kernel calling qLookAt/qSlerp) would otherwise have to re-transcribe byte-for-byte.
// This file is the first materialization of the doc's anticipated `mathv_ref_shared_quat.h`.
// mathv_ref_addnoise.h's existing local qRotateVec3/qFromMatrix3Precise are intentionally NOT
// migrated here in this change (out of scope for the ReorientLinePoints ticket; migrating a
// landed, lint-green ref file is a separate cleanup, not bundled into an unrelated op's ref).
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §5): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port
// (intentionally never opened while writing this header). Zero metal include, zero app/shaders/
// reference, zero sw math helper.
//
// FLOAT (not double) on purpose (§2.3 branchy-class rule): qLookAt's `num8 > 0.0` / `m00 >= m11`
// ladder and qSlerp's `cosHalfAngle < 0.99` threshold are float comparisons on the GPU; a double ref
// could land on a different branch than the float GPU for the same nominal input.
//
// ZONE: shell-tier mathv support (pure math; no runtime/platform/Metal dependency, no dependency on
// any other mathv_ref_*.h — this file only needs <cmath>, so any op ref can include it standalone).
#include <cmath>

namespace sw {
namespace mathv_ref {
namespace quat {

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };

// HLSL builtin `dot()` (language intrinsic, not TiXL source; same treatment as
// mathv_ref_addnoise.h's detail::hlslCross for the sibling `cross()` builtin).
inline float hlslDot3(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// HLSL builtin `cross()` (standard right-handed formula).
inline Vec3 hlslCross3(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// HLSL builtin `length(v)` == `sqrt(dot(v,v))`.
inline float hlslLength3(Vec3 v) { return std::sqrt(hlslDot3(v, v)); }

// HLSL builtin `normalize(v)` == `v * rsqrt(dot(v,v))`, modeled as `v / sqrt(dot(v,v))` (same
// normalize(0)->NaN behavior documented in mathv_ref_addnoise.h's detail::hlslNormalize).
inline Vec3 hlslNormalize3(Vec3 v) {
  float len = hlslLength3(v);
  return {v.x / len, v.y / len, v.z / len};
}

// qRotateVec3 — quat-functions.hlsl :47-51 ("faster" variant; see file header DEAD-CODE note).
inline Vec3 qRotateVec3(Vec3 v, Quat q) {
  Vec3 qxyz{q.x, q.y, q.z};
  Vec3 t = hlslCross3(qxyz, v);
  t = {2.0f * t.x, 2.0f * t.y, 2.0f * t.z};                      // :49 float3 t = 2*cross(q.xyz, v);
  Vec3 qwt{q.w * t.x, q.w * t.y, q.w * t.z};
  Vec3 crossQxyzT = hlslCross3(qxyz, t);
  return {v.x + qwt.x + crossQxyzT.x, v.y + qwt.y + crossQxyzT.y,
          v.z + qwt.z + crossQxyzT.z};                           // :50 v + q.w*t + cross(q.xyz, t)
}

// qLookAt — quat-functions.hlsl :103-160. Builds an orthonormal (right,up,forward) frame from the
// two input vectors (re-deriving `up` as cross(forward,right) rather than trusting the caller's
// `up` to already be orthogonal to `forward`), then extracts a quaternion via a "largest diagonal
// term" ladder. NOTE: this is a DIFFERENT branch shape than qFromMatrix3Precise (quat-functions.hlsl
// :263-303, already ported in mathv_ref_addnoise.h) — qLookAt's ladder uses `>=` (not `>`) in its
// second branch and RETURNS early from each arm instead of falling through an if/else-if/else chain;
// the two are not interchangeable and must not be conflated (S-audit note).
inline Quat qLookAt(Vec3 forward, Vec3 up) {
  Vec3 right = hlslNormalize3(hlslCross3(forward, up));   // :105
  up = hlslNormalize3(hlslCross3(forward, right));        // :106 (re-derived, ignores caller's `up`
                                                           // beyond its use in computing `right`)

  float m00 = right.x, m01 = right.y, m02 = right.z;      // :108-110
  float m10 = up.x, m11 = up.y, m12 = up.z;               // :111-113
  float m20 = forward.x, m21 = forward.y, m22 = forward.z; // :114-116

  float num8 = (m00 + m11) + m22;                          // :118
  Quat q{0.0f, 0.0f, 0.0f, 1.0f};                           // :119 QUATERNION_IDENTITY default
  if (num8 > 0.0f) {                                        // :120
    float num = std::sqrt(num8 + 1.0f);                     // :122
    q.w = num * 0.5f;                                       // :123
    num = 0.5f / num;                                       // :124
    q.x = (m12 - m21) * num;                                // :125
    q.y = (m20 - m02) * num;                                // :126
    q.z = (m01 - m10) * num;                                // :127
    return q;                                               // :128
  }

  if ((m00 >= m11) && (m00 >= m22)) {                       // :131
    float num7 = std::sqrt(((1.0f + m00) - m11) - m22);     // :133
    float num4 = 0.5f / num7;                               // :134
    q.x = 0.5f * num7;                                      // :135
    q.y = (m01 + m10) * num4;                               // :136
    q.z = (m02 + m20) * num4;                               // :137
    q.w = (m12 - m21) * num4;                               // :138
    return q;                                               // :139
  }

  if (m11 > m22) {                                          // :142
    float num6 = std::sqrt(((1.0f + m11) - m00) - m22);     // :144
    float num3 = 0.5f / num6;                               // :145
    q.x = (m10 + m01) * num3;                                // :146
    q.y = 0.5f * num6;                                       // :147
    q.z = (m21 + m12) * num3;                                // :148
    q.w = (m20 - m02) * num3;                                // :149
    return q;                                                // :150
  }

  float num5 = std::sqrt(((1.0f + m22) - m00) - m11);        // :153
  float num2 = 0.5f / num5;                                  // :154
  q.x = (m20 + m02) * num2;                                  // :155
  q.y = (m21 + m12) * num2;                                  // :156
  q.z = 0.5f * num5;                                         // :157
  q.w = (m01 - m10) * num2;                                  // :158
  return q;                                                  // :159
}

// qSlerp — quat-functions.hlsl :162-215. `a`/`b` taken by value (HLSL passes `b` `in` but the body
// mutates it locally at :186-188; a local copy here reproduces that without touching the caller's
// quaternion, matching HLSL's by-value `in` semantics).
inline Quat qSlerp(Quat a, Quat b, float t) {
  // :165-176 -- zero-length-input fast paths. `length(a)==0.0` is an EXACT float compare against
  // the literal `0.0`, not an epsilon test -- a genuinely-zero (all-four-components-zero) input
  // quaternion, not merely "small". Ordinary unit quaternions never hit this in practice.
  if (hlslLength3({a.x, a.y, a.z}) == 0.0f && a.w == 0.0f) {   // length(a)==0 requires all 4 comps 0;
                                                                // hlslLength3 only covers xyz, so the
                                                                // w component is checked separately.
    if (hlslLength3({b.x, b.y, b.z}) == 0.0f && b.w == 0.0f) {
      return {0.0f, 0.0f, 0.0f, 1.0f};                          // :169 QUATERNION_IDENTITY
    }
    return b;                                                  // :171
  } else if (hlslLength3({b.x, b.y, b.z}) == 0.0f && b.w == 0.0f) {
    return a;                                                  // :175
  }

  float cosHalfAngle = a.w * b.w + hlslDot3({a.x, a.y, a.z}, {b.x, b.y, b.z});  // :178

  if (cosHalfAngle >= 1.0f || cosHalfAngle <= -1.0f) {         // :180
    return a;                                                  // :182
  } else if (cosHalfAngle < 0.0f) {                            // :184
    b.x = -b.x; b.y = -b.y; b.z = -b.z;                        // :186
    b.w = -b.w;                                                // :187
    cosHalfAngle = -cosHalfAngle;                               // :188
  }

  float blendA, blendB;                                        // :191-192
  if (cosHalfAngle < 0.99f) {                                   // :193
    float halfAngle = std::acos(cosHalfAngle);                  // :196
    float sinHalfAngle = std::sin(halfAngle);                   // :197
    float oneOverSinHalfAngle = 1.0f / sinHalfAngle;             // :198
    blendA = std::sin(halfAngle * (1.0f - t)) * oneOverSinHalfAngle;  // :199
    blendB = std::sin(halfAngle * t) * oneOverSinHalfAngle;      // :200
  } else {
    blendA = 1.0f - t;                                          // :205
    blendB = t;                                                 // :206
  }

  Quat result{blendA * a.x + blendB * b.x, blendA * a.y + blendB * b.y,
              blendA * a.z + blendB * b.z, blendA * a.w + blendB * b.w};  // :209
  float resultLen = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z +
                               result.w * result.w);
  if (resultLen > 0.0f) {                                        // :210
    return {result.x / resultLen, result.y / resultLen, result.z / resultLen,
            result.w / resultLen};                               // :212 normalize(result)
  }
  return {0.0f, 0.0f, 0.0f, 1.0f};                                // :214 QUATERNION_IDENTITY
}

}  // namespace quat
}  // namespace mathv_ref
}  // namespace sw
