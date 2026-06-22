// runtime/stateful_value_ops_easing — shared EasingFunctions util header for the stateful Ease family
// (Ease / EaseVec2 / EaseVec3). Extracted VERBATIM from the old stateful_value_ops.cpp monolith
// (lines 14-164) during the debt-sprint split. All curve fns were already `inline`; the four dispatch
// fns (applyEaseIn/Out/InOut, applyEasing) gained `inline` ONLY so they are ODR-safe in a header
// included by multiple leaf TUs — a linkage qualifier, ZERO math/behavior change.
//
// TiXL authority: Core/Utils/EasingFunctions.cs (ported verbatim). Constants for Back/Elastic/Bounce
// match the .cs exactly. runtime leaf: pure computation, no hardware, no UI.
#pragma once
#include <cmath>

namespace sw {

// === EasingFunctions (TiXL Core/Utils/EasingFunctions.cs) — ported VERBATIM ===
// Only needed by Ease/EaseVec2/EaseVec3, so the 30 curve fns + ApplyEasing live here as static free
// functions (not a shared header). Constants for Back/Elastic/Bounce match the .cs exactly.
constexpr float kPi = 3.14159265358979323846f;  // MathF.PI
// Sine
inline float inSine(float t) { return 1.0f - std::cos((t * kPi) / 2.0f); }
inline float outSine(float t) { return std::sin((t * kPi) / 2.0f); }
inline float inOutSine(float t) { return -(std::cos(kPi * t) - 1.0f) / 2.0f; }
// Quad
inline float inQuad(float t) { return t * t; }
inline float outQuad(float t) { return t * (2.0f - t); }
inline float inOutQuad(float t) { return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; }
// Cubic
inline float inCubic(float t) { return t * t * t; }
inline float outCubic(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
inline float inOutCubic(float t) { return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f; }
// Quart
inline float inQuart(float t) { return t * t * t * t; }
inline float outQuart(float t) { return 1.0f - std::pow(1.0f - t, 4.0f); }
inline float inOutQuart(float t) { return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f; }
// Quint
inline float inQuint(float t) { return t * t * t * t * t; }
inline float outQuint(float t) { return 1.0f - std::pow(1.0f - t, 5.0f); }
inline float inOutQuint(float t) { return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f; }
// Expo
inline float inExpo(float t) { return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f); }
inline float outExpo(float t) { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
inline float inOutExpo(float t) {
  return t == 0.0f ? 0.0f
         : t == 1.0f ? 1.0f
         : t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                    : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}
// Circ
inline float inCirc(float t) { return 1.0f - std::sqrt(1.0f - std::pow(t, 2.0f)); }
inline float outCirc(float t) { return std::sqrt(1.0f - std::pow(t - 1.0f, 2.0f)); }
inline float inOutCirc(float t) {
  return t < 0.5f ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
                  : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}
// Back
inline float inBack(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.0f;
  return c3 * t * t * t - c1 * t * t;
}
inline float outBack(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.0f;
  return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}
inline float inOutBack(float t) {
  const float c1 = 1.70158f, c2 = c1 * 1.525f;
  return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                  : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
}
// Elastic
inline float inElastic(float t) {
  const float c4 = (2.0f * kPi) / 3.0f;
  return t == 0.0f ? 0.0f
         : t == 1.0f ? 1.0f
                     : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}
inline float outElastic(float t) {
  const float c4 = (2.0f * kPi) / 3.0f;
  return t == 0.0f ? 0.0f
         : t == 1.0f ? 1.0f
                     : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
inline float inOutElastic(float t) {
  const float c5 = (2.0f * kPi) / 4.5f;
  return t == 0.0f ? 0.0f
         : t == 1.0f ? 1.0f
         : t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
                    : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
}
// Bounce
inline float outBounce(float t) {
  const float n1 = 7.5625f, d1 = 2.75f;
  if (t < 1.0f / d1) {
    return n1 * t * t;
  } else if (t < 2.0f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  } else if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  } else {
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
  }
}
inline float inBounce(float t) { return 1.0f - outBounce(1.0f - t); }
inline float inOutBounce(float t) {
  return t < 0.5f ? (1.0f - outBounce(1.0f - 2.0f * t)) / 2.0f
                  : (1.0f + outBounce(2.0f * t - 1.0f)) / 2.0f;
}

// Interpolations enum (0=Linear..10=Bounce) / EaseDirection (0=In,1=Out,2=InOut), TiXL EasingFunctions.cs.
inline float applyEaseIn(float p, int mode) {
  switch (mode) {
    case 1:  return inSine(p);
    case 2:  return inQuad(p);
    case 3:  return inCubic(p);
    case 4:  return inQuart(p);
    case 5:  return inQuint(p);
    case 6:  return inExpo(p);
    case 7:  return inCirc(p);
    case 8:  return inBack(p);
    case 9:  return inElastic(p);
    case 10: return inBounce(p);
    default: return p;  // Linear (0) + unknown
  }
}
inline float applyEaseOut(float p, int mode) {
  switch (mode) {
    case 1:  return outSine(p);
    case 2:  return outQuad(p);
    case 3:  return outCubic(p);
    case 4:  return outQuart(p);
    case 5:  return outQuint(p);
    case 6:  return outExpo(p);
    case 7:  return outCirc(p);
    case 8:  return outBack(p);
    case 9:  return outElastic(p);
    case 10: return outBounce(p);
    default: return p;
  }
}
inline float applyEaseInOut(float p, int mode) {
  switch (mode) {
    case 1:  return inOutSine(p);
    case 2:  return inOutQuad(p);
    case 3:  return inOutCubic(p);
    case 4:  return inOutQuart(p);
    case 5:  return inOutQuint(p);
    case 6:  return inOutExpo(p);
    case 7:  return inOutCirc(p);
    case 8:  return inOutBack(p);
    case 9:  return inOutElastic(p);
    case 10: return inOutBounce(p);
    default: return p;
  }
}
// TiXL EasingFunctions.ApplyEasing(progress, direction, easeMode).
inline float applyEasing(float progress, int direction, int easeMode) {
  switch (direction) {
    case 0:  return applyEaseIn(progress, easeMode);
    case 1:  return applyEaseOut(progress, easeMode);
    case 2:  return applyEaseInOut(progress, easeMode);
    default: return progress;
  }
}

}  // namespace sw
