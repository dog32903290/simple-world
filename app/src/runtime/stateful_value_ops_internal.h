// runtime/stateful_value_ops_internal — shared internal helpers for the stateful value-op leaves.
// Extracted VERBATIM from the old stateful_value_ops.cpp monolith during the debt-sprint split:
//   lerpf / getIn / dampenFloat / methodOf / kVecComp / getInC / enumOf / clamp01.
// These are the cross-family glue the Damp/Spring/Ease/Vec/ChangeDetector/Numeric leaves share. Every
// fn was either already `inline` or gains `inline` here (a linkage qualifier ONLY — ZERO math/behavior
// change) so it is ODR-safe in a header included by multiple leaf TUs. kVecComp becomes `inline
// constexpr` for the same header-safety reason.
//
// runtime leaf: pure computation, no hardware, no UI.
#pragma once
#include <cmath>
#include <map>
#include <string>

namespace sw {

// TiXL MathUtils.Lerp(a,b,t) = a + (b-a)*t (standard lerp). Used by Damp(Linear) and Spring.
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline float getIn(const std::map<std::string, float>& in, const char* k, float dflt) {
  auto it = in.find(k);
  return it != in.end() ? it->second : dflt;
}

// The shared damp core (TiXL DampFunctions.DampenFloat / MathUtils.SpringDamp, MathUtils.cs). Single
// source so Damp and DampAngle can't drift. method 0=LinearInterpolation (Lerp(target,current,d) =
// target+(current-target)*d), 1=DampedSpring (critically-damped, k=0.5/(damping+0.001), dt∈[0,1/60]).
// dt clamp is TiXL's (DampFunctions.SpringDampFloat: Playback.LastFrameDuration.Clamp(0,1/60)).
inline float dampenFloat(float target, float prev, float damping, float& velocity, int method, float dt) {
  float r;
  if (method == 0) {
    r = lerpf(target, prev, damping);
  } else {
    const float k = 0.5f / (damping + 0.001f);
    float ts = dt;
    if (ts < 0.0f) ts = 0.0f;
    else if (ts > 1.0f / 60.0f) ts = 1.0f / 60.0f;
    const float toTarget = target - prev;
    const float force = toTarget * k - velocity * 2.0f * std::sqrt(k);
    velocity += force * ts;
    r = prev + velocity * ts;
  }
  if (!std::isfinite(r)) r = 0.0f;                // TiXL MathUtils.ApplyDefaultIfInvalid(_dampedValue,0)
  if (!std::isfinite(velocity)) velocity = 0.0f;  // ...and _velocity
  return r;
}

inline int methodOf(const std::map<std::string, float>& in) {
  int m = (int)std::lround(getIn(in, "Method", 0.0f));
  return m < 0 ? 0 : (m > 1 ? 1 : m);  // TiXL: Method.GetValue(context).Clamp(0,1)
}

// Vec component lookup: TiXL Vec inputs are our "<base>.x/.y/.z" Float ports (node_registry vec
// convention, e.g. AddVec3). Returns 0 for a missing component (matches the spec default).
inline constexpr const char* const kVecComp[3] = {".x", ".y", ".z"};
inline float getInC(const std::map<std::string, float>& in, const char* base, int c) {
  std::string key = std::string(base) + kVecComp[c];
  auto it = in.find(key);
  return it != in.end() ? it->second : 0.0f;
}

inline int enumOf(const std::map<std::string, float>& in, const char* k) {
  return (int)std::lround(getIn(in, k, 0.0f));
}
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace sw
