#include "runtime/stateful_value_ops.h"

#include <cmath>
#include <cstdio>

#include "runtime/anim_math.h"  // AnimValue (+ the whole Anim* family) shape engine — pure-math helper

namespace sw {
namespace {

// TiXL MathUtils.Lerp(a,b,t) = a + (b-a)*t (standard lerp). Used by Damp(Linear) and Spring.
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

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
float applyEaseIn(float p, int mode) {
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
float applyEaseOut(float p, int mode) {
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
float applyEaseInOut(float p, int mode) {
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
float applyEasing(float progress, int direction, int easeMode) {
  switch (direction) {
    case 0:  return applyEaseIn(progress, easeMode);
    case 1:  return applyEaseOut(progress, easeMode);
    case 2:  return applyEaseInOut(progress, easeMode);
    default: return progress;
  }
}

inline float getIn(const std::map<std::string, float>& in, const char* k, float dflt) {
  auto it = in.find(k);
  return it != in.end() ? it->second : dflt;
}

// The shared damp core (TiXL DampFunctions.DampenFloat / MathUtils.SpringDamp, MathUtils.cs). Single
// source so Damp and DampAngle can't drift. method 0=LinearInterpolation (Lerp(target,current,d) =
// target+(current-target)*d), 1=DampedSpring (critically-damped, k=0.5/(damping+0.001), dt∈[0,1/60]).
// dt clamp is TiXL's (DampFunctions.SpringDampFloat: Playback.LastFrameDuration.Clamp(0,1/60)).
float dampenFloat(float target, float prev, float damping, float& velocity, int method, float dt) {
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

// --- Damp (TiXL Lib/numbers/float/process/Damp.cs) ---
// Ports: Value, Damping, Method(enum 0=LinearInterpolation, 1=DampedSpring).
// State: s[0]=dampedValue, s[1]=velocity. First cook seeds dampedValue=Value (TiXL _isFirstEval).
// Fork (named): TiXL's UseAppRunTime input + the 1ms MinTimeElapsedBeforeEvaluation guard are
//   DROPPED — frame_cook cooks each node exactly once per frame, so there is no sub-millisecond
//   double-eval to guard against (the whole reason that guard exists in TiXL).
void stepDamp(const std::map<std::string, float>& in, float dt, float /*time*/,
              StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  const float damping = getIn(in, "Damping", 0.9f);
  if (!st.init) {
    st.s[0] = value;  // TiXL: _dampedValue = inputValue on the first eval
    st.init = true;
  } else {
    st.s[0] = dampenFloat(value, st.s[0], damping, st.s[1], methodOf(in), dt);
  }
  out[0] = st.s[0];
}

// --- DampAngle (TiXL Lib/numbers/float/process/DampAngle.cs) ---
// Like Damp but in angle space: first re-target through the SHORTEST angular delta so 359°→1°
// damps +2° not -358°. Ports: Value, Damping, Method. State: s[0]=dampedValue, s[1]=velocity.
// Fork (named): UseAppRunTime + 1ms guard dropped (same once-per-frame reason). NOTE TiXL DampAngle
// has NO _isFirstEval — it damps from 0 on frame 1 (faithful: no init seeding here).
void stepDampAngle(const std::map<std::string, float>& in, float dt, float /*time*/,
                   StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  const float damping = getIn(in, "Damping", 0.9f);
  float& damped = st.s[0];
  // DeltaAngle(current=damped, target=value): Repeat(target-current,360); >180 → -360.
  float d = value - damped;
  d = d - std::floor(d / 360.0f) * 360.0f;  // TiXL Repeat(t,360)
  if (d < 0.0f) d = 0.0f;                    // Repeat clamps to [0,360]
  else if (d > 360.0f) d = 360.0f;
  if (d > 180.0f) d -= 360.0f;
  const float targetAngle = damped + d;
  damped = dampenFloat(targetAngle, damped, damping, st.s[1], methodOf(in), dt);
  out[0] = damped;
}

// --- DeltaSinceLastFrame (TiXL Lib/numbers/floats/process/DeltaSinceLastFrame.cs) ---
// Output = Value − previousValue. State: s[0]=lastValue (init 0 → first frame delta = Value).
// Ports: Value, Threshold (declared in TiXL but UNUSED in its math — kept for port parity, no fork).
void stepDeltaSinceLastFrame(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                             StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  out[0] = value - st.s[0];
  st.s[0] = value;
}

// --- FreezeValue (TiXL Lib/numbers/float/process/FreezeValue.cs) ---
// Sample-and-hold. Ports: Value, Freeze(Bool as Float 0/1), Mode(enum 0=FreezeWhileTrue,
// 1=UpdateWhenSwitchingToTrue). State: s[0]=frozenValue, s[1]=prevFreeze(0/1). Outputs:
// Result(frozen), DeltaSinceFreeze(Value−frozen). TiXL updates _freeze (the WasTriggered current)
// every frame on change, BEFORE the mode branch — replicated.
void stepFreezeValue(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                     StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  const bool freeze = getIn(in, "Freeze", 0.0f) > 0.5f;
  const int mode = (int)std::lround(getIn(in, "Mode", 0.0f));
  const bool prevFreeze = st.s[1] > 0.5f;
  const bool wasTriggered = (freeze != prevFreeze) && freeze;  // TiXL WasTriggered
  st.s[1] = freeze ? 1.0f : 0.0f;
  if (mode == 0) {
    if (!freeze) st.s[0] = value;          // FreezeWhileTrue: track while not frozen
  } else if (wasTriggered) {
    st.s[0] = value;                       // UpdateWhenSwitchingToTrue: sample on the rising edge
  }
  out[0] = st.s[0];
  out[1] = value - st.s[0];
}

// --- Spring (TiXL Lib/numbers/float/process/Spring.cs) ---
// Ports: Value, Tension, Strength. State: s[0]=springedValue, s[1]=result (the previous output).
// Fork (named): TiXL Spring uses NO dt term — it is purely iterative per frame (frame-rate
//   dependent); kept faithful (dt is ignored). UseAppRunTime + the 1ms guard are dropped for the
//   same once-per-frame-cook reason as Damp.
void stepSpring(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  const float tension = getIn(in, "Tension", 0.1f);
  const float strength = getIn(in, "Strength", 0.5f);
  float& springed = st.s[0];
  float& result = st.s[1];
  // TiXL: _springedValue = Lerp(_springedValue, (target - Result.Value) * Strength, Tension);
  //       Result.Value += _springedValue;
  springed = lerpf(springed, (value - result) * strength, tension);
  result += springed;
  out[0] = result;
}

// Vec component lookup: TiXL Vec inputs are our "<base>.x/.y/.z" Float ports (node_registry vec
// convention, e.g. AddVec3). Returns 0 for a missing component (matches the spec default).
const char* const kVecComp[3] = {".x", ".y", ".z"};
inline float getInC(const std::map<std::string, float>& in, const char* base, int c) {
  std::string key = std::string(base) + kVecComp[c];
  auto it = in.find(key);
  return it != in.end() ? it->second : 0.0f;
}

// --- DampVec2 / DampVec3 (TiXL vec2/DampVec2.cs, vec3/DampVec3.cs) ---
// Component-wise Damp. Ports: Value(vecN), Damping, Method. State: s[0..N-1]=damped, s[N..2N-1]=vel.
// Fork-free parity note: TiXL DampVec has NO _isFirstEval (unlike scalar Damp) — it damps from 0
// on frame 1; faithful here (no init seeding). Method 0=component Lerp, 1=SpringDampVecN (per-
// component SpringDamp, shared k & dt-clamp) — both are exactly dampenFloat() per component.
void dampVecImpl(const std::map<std::string, float>& in, float dt, StatefulValueState& st,
                 float out[3], int N) {
  const float damping = getIn(in, "Damping", 0.9f);
  const int method = methodOf(in);
  for (int c = 0; c < N; ++c) {
    const float v = getInC(in, "Value", c);
    st.s[c] = dampenFloat(v, st.s[c], damping, st.s[N + c], method, dt);
    out[c] = st.s[c];
  }
}
void stepDampVec2(const std::map<std::string, float>& in, float dt, float, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { dampVecImpl(in, dt, st, out, 2); }
void stepDampVec3(const std::map<std::string, float>& in, float dt, float, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { dampVecImpl(in, dt, st, out, 3); }

// --- SpringVec2 / SpringVec3 (TiXL vec2/process/SpringVec2.cs, vec3/process/SpringVec3.cs) ---
// Component-wise Spring (no dt; frame-rate dependent, faithful). Ports: Value(vecN), Tension,
// Strength. State: s[0..N-1]=springed, s[N..2N-1]=result. No init seeding (TiXL springs from 0).
void springVecImpl(const std::map<std::string, float>& in, StatefulValueState& st, float out[3], int N) {
  const float tension = getIn(in, "Tension", 0.1f);
  const float strength = getIn(in, "Strength", 0.5f);
  for (int c = 0; c < N; ++c) {
    const float v = getInC(in, "Value", c);
    float& springed = st.s[c];
    float& result = st.s[N + c];
    springed = lerpf(springed, (v - result) * strength, tension);
    result += springed;
    out[c] = result;
  }
}
void stepSpringVec2(const std::map<std::string, float>& in, float, float, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { springVecImpl(in, st, out, 2); }
void stepSpringVec3(const std::map<std::string, float>& in, float, float, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { springVecImpl(in, st, out, 3); }

// --- HasValueIncreased / HasValueDecreased (TiXL float/logic/HasValueIncreased.cs,
// float/process/HasValueDecreased.cs). Compare this frame's Value to last frame's; output a Float
// 0/1 flag (Bool dissolves to Float 0/1, Cut 32). State: s[0]=lastValue (init 0 → frame 1 compares
// against 0). Stateful because the flag needs the PRIOR frame's value. Verbatim:
//   Increased: HasIncreased = v > _lastValue + Threshold;  _lastValue = v;
//   Decreased: HasDecreased = v < _lastValue - Threshold;  _lastValue = v;
void stepHasValueIncreased(const std::map<std::string, float>& in, float, float, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float v = getIn(in, "Value", 0.0f);
  const float threshold = getIn(in, "Threshold", 0.0f);
  out[0] = (v > st.s[0] + threshold) ? 1.0f : 0.0f;
  st.s[0] = v;
}
void stepHasValueDecreased(const std::map<std::string, float>& in, float, float, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float v = getIn(in, "Value", 0.0f);
  const float threshold = getIn(in, "Threshold", 0.0f);
  out[0] = (v < st.s[0] - threshold) ? 1.0f : 0.0f;
  st.s[0] = v;
}

// --- HasValueChanged (TiXL float/logic/HasValueChanged.cs) ---
// Compares this frame's Value to last frame's and emits a change flag + delta + the delta captured
// on the last "hit". Bool outputs/inputs dissolve to Float 0/1 (Cut 32: no Bool port type).
// Outputs: HasChanged(0/1), Delta(=Value−lastValue, signed), DeltaOnHit(absDelta of last accepted hit).
// Ports/inputs (TiXL decl order): Value, Threshold(0), Mode(enum Changed/Increased/Decreased, 0),
//   MinTimeBetweenHits(0), PreventContinuedChanges(Bool→Float 0/1, default 0).
//   (No InputSlot ctor supplies a default in the .cs → every default is the type-zero, confirmed.)
// State: s[0]=lastValue, s[1]=lastHitTime, s[2]=lastHitDelta, s[3]=wasHit(0/1). All init 0 = TiXL
//   field defaults (_lastValue/_lastHitDelta=0, _wasHit=false; _lastHitTime=0 — see fork below).
// Time: TiXL uses context.LocalFxTime for the MinTimeBetweenHits gate; frame_cook hands wall seconds
//   via `time`, the same substitution Ease used. `dt` unused.
// Fork (named) — same precedent as Damp/Spring/Ease:
//   • The _lastEvalTime + `Math.Abs(LocalFxTime - _lastEvalTime) < 0.0002f` early-return is DROPPED.
//     frame_cook cooks each node exactly once per frame, so the sub-ms double-eval that guard
//     prevents cannot occur. We therefore store NO _lastEvalTime.
//   • TiXL inits _lastHitTime = 0 (default double). With wall `time` starting at 0, the very first
//     qualifying change has timeSinceLastHit = |time - 0| = time ≥ minTimeBetweenHits only once
//     `time` has advanced past it — faithful to TiXL's own first-hit timing (both start the clock
//     at 0). No fork; just noting the shared origin.
// MathUtils.WasTriggered(cur, ref prev) = rising edge: result = cur && !prev; then prev = cur.
void stepHasValueChanged(const std::map<std::string, float>& in, float /*dt*/, float time,
                         StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float newValue = getIn(in, "Value", 0.0f);
  const float threshold = getIn(in, "Threshold", 0.0f);
  const float minTimeBetweenHits = getIn(in, "MinTimeBetweenHits", 0.0f);
  const bool preventContinuedChanges = getIn(in, "PreventContinuedChanges", 0.0f) > 0.5f;
  const int mode = (int)std::lround(getIn(in, "Mode", 0.0f));

  float& lastValue = st.s[0];
  float& lastHitTime = st.s[1];
  float& lastHitDelta = st.s[2];

  const float absDelta = std::fabs(newValue - lastValue);
  bool hasChanged = false;
  switch (mode) {
    case 1:  hasChanged = newValue > lastValue + threshold; break;  // Increased
    case 2:  hasChanged = newValue < lastValue - threshold; break;  // Decreased
    default: hasChanged = absDelta > threshold; break;              // Changed (0)
  }

  // MathUtils.WasTriggered(hasChanged, ref _wasHit): rising edge, then store current.
  const bool prevWasHit = st.s[3] > 0.5f;
  const bool wasTriggered = hasChanged && !prevWasHit;
  st.s[3] = hasChanged ? 1.0f : 0.0f;

  if (hasChanged && (preventContinuedChanges || wasTriggered)) {
    const float timeSinceLastHit = std::fabs(time - lastHitTime);
    if (timeSinceLastHit >= minTimeBetweenHits) {
      lastHitTime = time;
      lastHitDelta = absDelta;
    } else {
      hasChanged = false;
    }
  }

  out[0] = hasChanged ? 1.0f : 0.0f;
  out[1] = newValue - lastValue;
  lastValue = newValue;
  out[2] = lastHitDelta;
}

// --- DetectPulse (TiXL float/process/DetectPulse.cs) ---
// Detects a downward "pulse": when the input drops fast enough that (damped − new) exceeds
// Threshold, fires HasChanged once on the rising edge of that condition, gated by MinTimeBetweenHits.
// Bool output dissolves to Float 0/1 (Cut 32: no Bool port type). Outputs:
//   HasChanged(0/1) = isHit ; DebugValue = deltaToDamped (= dampedValue − newValue, PRE-update).
// Ports/inputs (TiXL decl order): Value, Threshold, Damping, MinTimeBetweenHits.
//   .t3 source defaults (DetectPulse.t3): Value=1.0, Threshold=0.0, Damping=0.95, MinTimeBetweenHits=0.075.
// State: s[0]=lastHitTime, s[1]=wasHit(0/1), s[2]=dampedValue.
//   TiXL inits _lastHitTime = double.NegativeInfinity → represented as -1e30f so the first qualifying
//   pulse always clears the gate (time − (−1e30) ≫ minTime). _wasHit=false(0), _dampedValue=0.
//   The `init` flag seeds s[0]=-1e30 once (zero-init would otherwise put it at 0).
// Time: TiXL uses context.LocalFxTime for the hasTimeDecreased reset + the MinTimeBetweenHits gate;
//   frame_cook hands wall seconds via `time`, the same substitution Ease/HasValueChanged use. `dt` unused.
// Fork (named) — same precedent as Damp/Ease/HasValueChanged:
//   • context.LocalFxTime → the seam's `time` param (wall seconds). DetectPulse has NO sub-ms
//     early-return guard, so there is nothing else to drop (no _lastEvalTime exists in the .cs).
//   • TiXL's Log.Debug(...) diagnostic line is a pure side-effect-free logging call → dropped (no
//     behavior change to any output).
// MathUtils.WasTriggered(cur, ref prev) = rising edge: result = cur && !prev; then prev = cur.
// QUIRK kept VERBATIM: the inner `if (timeSinceLastHit >= minTimeBetweenHits)` is a redundant
//   re-check of the SAME condition already in the outer `if` — a TiXL source quirk, preserved as-is.
void stepDetectPulse(const std::map<std::string, float>& in, float /*dt*/, float time,
                     StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float newValue = getIn(in, "Value", 1.0f);
  const float threshold = getIn(in, "Threshold", 0.0f);
  const float minTimeBetweenHits = getIn(in, "MinTimeBetweenHits", 0.075f);

  if (!st.init) {
    st.s[0] = -1e30f;  // TiXL _lastHitTime = double.NegativeInfinity
    st.init = true;
  }
  float& lastHitTime = st.s[0];
  float& dampedValue = st.s[2];

  // hasTimeDecreased = context.LocalFxTime < _lastHitTime; if so _lastHitTime = 0.
  if (time < lastHitTime) lastHitTime = 0.0f;

  // deltaToDamped uses the PRE-update damped value.
  const float deltaToDamped = dampedValue - newValue;

  // dampFactor = Damping.Clamp(0,1); _dampedValue = Lerp(newValue, _dampedValue, dampFactor).
  float dampFactor = getIn(in, "Damping", 0.95f);
  if (dampFactor < 0.0f) dampFactor = 0.0f;
  else if (dampFactor > 1.0f) dampFactor = 1.0f;
  dampedValue = lerpf(newValue, dampedValue, dampFactor);

  out[1] = deltaToDamped;  // DebugValue.Value = deltaToDamped

  const bool exceedsThreshold = deltaToDamped > threshold;
  // MathUtils.WasTriggered(exceedsThreshold, ref _wasHit): rising edge, then store current.
  const bool prevWasHit = st.s[1] > 0.5f;
  const bool wasTriggered = exceedsThreshold && !prevWasHit;
  st.s[1] = exceedsThreshold ? 1.0f : 0.0f;

  bool isHit = false;
  const float timeSinceLastHit = time - lastHitTime;
  if (wasTriggered && timeSinceLastHit >= minTimeBetweenHits) {
    // VERBATIM TiXL quirk: redundant re-check of the same condition as the outer if.
    if (timeSinceLastHit >= minTimeBetweenHits) {
      lastHitTime = time;
      isHit = true;
    }
  }

  out[0] = isHit ? 1.0f : 0.0f;  // HasChanged.Value = isHit
}

inline int enumOf(const std::map<std::string, float>& in, const char* k) {
  return (int)std::lround(getIn(in, k, 0.0f));
}
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// --- Ease / EaseVec2 / EaseVec3 (TiXL float/process/Ease.cs, vec2/EaseVec2.cs, vec3/EaseVec3.cs) ---
// Time-based eased re-target: when the input changes by >0.001 the animation RESTARTS from the
// current output toward the new target over `Duration` seconds, shaped by Interpolation×Direction
// (EasingFunctions). Uses the wall `time` param as TiXL's currentTime; `dt` unused (absolute-time).
// State (per spec) packs startTime + initial/target/prevInput per component, PLUS one shared
// `prevEased` float at the END (see below):
//   Ease     s[0]=startTime, s[1]=initial, s[2]=target, s[3]=prevInput, s[4]=prevEased
//   EaseVec2 s[0]=startTime, s[1..2]=initial, s[3..4]=target, s[5..6]=prevInput, s[7]=prevEased
//   EaseVec3 s[0]=startTime, s[1..3]=initial, s[4..6]=target, s[7..9]=prevInput, s[10]=prevEased (≤12)
// Why prevEased: TiXL captures `_initialValue = Result.Value` on restart — i.e. LAST frame's output.
// frame_cook hands easeImpl a freshly-zeroed out[] each frame (it does NOT carry the prior Result),
// so we must reconstruct it. Between restarts initial/target are fixed, so last-frame's Result is
// exactly lerp(initial,target,prevEased) — and easing is scalar (one t for all components), so ONE
// stored float reconstructs the previous Result for any N. This is faithful to TiXL and fits s[12]
// (the spec's "out[] carries prior Result" assumption is broken by frame_cook's zeroing; this is the
// minimal correct substitute — flagged in the dossier).
// Fork (named) — same precedent as Damp/Spring (batch25):
//   • UseAppRunTime input DROPPED — frame_cook always cooks once per frame with wall time (TiXL's
//     non-default RunTimeInSecs branch has no analog here; we always use context-time = `time`).
//   • The 1ms MinTimeElapsedBeforeEvaluation early-return DROPPED — frame_cook cooks exactly once
//     per frame, so the sub-ms double-eval that guard prevents cannot occur.
//   • The __MotionBlurPass skip DROPPED — no motion-blur pass system in this runtime.
// Faithful: _previousInput field-inits to 0, so on frame 1 a nonzero input triggers an immediate
// restart capturing initialValue=prevResult=0 (matches TiXL's exact first-frame behavior).
void easeImpl(const std::map<std::string, float>& in, float time, StatefulValueState& st,
              float out[3], int N) {
  float duration = getIn(in, "Duration", 1.0f);
  if (duration == 0.0f) duration = 0.0001f;  // TiXL guard
  const int direction = enumOf(in, "Direction");
  const int easeMode = enumOf(in, "Interpolation");

  float input[3] = {0, 0, 0};
  for (int c = 0; c < N; ++c) input[c] = (N == 1) ? getIn(in, "Value", 0.0f) : getInC(in, "Value", c);

  float& startTime = st.s[0];
  float* initial = &st.s[1];           // s[1..N]
  float* target = &st.s[1 + N];        // s[1+N..1+2N-1]
  float* prevInput = &st.s[1 + 2 * N]; // s[1+2N..1+3N-1]
  float& prevEased = st.s[1 + 3 * N];  // shared scalar t of last frame's output

  // Restart trigger: scalar abs() / vec Distance() > 0.001 (TiXL Math.Abs / VectorN.Distance).
  float distSq = 0.0f;
  for (int c = 0; c < N; ++c) {
    const float d = input[c] - prevInput[c];
    distSq += d * d;
  }
  if (std::sqrt(distSq) > 0.001f) {
    startTime = time;
    for (int c = 0; c < N; ++c) {
      initial[c] = lerpf(initial[c], target[c], prevEased);  // TiXL _initialValue = Result.Value (last frame)
      target[c] = input[c];
    }
  }

  const float elapsed = time - startTime;
  const float progress = clamp01(elapsed / duration);
  const float eased = applyEasing(progress, direction, easeMode);
  for (int c = 0; c < N; ++c) {
    out[c] = lerpf(initial[c], target[c], eased);
    prevInput[c] = input[c];
  }
  prevEased = eased;
}
void stepEase(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { easeImpl(in, time, st, out, 1); }
void stepEaseVec2(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { easeImpl(in, time, st, out, 2); }
void stepEaseVec3(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { easeImpl(in, time, st, out, 3); }

// --- Accumulator (TiXL float/process/Accumulator.cs) — a running accumulator. Running gates
// accumulation, ResetTrigger reloads StartValue, Accumulate mode picks the per-step amount
// (PerFrame=+Increment, PerSeconds=+Increment*dt), Modulo>0 wraps the output. State: s[0]=v.
// .t3 defaults: Increment=1, StartValue=0, Modulo=0, Running=true, ResetTrigger=false, Accumulate=0.
// Fork (named): TiXL computes dt = Playback.SecondsFromBars(LocalFxTime) - _lastUpdateTime; we use
// the seam's wall `dt` directly (== bars→seconds delta at constant BPM), dropping _lastUpdateTime.
// TiXL has no _isFirstEval: _v starts 0 (NOT StartValue) until a ResetTrigger fires — faithful.
void stepAccumulator(const std::map<std::string, float>& in, float dt, float /*time*/,
                     StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool running = getIn(in, "Running", 1.0f) > 0.5f;
  const int mode = (int)std::lround(getIn(in, "Accumulate", 0.0f));
  const float startValue = getIn(in, "StartValue", 0.0f);
  if (getIn(in, "ResetTrigger", 0.0f) > 0.5f) {
    st.s[0] = startValue;  // TiXL also writes Result=startValue here; final out below overwrites it
  }
  const float increment = getIn(in, "Increment", 1.0f);
  if (running) {
    const float f = (mode == 1) ? dt : 1.0f;  // PerSeconds(1) => dt, PerFrame(0)/else => 1
    st.s[0] += increment * f;
  }
  const float modulo = getIn(in, "Modulo", 0.0f);
  out[0] = modulo > 0.0f ? std::fmod(st.s[0], modulo) : st.s[0];  // TiXL: modulo>0 ? _v % modulo : _v
}

// --- HasVec2Changed (TiXL vec2/HasVec2Changed.cs) — fires when Value moves > Threshold (Euclidean
// distance). Outputs: HasChanged(Bool→Float 0/1), Delta.x, Delta.y (signed newValue−lastValue).
// State: s[0]=lastValue.x, s[1]=lastValue.y, s[2]=lastHitTime, s[3]=wasHit. Mirrors HasValueChanged.
// Fork (same as HasValueChanged): drop the Playback.RunTimeInSecs 0.010 dedup early-return; use the
// seam `time` (wall seconds) for the MinTimeBetweenHits gate (TiXL context.LocalFxTime).
void stepHasVec2Changed(const std::map<std::string, float>& in, float /*dt*/, float time,
                        StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float nx = getInC(in, "Value", 0), ny = getInC(in, "Value", 1);
  const float threshold = getIn(in, "Threshold", 0.0f);
  const float minTime = getIn(in, "MinTimeBetweenHits", 0.0f);
  const bool prevent = getIn(in, "PreventContinuedChanges", 0.0f) > 0.5f;
  float& lx = st.s[0];
  float& ly = st.s[1];
  const float dx = nx - lx, dy = ny - ly;
  const float dist = std::sqrt(dx * dx + dy * dy);  // TiXL Vector2.Distance
  bool hasChanged = dist > threshold;
  const bool prevWasHit = st.s[3] > 0.5f;
  const bool wasTriggered = hasChanged && !prevWasHit;  // MathUtils.WasTriggered
  st.s[3] = hasChanged ? 1.0f : 0.0f;
  if (hasChanged && (prevent || wasTriggered)) {
    if (time - st.s[2] >= minTime) st.s[2] = time;
    else hasChanged = false;
  }
  out[0] = hasChanged ? 1.0f : 0.0f;
  out[1] = dx;  // Delta = newValue - lastValue (signed)
  out[2] = dy;
  lx = nx;
  ly = ny;
}

// --- HasVec3Changed (TiXL vec3/HasVec3Changed.cs) — 7-output sibling of HasValueChanged for Vec3.
// Mode(Changed=any |Δcomp|>thr / Increased / Decreased). Outputs: HasChanged(0/1), Delta.xyz(signed
// out[1..3]), DeltaOnHit.xyz(abs Δ at last hit, out[4..6]). State: s[0..2]=lastValue, s[3]=lastHitTime,
// s[4]=wasHit, s[5..7]=lastHitDelta. Fork (same as HasValueChanged): drop Playback 0.010 dedup; seam
// `time` for the MinTimeBetweenHits gate.
void stepHasVec3Changed(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[8], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float nx = getInC(in, "Value", 0), ny = getInC(in, "Value", 1), nz = getInC(in, "Value", 2);
  const float threshold = getIn(in, "Threshold", 0.0f);
  const float minTime = getIn(in, "MinTimeBetweenHits", 0.0f);
  const bool prevent = getIn(in, "PreventContinuedChanges", 0.0f) > 0.5f;
  int mode = (int)std::lround(getIn(in, "Mode", 0.0f));
  if (mode < 0) mode = 0; else if (mode > 2) mode = 2;  // TiXL Clamp(0, len-1)
  const float lx = st.s[0], ly = st.s[1], lz = st.s[2];
  const float ax = std::fabs(nx - lx), ay = std::fabs(ny - ly), az = std::fabs(nz - lz);
  bool hasChanged;
  if (mode == 1)       hasChanged = nx > lx + threshold || ny > ly + threshold || nz > lz + threshold;  // Increased
  else if (mode == 2)  hasChanged = nx < lx - threshold || ny < ly - threshold || nz < lz - threshold;  // Decreased
  else                 hasChanged = ax > threshold || ay > threshold || az > threshold;                 // Changed
  const bool prevWasHit = st.s[4] > 0.5f;
  const bool wasTriggered = hasChanged && !prevWasHit;
  st.s[4] = hasChanged ? 1.0f : 0.0f;
  if (hasChanged && (prevent || wasTriggered)) {
    if (time - st.s[3] >= minTime) { st.s[3] = time; st.s[5] = ax; st.s[6] = ay; st.s[7] = az; }
    else hasChanged = false;
  }
  out[0] = hasChanged ? 1.0f : 0.0f;
  out[1] = nx - lx; out[2] = ny - ly; out[3] = nz - lz;  // Delta = newValue - lastValue (signed)
  st.s[0] = nx; st.s[1] = ny; st.s[2] = nz;
  out[4] = st.s[5]; out[5] = st.s[6]; out[6] = st.s[7];  // DeltaOnHit (abs Δ at last hit)
}

// --- PeakLevel (TiXL float/process/PeakLevel.cs) — 4 outputs: AttackLevel(Δ since last frame),
// FoundPeak(0/1 when a rising step > Threshold and ≥ MinTimeBetweenPeaks since the last peak),
// TimeSincePeak, MovingSum(running Σ of increases, wrapped at ±30000 for float precision). State:
// s[0]=lastValue, s[1]=lastPeakTime(init −∞), s[2]=movingSum. Fork: drop the FxTime 0.001 dedup; seam
// `time` (wall secs) for Playback.RunTimeInSecs. MovingSum is a feedback accumulator (reads its own
// prior output = state, like Ease reads Result.Value).
void stepPeakLevel(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[8], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  if (!st.init) { st.s[1] = -1e30f; st.init = true; }  // _lastPeakTime = double.NegativeInfinity
  const float value = getIn(in, "Value", 0.0f);
  const float threshold = getIn(in, "Threshold", 0.0f);
  const float minTime = getIn(in, "MinTimeBetweenPeaks", 0.0f);
  const float increase = value - st.s[0];
  const float timeSinceLastPeak = time - st.s[1];
  if (timeSinceLastPeak < 0.0f) st.s[1] = -1e30f;  // seek-backward: reset field (local stays, faithful)
  bool foundPeak = false;
  if (increase > threshold && timeSinceLastPeak > minTime) { st.s[1] = time; foundPeak = true; }
  float previousSum = st.s[2];
  const float precisionThreshold = 30000.0f;
  if (std::fabs(previousSum) > precisionThreshold) previousSum = std::fmod(previousSum, precisionThreshold);
  st.s[2] = previousSum + increase;
  out[0] = increase;                 // AttackLevel
  out[1] = foundPeak ? 1.0f : 0.0f;  // FoundPeak (Bool→Float)
  out[2] = timeSinceLastPeak;        // TimeSincePeak
  out[3] = st.s[2];                  // MovingSum
  st.s[0] = value;
}

// --- CountInt (TiXL Lib/numbers/int/logic/CountInt.cs) — a running integer counter that steps every
// evaluated frame TriggerIncrement / TriggerDecrement is held true (LEVEL, not edge), reloads
// DefaultValue on TriggerReset, and wraps by Modulo. The optional OnlyCountChanges gate skips the
// whole step on frames where neither trigger CHANGED since last frame. Stateful: the count must
// persist across frames and be reconstructed each cook (frame_cook hands a zeroed out[]).
// State: s[0]=count, s[1]=lastIncTrigger(0/1), s[2]=lastDecTrigger(0/1).
// .t3 defaults: TriggerIncrement=true, TriggerDecrement=false, TriggerReset=false, OnlyCountChanges=false,
//   Delta=1, DefaultValue=0, Modulo=0. Help doc: "counts evaluations as an integer" = free-running
//   per-frame counter (with defaults, output = 1,2,3,4,... over consecutive evaluated frames).
// TiXL Update() FULL (CountInt.cs:14-56), exact order:
//   var defaultValue = DefaultValue.GetValue(context);
//   var currentTime = context.LocalFxTime;
//   if (Math.Abs(currentTime - _lastEvalTime) < MinTimeElapsedBeforeEvaluation) return;   // sub-ms guard
//   _lastEvalTime = currentTime;
//   var triggeredIncrement = TriggerIncrement.GetValue(context);                            // raw LEVEL
//   var triggeredDecrement = TriggerDecrement.GetValue(context);
//   var notChanged = triggeredIncrement == _lastIncrementTrigger && triggeredDecrement == _lastDecrementTrigger;
//   if (OnlyCountChanges.GetValue(context) && notChanged) return;                           // gate
//   _lastIncrementTrigger = triggeredIncrement; _lastDecrementTrigger = triggeredDecrement;
//   var delta = Delta.GetValue(context);
//   if (triggeredIncrement) Result.Value += delta; else if (triggeredDecrement) Result.Value -= delta;
//   if (!_initialized || TriggerReset.GetValue(context)) { Result.Value = defaultValue; _initialized = true; }
//   var modulo = Modulo.GetValue(context); if (modulo != 0) Result.Value %= modulo;
// Note _lastIncrementTrigger/_lastDecrementTrigger feed ONLY the OnlyCountChanges gate — they NEVER
// gate the increment itself. The increment is the raw LEVEL `if (triggeredIncrement)`.
// Forks (named):
//   • The `MinTimeElapsedBeforeEvaluation` (1/10000s) sub-ms double-eval guard is DROPPED — frame_cook
//     cooks each node exactly once per frame (same precedent as Damp/Spring/Ease). No _lastEvalTime.
//   • bool-as-float threshold 0.5: TriggerIncrement/TriggerDecrement/TriggerReset/OnlyCountChanges read
//     from Float ports as >0.5 (Cut 32: no Bool port type).
//   • int-on-float-port: Delta/DefaultValue/Modulo/count arrive on Float ports but are int-typed in
//     TiXL — converted by C#-`(int)` TRUNCATION toward zero ((long)std::trunc), NOT rounding, so 1.9→1.
//     C# integer `%` (truncated remainder, sign of dividend) is the native long `%`. The count is kept
//     in s[0] as an exact integer-valued float (|count| ≪ 2^24 in practice → no float-int drift).
//   • zeroed-out[] reconstruct: TiXL's Result.Value Slot persists across frames; here frame_cook zeroes
//     out[] each cook, so the count is reconstructed from s[0] (and the OnlyCountChanges early-return
//     still re-emits s[0] so the output holds, mirroring TiXL keeping Result.Value untouched).
//   • Reset reloads DefaultValue AFTER the inc/dec step (TiXL order: the reset overwrites), so on a
//     frame with both inc held AND TriggerReset, the result is DefaultValue (then Modulo).
void stepCountInt(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                  StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool incTrig = getIn(in, "TriggerIncrement", 1.0f) > 0.5f;   // .t3 default TriggerIncrement=true
  const bool decTrig = getIn(in, "TriggerDecrement", 0.0f) > 0.5f;
  const bool resetTrig = getIn(in, "TriggerReset", 0.0f) > 0.5f;
  const bool onlyCountChanges = getIn(in, "OnlyCountChanges", 0.0f) > 0.5f;
  const long delta = (long)std::trunc(getIn(in, "Delta", 1.0f));          // C# (int) cast = truncate
  const long defaultValue = (long)std::trunc(getIn(in, "DefaultValue", 0.0f));
  const long modulo = (long)std::trunc(getIn(in, "Modulo", 0.0f));

  long count = (long)std::trunc(st.s[0]);

  // OnlyCountChanges gate (CountInt.cs:28-30): skip the WHOLE step (inc/dec/reset/modulo) on frames
  // where neither trigger CHANGED since last frame. _lastInc/_lastDec are NOT updated on early-return
  // (they're already equal → notChanged), and Result.Value is left untouched → re-emit persisted count.
  const bool prevInc = st.s[1] > 0.5f;
  const bool prevDec = st.s[2] > 0.5f;
  const bool notChanged = (incTrig == prevInc) && (decTrig == prevDec);
  if (onlyCountChanges && notChanged) {
    out[0] = (float)count;   // TiXL returns with Result.Value held; reconstruct from s[0]
    return;
  }

  // CountInt.cs:32-33 — store current triggers (feed ONLY the gate above, never the increment).
  st.s[1] = incTrig ? 1.0f : 0.0f;
  st.s[2] = decTrig ? 1.0f : 0.0f;

  // CountInt.cs:36-43 — LEVEL increment: fires every evaluated frame the trigger is held true.
  if (incTrig)      count += delta;   // TiXL: if (triggeredIncrement) Result.Value += delta
  else if (decTrig) count -= delta;   //       else if (triggeredDecrement) Result.Value -= delta

  if (!st.init || resetTrig) {        // CountInt.cs:45-49 — !_initialized || TriggerReset → defaultValue
    count = defaultValue;
    st.init = true;
  }
  if (modulo != 0) count %= modulo;   // CountInt.cs:51-55 — Result.Value %= modulo (C# truncated remainder)

  st.s[0] = (float)count;
  out[0] = (float)count;
}

// --- FlipBool (TiXL Lib/numbers/bool/logic/FlipBool.cs) — a latched boolean that TOGGLES on the
// rising edge of Trigger and reloads DefaultValue on ResetTrigger (reset wins). Bool dissolves to
// Float 0/1 (Cut 32: no Bool port type). Stateful: the latched bool must persist + be reconstructed
// each cook (out[] is zeroed). State: s[0]=current bool(0/1), s[1]=lastTrigger(0/1).
// .t3 defaults: Trigger=false, ResetTrigger=false, DefaultValue=false.
// TiXL Update() (FlipBool.cs:21-34):
//   var isTriggered = MathUtils.WasTriggered(Trigger.GetValue(context), ref _triggered);
//   var isReset = ResetTrigger.GetValue(context); var defaultValue = DefaultValue.GetValue(context);
//   if (isReset) Result.Value = defaultValue; else if (isTriggered) Result.Value = !Result.Value;
// (Rising-edge toggle is already faithful in the .cs — no edge fork needed, unlike CountInt.)
// Forks (named):
//   • bool-as-float threshold 0.5: Trigger/ResetTrigger/DefaultValue read from Float ports as >0.5;
//     Result emitted as 1.0/0.0.
//   • No init seeding: TiXL Result.Value starts false (Slot<bool> default); s[0] zero-init = false,
//     faithful. ResetTrigger is LEVEL (every frame it is held true forces DefaultValue — TiXL's
//     `if (isReset)`), only the toggle is edge-gated.
void stepFlipBool(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                  StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool trigger = getIn(in, "Trigger", 0.0f) > 0.5f;
  const bool reset = getIn(in, "ResetTrigger", 0.0f) > 0.5f;
  const bool defaultValue = getIn(in, "DefaultValue", 0.0f) > 0.5f;

  // MathUtils.WasTriggered(Trigger, ref _triggered): rising edge, then store current.
  const bool prevTrig = st.s[1] > 0.5f;
  const bool isTriggered = trigger && !prevTrig;
  st.s[1] = trigger ? 1.0f : 0.0f;

  bool result = st.s[0] > 0.5f;
  if (reset)              result = defaultValue;  // TiXL: reset wins (checked first)
  else if (isTriggered)   result = !result;       //       else toggle on rising edge

  st.s[0] = result ? 1.0f : 0.0f;
  out[0] = st.s[0];
}

// --- HasIntChanged (TiXL Lib/numbers/int/logic/HasIntChanged.cs) — emits HasChanged(Bool→Float 0/1)
// when this frame's (int-truncated) Value differs from last frame's, by Mode. State: s[0]=lastValue.
// .t3 defaults: Value=0, ReturnTrueIf=3 (Changed). Modes enum: Never=0, Increased=1, Decreased=2, Changed=3.
// TiXL Update() (HasIntChanged.cs:23-41):
//   var v = Value.GetValue(context); var result = false;
//   switch ((Modes)ReturnTrueIf...) { Increased: v>_lastValue; Decreased: v<_lastValue; Changed: v!=_lastValue; }
//   HasChanged.Value = result; _lastValue = v;
// (Never(0) and any out-of-range mode → result stays false, faithful to the switch default.)
// Forks (named):
//   • The `Math.Abs(LocalFxTime - _lastEvalTime) < double.Epsilon` sub-frame double-eval guard is
//     DROPPED — frame_cook cooks once per frame (Damp/Spring/Ease precedent). No _lastEvalTime.
//   • int-on-float-port: Value arrives on a Float port but is int-typed in TiXL — converted by
//     C#-`(int)` TRUNCATION toward zero ((long)std::trunc), NOT rounding, so 4.9→4. The prior int is
//     held in s[0] as an integer-valued float. (ReturnTrueIf is an enum selector → std::lround is fine.)
//   • Bool output dissolves to Float 0/1 (Cut 32). Frame 1 compares against the zero-init lastValue
//     (0), matching TiXL's _lastValue=0 field default.
void stepHasIntChanged(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                       StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const long v = (long)std::trunc(getIn(in, "Value", 0.0f));  // C# (int) cast = truncate toward zero
  const int mode = (int)std::lround(getIn(in, "ReturnTrueIf", 3.0f));
  const long lastValue = (long)std::trunc(st.s[0]);

  bool result = false;
  switch (mode) {
    case 1:  result = v > lastValue; break;   // Increased
    case 2:  result = v < lastValue; break;   // Decreased
    case 3:  result = v != lastValue; break;  // Changed
    default: result = false; break;           // Never(0) / out-of-range → false (switch default)
  }
  out[0] = result ? 1.0f : 0.0f;
  st.s[0] = (float)v;
}

// --- ToggleBoolean (TiXL Lib/numbers/bool/logic/ToggleBoolean.cs) — a latched bool that flips its
// held state when TriggerToggle fires and clears it when TriggerReset fires. Bool dissolves to Float
// 0/1 (Cut 32). Stateful: the latch persists + is reconstructed each cook (out[] is zeroed).
// State: s[0]=isActive(0/1), s[1]=lastToggle(0/1), s[2]=lastReset(0/1).
// .t3 defaults: TriggerToggle=false, TriggerReset=false. Description: "When triggered toggles from
//   true to false and back."
// TiXL Update() (ToggleBoolean.cs:20-37), exact order:
//   var triggerToggle = TriggerToggle.GetValue(context);
//   if (triggerToggle) { TriggerToggle.SetTypedInputValue(false); _isActive = !_isActive; }
//   var triggerReset = TriggerReset.GetValue(context);
//   if (triggerReset)  { TriggerReset.SetTypedInputValue(false);  _isActive = false; }
//   Result.Value = _isActive;
// BEHAVIOR (backward-traced, NOT assumed): the `SetTypedInputValue(false)` immediately writes the
//   trigger input back to false the SAME frame it fires. So in TiXL a held-true button toggles EXACTLY
//   ONCE (the op debounces its own input) — it is effectively RISING-EDGE, not level. Reset is checked
//   AFTER toggle (so a frame with both flips THEN clears → ends false).
// Forks (named):
//   • input-writeback → rising-edge reconstruct: our Float ports are read-only at cook time (no
//     SetTypedInputValue analog), so the self-clearing once-per-press behavior is replicated by
//     detecting the trigger's RISING EDGE (WasTriggered) from stored state. A held-true trigger thus
//     toggles once (faithful to TiXL's debounced single toggle), not every frame.
//   • bool-as-float threshold 0.5: TriggerToggle/TriggerReset read from Float ports as >0.5; Result
//     emitted as 1.0/0.0.
//   • No init seeding: TiXL _isActive starts false; s[0] zero-init = false, faithful.
void stepToggleBoolean(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                       StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool toggle = getIn(in, "TriggerToggle", 0.0f) > 0.5f;
  const bool reset = getIn(in, "TriggerReset", 0.0f) > 0.5f;

  // Rising-edge of each trigger (the SetTypedInputValue(false) self-clear == an edge debounce).
  const bool prevToggle = st.s[1] > 0.5f;
  const bool prevReset = st.s[2] > 0.5f;
  const bool toggleEdge = toggle && !prevToggle;  // MathUtils.WasTriggered analog
  const bool resetEdge = reset && !prevReset;
  st.s[1] = toggle ? 1.0f : 0.0f;
  st.s[2] = reset ? 1.0f : 0.0f;

  bool active = st.s[0] > 0.5f;
  if (toggleEdge) active = !active;  // TiXL: if (triggerToggle) _isActive = !_isActive (then clears)
  if (resetEdge)  active = false;    // TiXL: if (triggerReset) _isActive = false (checked AFTER toggle)

  st.s[0] = active ? 1.0f : 0.0f;
  out[0] = st.s[0];
}

// --- FlipFlop (TiXL Lib/numbers/bool/logic/FlipFlop.cs) — a latch that SETS to true on a LEVEL Trigger
// and reloads DefaultValue on a LEVEL ResetTrigger (reset wins); otherwise HOLDS its prior value. Bool
// dissolves to Float 0/1 (Cut 32). Stateful: the latch persists + is reconstructed each cook.
// State: s[0]=result(0/1).
// .t3 defaults: DefaultValue=false, Trigger=false, ResetTrigger=false. Description: "Holds the
//   \"activated\" state of a boolean."
// TiXL Update() (FlipFlop.cs:22-40):
//   var isTriggered = Trigger.GetValue(context); var isReset = ResetTrigger.GetValue(context);
//   var defaultValue = DefaultValue.GetValue(context);
//   if (isReset) Result.Value = defaultValue; else if (isTriggered) Result.Value = true;
//   // (no else → Result is left UNCHANGED when neither fires: it HOLDS.)
// BEHAVIOR (backward-traced, NOT assumed): both Trigger and ResetTrigger are read as RAW LEVELS — NO
//   WasTriggered/edge anywhere (contrast FlipBool, which DOES edge-gate its toggle). Trigger only ever
//   sets to true (never clears); the ONLY way back to false is ResetTrigger with DefaultValue=false.
//   When neither is true the prior value HOLDS (no else branch). Reset wins (checked first).
// Forks (named):
//   • bool-as-float threshold 0.5: Trigger/ResetTrigger/DefaultValue read from Float ports as >0.5;
//     Result emitted as 1.0/0.0.
//   • No init seeding: TiXL Result.Value starts false (Slot<bool> default); s[0] zero-init = false,
//     faithful. (No _isFirstEval in the .cs — frame 1 with no trigger holds the zero-init false.)
//   • zeroed-out[] reconstruct: TiXL Result.Value persists across frames; here out[] is zeroed each
//     cook, so the held value is reconstructed from s[0] (re-emitted on hold frames).
void stepFlipFlop(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                  StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool trigger = getIn(in, "Trigger", 0.0f) > 0.5f;
  const bool reset = getIn(in, "ResetTrigger", 0.0f) > 0.5f;
  const bool defaultValue = getIn(in, "DefaultValue", 0.0f) > 0.5f;

  bool result = st.s[0] > 0.5f;     // reconstruct prior value (HOLD when neither fires)
  if (reset)         result = defaultValue;  // TiXL: reset wins (checked first)
  else if (trigger)  result = true;          //       else LEVEL set-to-true (never clears)

  st.s[0] = result ? 1.0f : 0.0f;
  out[0] = st.s[0];
}

// --- HasBooleanChanged (TiXL Lib/numbers/bool/logic/HasBooleanChanged.cs) — emits HasChanged(Bool→
// Float 0/1) when this frame's bool Value differs from last frame's, by Mode. Bool dissolves to Float
// 0/1 (Cut 32). State: s[0]=lastValue(0/1).
// .t3 defaults: Value=false, Mode=1 (= Increased — NOT Changed; see BEHAVIOR). Description: "Returns
//   true if the connected input changed, either from False to True or vice versa."
// Modes enum (HasBooleanChanged.cs:42-47): Changed=0, Increased=1, Decreased=2.
// TiXL Update() (HasBooleanChanged.cs:21-32):
//   var newValue = Value.GetValue(context);
//   bool hasChanged = (Modes)Mode.GetValue(context).Clamp(0, len-1) switch {
//       Changed   => newValue != _lastValue,
//       Increased => newValue != _lastValue && newValue,    // edge False→True only
//       Decreased => newValue != _lastValue && !newValue,   // edge True→False only
//   };
//   HasChanged.Value = hasChanged; _lastValue = newValue;
// BEHAVIOR (backward-traced, NOT assumed): the .t3 DEFAULT Mode is INCREASED(1), so out-of-the-box the
//   op fires ONLY on a False→True transition (a rising edge), NOT on every change. Changed(0) fires on
//   either direction; Decreased(2) fires only on True→False. There is NO time gate, NO WasTriggered
//   latch — it is a pure one-frame comparison against the previous frame's value.
// Forks (named):
//   • bool-as-float threshold 0.5: Value read from a Float port as >0.5; HasChanged emitted as 1.0/0.0.
//     Mode arrives on a Float port (enum selector) → std::lround. Clamp(0,2) per TiXL.
//   • Frame 1 compares against the zero-init s[0] (=false), matching TiXL's _lastValue=false default.
void stepHasBooleanChanged(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                           StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool newValue = getIn(in, "Value", 0.0f) > 0.5f;
  int mode = (int)std::lround(getIn(in, "Mode", 1.0f));  // .t3 default Mode=Increased(1)
  if (mode < 0) mode = 0; else if (mode > 2) mode = 2;   // TiXL Clamp(0, len-1)
  const bool lastValue = st.s[0] > 0.5f;

  bool hasChanged;
  switch (mode) {
    case 1:  hasChanged = (newValue != lastValue) && newValue;   break;  // Increased (False→True)
    case 2:  hasChanged = (newValue != lastValue) && !newValue;  break;  // Decreased (True→False)
    default: hasChanged = (newValue != lastValue);               break;  // Changed (either)
  }
  out[0] = hasChanged ? 1.0f : 0.0f;
  st.s[0] = newValue ? 1.0f : 0.0f;
}

// --- Trigger (TiXL Lib/numbers/bool/logic/Trigger.cs) — a bool gate that either passes its BoolValue
// straight through (OnlyOnDown=false) or emits a one-frame pulse on the RISING edge of BoolValue
// (OnlyOnDown=true, the .t3 default). Bool dissolves to Float 0/1 (Cut 32). Stateful: the rising-edge
// detection needs the PRIOR frame's BoolValue. State: s[0]=isSet (prev BoolValue, 0/1).
// .t3 defaults: OnlyOnDown=true, BoolValue=false (ColorInGraph is a graph-cosmetic input → dropped,
//   it never touches an output; see fork). Description (none in .cs; behavior is the WasTriggered gate).
// TiXL Update() (Trigger.cs:20-31):
//   if (!context.HasTimeChanged(ref _lastUpdateTime)) return;            // once-per-frame guard
//   var value = BoolValue.GetValue(context);
//   var wasHit = MathUtils.WasTriggered(value, ref _isSet);             // rising edge: value && !_isSet
//   var onlyOnDown = OnlyOnDown.GetValue(context);
//   Result.Value = onlyOnDown ? wasHit : value;                          // pulse-on-edge OR pass-through
//   // (DirtyFlag bookkeeping for the next-frame refresh — render-graph only, no output effect.)
// BEHAVIOR (backward-traced, NOT assumed): with the .t3 DEFAULT OnlyOnDown=true, the op fires a
//   single-frame TRUE only on the frame BoolValue goes false→true; a held-true input pulses ONCE
//   (the WasTriggered edge). With OnlyOnDown=false it is a transparent pass-through of the raw level.
// MathUtils.WasTriggered(cur, ref prev) = rising edge: result = cur && !prev; then prev = cur.
// Forks (named):
//   • bool-as-float threshold 0.5: BoolValue/OnlyOnDown read from Float ports as >0.5; Result
//     emitted as 1.0/0.0.
//   • The `HasTimeChanged` once-per-frame early-return is DROPPED — frame_cook cooks each node
//     exactly once per frame (Damp/Spring/Ease precedent). No _lastUpdateTime stored.
//   • ColorInGraph (Vector4) input DROPPED — it only tints the node body in TiXL's editor and never
//     influences Result; it has no port here (no behavior change to the output).
//   • No init seeding: TiXL _isSet starts false; s[0] zero-init = false, faithful → a true BoolValue
//     on frame 1 is itself a rising edge (false→true) and pulses, matching TiXL.
void stepTrigger(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                 StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool value = getIn(in, "BoolValue", 0.0f) > 0.5f;
  const bool onlyOnDown = getIn(in, "OnlyOnDown", 1.0f) > 0.5f;  // .t3 default OnlyOnDown=true

  // MathUtils.WasTriggered(value, ref _isSet): rising edge, then store current.
  const bool prevSet = st.s[0] > 0.5f;
  const bool wasHit = value && !prevSet;
  st.s[0] = value ? 1.0f : 0.0f;

  out[0] = (onlyOnDown ? wasHit : value) ? 1.0f : 0.0f;
}

// --- KeepBoolean (TiXL Lib/numbers/bool/process/KeepBoolean.cs) — the BOOL twin of FreezeValue:
// sample-and-hold a bool, plus a TimeSinceFreeze clock. Bool dissolves to Float 0/1 (Cut 32).
// Outputs: Result(frozen bool 0/1), TimeSinceFreeze(seconds since the last freeze edge). Stateful.
// State: s[0]=frozenValue(0/1), s[1]=prevFreeze(0/1), s[2]=freezeTime (the time of the last rising
//   freeze edge). .t3 defaults: Value=false, Mode=0 (FreezeWhileTrue), Freeze=false.
// Modes enum (KeepBoolean.cs:62-66): FreezeWhileTrue=0, UpdateWhenSwitchingToTrue=1.
// TiXL Update() (KeepBoolean.cs:24-49):
//   var newValue = Value.GetValue(context); var freeze = Freeze.GetValue(context);
//   var mode = Mode...; var wasTriggered = MathUtils.WasTriggered(freeze, ref _freeze);
//   if (wasTriggered) _freezeTime = context.LocalTime;
//   if (mode == FreezeWhileTrue) { if (!freeze) _frozenValue = newValue; }
//   else { if (wasTriggered) _frozenValue = newValue; }
//   Result.Value = _frozenValue;
//   TimeSinceFreeze.Value = (float)(context.LocalTime - _freezeTime);
// BEHAVIOR (backward-traced, NOT assumed): the WasTriggered current (_freeze) is updated EVERY frame
//   on the rising edge BEFORE the mode branch — identical structure to the already-shipped FreezeValue
//   (this is its bool sibling). FreezeWhileTrue tracks the input while NOT frozen and holds while
//   frozen; UpdateWhenSwitchingToTrue samples ONCE on the freeze rising edge. _freezeTime moves only
//   on a rising freeze edge, so TimeSinceFreeze counts up from each fresh freeze.
// Time: TiXL uses context.LocalTime for _freezeTime + TimeSinceFreeze; frame_cook hands wall seconds
//   via `time`, the same substitution Ease/HasValueChanged/FreezeValue-family use. `dt` unused.
// Forks (named):
//   • bool-as-float threshold 0.5: Value/Freeze read from Float ports as >0.5; Result emitted 1.0/0.0.
//   • No init seeding: TiXL _frozenValue/_freeze start false, _freezeTime starts 0; s[] zero-init
//     matches all three (faithful). TimeSinceFreeze on frame 1 = time - 0 = wall `time` (TiXL's own
//     first-frame value, both clocks start at 0).
void stepKeepBoolean(const std::map<std::string, float>& in, float /*dt*/, float time,
                     StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const bool newValue = getIn(in, "Value", 0.0f) > 0.5f;
  const bool freeze = getIn(in, "Freeze", 0.0f) > 0.5f;
  const int mode = (int)std::lround(getIn(in, "Mode", 0.0f));

  // MathUtils.WasTriggered(freeze, ref _freeze): rising edge, then store current.
  const bool prevFreeze = st.s[1] > 0.5f;
  const bool wasTriggered = freeze && !prevFreeze;
  st.s[1] = freeze ? 1.0f : 0.0f;

  if (wasTriggered) st.s[2] = time;  // _freezeTime = LocalTime on the rising freeze edge

  if (mode == 0) {                   // FreezeWhileTrue: track while not frozen
    if (!freeze) st.s[0] = newValue ? 1.0f : 0.0f;
  } else if (wasTriggered) {         // UpdateWhenSwitchingToTrue: sample on the rising edge
    st.s[0] = newValue ? 1.0f : 0.0f;
  }

  out[0] = st.s[0];                  // Result (frozen bool 0/1)
  out[1] = time - st.s[2];           // TimeSinceFreeze (seconds)
}

// --- DampPeakDecay (TiXL Lib/numbers/floats/process/DampPeakDecay.cs) — a one-way peak follower:
// the output snaps UP instantly to a rising input but decays DOWN toward a falling input by Decay
// (a Lerp). Classic VU-meter / peak-hold envelope. Scalar despite the `floats/` namespace (single
// Value → single Result; the `_dampedValue` is one float). State: s[0]=dampedValue.
// .t3 default: Decay=0.05 (the InputSlot<float> ctor default; confirmed below).
// TiXL Update() (DampPeakDecay.cs:17-37):
//   var runTime = context.Playback.FxTimeInBars;
//   if (Math.Abs(runTime - _lastEvalTime) < 0.001f) { ...DirtyFlag.Clear(); return; }   // once-per-frame
//   _lastEvalTime = runTime;
//   var value = Value.GetValue(context);
//   _dampedValue = _dampedValue > value ? MathUtils.Lerp(_dampedValue, value, Decay) : value;
//   MathUtils.ApplyDefaultIfInvalid(ref _dampedValue, 0);
//   Result.Value = _dampedValue;
// BEHAVIOR (backward-traced, NOT assumed): asymmetric. When dampedValue > value (input falling below
//   the held peak) it eases down: Lerp(dampedValue, value, Decay) = dampedValue + (value-dampedValue)*
//   Decay. Otherwise (input ≥ held peak) it SNAPS to value (instant attack). Decay∈[0,1] is the
//   per-frame fraction of the gap closed on the way down; Decay=0 holds the peak forever, Decay=1
//   tracks instantly both ways. NaN/Inf guard resets to 0 (ApplyDefaultIfInvalid).
// Forks (named):
//   • The `context.Playback.FxTimeInBars` sub-frame (0.001) double-eval early-return is DROPPED —
//     frame_cook cooks each node exactly once per frame (Damp/Spring/Ease precedent). No _lastEvalTime,
//     so DampPeakDecay needs NO transport/Playback access (it is frame-rate-iterative, like Spring).
//   • No init seeding: TiXL _dampedValue starts 0; s[0] zero-init = 0, faithful. Frame 1 with a
//     positive Value snaps to it (0 > value is false → result = value), matching TiXL exactly.
//   • Decay default 0.05 read from the Float port (TiXL InputSlot<float> Decay = new(0.05f)).
void stepDampPeakDecay(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                       StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  const float decay = getIn(in, "Decay", 0.05f);
  float& damped = st.s[0];
  damped = (damped > value) ? lerpf(damped, value, decay) : value;  // ease down, snap up
  if (!std::isfinite(damped)) damped = 0.0f;  // TiXL MathUtils.ApplyDefaultIfInvalid(_dampedValue, 0)
  out[0] = damped;
}

// --- HasTimeChanged (TiXL Lib/numbers/anim/time/HasTimeChanged.cs) — a time-edge detector: compares
// this frame's clock to last frame's and emits HasChanged(Bool→Float 0/1) by Mode, plus DeltaTime
// (= time − _lastTime). Bool dissolves to Float 0/1 (Cut 32). The PRIOR clock value is the state.
// State: s[0]=lastTime. (s[0] zero-init = TiXL _lastTime double field default 0.)
// Outputs (TiXL output decl order, both DirtyFlagTrigger.Animated): HasChanged(out[0]), DeltaTime(out[1]).
// Ports/inputs (TiXL Input decl order — Threshold, Mode, WhichTime; .t3 default order differs but the
//   .t3 DefaultValues are what matter): WhichTime(enum, .t3 default 1=LocalFxTime), Threshold(0),
//   Mode(enum, .t3 default 2=DidChange).
// Modes enum (HasTimeChanged.cs:107-113): DidRewind=0, DidAdvanced=1, DidChange=2, DidAdvancedWithMotionBlur=3.
// Times enum (HasTimeChanged.cs:115-121): LocalTime=0, LocalFxTime=1, GlobalTime=2, GlobalFxTime=3.
// Time: frame_cook hands wall fx seconds via `time` (the SINGLE-clock seam, == TiXL context.LocalFxTime
//   substitution every time-reading op here already uses: Ease/HasValueChanged/KeepBoolean). `dt` unused.
// Forks (named) — same precedent as Damp/Spring/Ease:
//   • SINGLE-CLOCK collapse: the seam exposes ONE clock to the step (`time` = wall fx seconds). TiXL's
//     four WhichTime targets (LocalTime/LocalFxTime/GlobalTime/GlobalFxTime) all resolve to that one
//     `time` here — there is no transport bar-clock / playhead-vs-wall split at the cook step. WhichTime
//     is KEPT as a port for parity (its .t3 default 1=LocalFxTime is the dominant, exactly-faithful
//     case), but it is INERT: every enum value reads `time`. This is the identical clock-substitution
//     fork Ease/HasValueChanged already carry, generalized to the WhichTime selector.
//   • __MotionBlurPass var-map ABSENT: Mode 3 (DidAdvancedWithMotionBlur) reads context.IntVariables
//     ["__MotionBlurPass"]; this runtime has NO context-var map, so the var is always ABSENT → TiXL's
//     own `else` branch runs verbatim: hasChanged = wasAdvanced, _lastTime always updates
//     (wasAdditionalMotionBlurPass stays false). Mode 3 thus faithfully degrades to DidAdvanced — this
//     IS TiXL's no-MB-pass behavior, not a divergence. (The .t3 default Mode=2 never enters this branch.)
//   • The `Playback.FrameCount == _lastFrameUpdate` double-eval early-return is DROPPED — frame_cook
//     cooks each node exactly once per frame (Damp/Spring/Ease precedent). No _lastFrameUpdate.
//   • Bool HasChanged dissolves to Float 0/1 (Cut 32). Mode arrives on a Float port (enum selector) →
//     std::lround, clamped to [0,3].
// Faithful first-frame: _lastTime=0, so with time=0 wasAdvanced = 0>0+thr = false → HasChanged=0,
//   DeltaTime=0 (matches TiXL's own first eval before time advances).
void stepHasTimeChanged(const std::map<std::string, float>& in, float /*dt*/, float time,
                        StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float threshold = getIn(in, "Threshold", 0.0f);
  int mode = (int)std::lround(getIn(in, "Mode", 2.0f));  // .t3 default Mode=DidChange(2)
  if (mode < 0) mode = 0; else if (mode > 3) mode = 3;   // TiXL (Modes) cast range
  // WhichTime is read for port parity but the seam has a single clock → `time` is used regardless.
  (void)getIn(in, "WhichTime", 1.0f);

  float& lastTime = st.s[0];
  const bool wasRewind = time < lastTime - threshold;    // TiXL: time < _lastTime - threshold
  const bool wasAdvanced = time > lastTime + threshold;  // TiXL: time > _lastTime + threshold
  out[1] = time - lastTime;                              // DeltaTime.Value = (float)(time - _lastTime)

  bool hasChanged = false;
  switch (mode) {
    case 0:  hasChanged = wasRewind; break;                 // DidRewind
    case 1:  hasChanged = wasAdvanced; break;               // DidAdvanced
    case 3:  hasChanged = wasAdvanced; break;               // DidAdvancedWithMotionBlur, var ABSENT → else branch
    default: hasChanged = wasAdvanced || wasRewind; break;  // DidChange (2)
  }
  // var ABSENT ⇒ wasAdditionalMotionBlurPass is always false ⇒ _lastTime always updates (TiXL line 100-101).
  lastTime = time;
  out[0] = hasChanged ? 1.0f : 0.0f;
}

// Step-fn signature: (in, dt, time, st, out, tr). `tr` = the read-only transport snapshot (see
// TransportSnapshot). Widened in the playback-transport seam batch so transport-reading ops
// (StopWatch) can sample the run clock / bpm / playback speed without piping them through the
// 16-byte GPU EvaluationContext. Existing ops ignore `tr` (unnamed param) — additive, no behavior
// change to Damp/Spring/Ease/etc.
// --- StopWatch (TiXL Lib/numbers/anim/time/StopWatch.cs) — a run-clock stopwatch: Delta = elapsed
// time since the last ResetTrigger rising edge (in seconds or bars), LastDuration = the length of the
// segment captured at the last reset. The clock is TiXL's Playback.RunTimeInSecs — a PROCESS-LIFETIME
// wall-clock run timer (Stopwatch.StartNew() at static init, Playback.cs:159), INDEPENDENT of the
// playhead, pause, or rate. The seam hands it via TransportSnapshot::runTimeSecs. `dt`/`time` (the
// fxTime-seconds the other time ops use) are UNUSED — StopWatch must read the run clock, not fxTime.
// State: s[0]=_startTime, s[1]=_accumulatedDuration, s[2]=_lastUpdateTime, s[3]=_wasResetTrigger(0/1),
//   s[4]=lastDurationHeld (the LastDuration Slot value — persists across frames; see fork below).
//   All zero-init = TiXL field defaults (double _startTime/_accumulatedDuration/_lastUpdateTime = 0,
//   bool _wasResetTrigger = false, LastDuration.Value = 0). No _isFirstEval in TiXL — first cook with
//   _startTime=0 yields Delta = runTime (the run clock at first cook), exactly TiXL's own first eval.
//   `init` unused.
//   • LastDuration HOLD: TiXL's LastDuration is a persistent Slot written ONLY on a reset edge; here
//     frame_cook zeroes out[] each cook (it does NOT carry the prior Slot), so the held value is
//     reconstructed from s[4] and re-emitted every frame — mirroring TiXL keeping LastDuration.Value
//     untouched between resets (same zeroed-out[] reconstruct precedent as CountInt/Ease).
// Outputs (TiXL output decl order; both DirtyFlagTrigger.Animated): Delta(out[0]), LastDuration(out[1]).
// Ports/inputs (TiXL Input decl order): ResetTrigger(bool, .t3 false), DurationIn(enum TimeModes,
//   .t3 0=TimeInSecs), PauseWithPlayback(bool, .t3 false). Bool ports read >0.5 (Cut 32: no Bool type).
//   DurationIn is a compile-time Widget::Enum selector (Cut 71-72 precedent), NOT a runtime uniform.
// bars conversion: BeatTime → BarsFromSeconds(secs) = secs * bpm / 240 (transport.h:37, the authority).
//   The seam carries bpm in TransportSnapshot; the op multiplies inline (no transport.h call from
//   runtime-leaf — the constant /240 IS the authority, read from transport.h:37).
// FORKS (named):
//   • R-1 RUN-CLOCK ORIGIN: TiXL's RunTimeWatch starts at static init (app launch); simple_world has no
//     such static clock, so frame_cook seeds runTimeSecs from a process-lifetime wall accumulator
//     (Σ measureDeltaSeconds()) that starts at 0 on the first frame cook. The ABSOLUTE baseline thus
//     differs by the launch→first-cook interval — but StopWatch only ever EXPOSES deltas
//     (runTime−_startTime, _accumulatedDuration), never the absolute run time, and both _startTime and
//     runTime read the SAME clock, so Delta/LastDuration are baseline-INVARIANT and faithful. The clock
//     advances at real wall-clock rate regardless of pause/scrub/rate, matching the Stopwatch.
//   • R-2 PRECISION: TiXL keeps _startTime/_lastUpdateTime/_accumulatedDuration as doubles; the seam's
//     s[] is float. The run clock grows UNBOUNDED, so the absolute float _startTime/_lastUpdateTime lose
//     resolution over a multi-hour session (float epsilon ≈ 1ms at ~10000s run time) — same precision
//     class as the float `lastHitTime` HasValueChanged/PeakLevel already carry (those store wall `time`
//     in float too). Deltas-since-reset stay small and precise; only the absolute baseline coarsens.
//     Named, accepted: a multi-hour absolute-run-time StopWatch jitters Delta at the ~ms level — not a
//     behavior change at any practical session length, and identical to the established time-op forks.
//   • The DirtyFlag.Clear() bookkeeping (StopWatch.cs:45) is a TiXL dirty-flag detail with no output
//     effect → dropped (no analog in the resident extOut model).
void stepStopWatch(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                   StatefulValueState& st, float out[3], const TransportSnapshot& tr, ContextVarMap*, const std::string&) {
  const bool resetTrigger = getIn(in, "ResetTrigger", 0.0f) > 0.5f;
  const bool pauseWithPlayback = getIn(in, "PauseWithPlayback", 0.0f) > 0.5f;
  const int mode = (int)std::lround(getIn(in, "DurationIn", 0.0f));  // .t3 0=TimeInSecs

  const float runTime = (float)tr.runTimeSecs;  // = Playback.RunTimeInSecs (run-clock, R-1 fork)

  float& startTime = st.s[0];
  float& accumulated = st.s[1];
  float& lastUpdate = st.s[2];
  float& lastDurationHeld = st.s[4];

  // MathUtils.WasTriggered(ResetTrigger, ref _wasResetTrigger): rising edge, then store current.
  const bool prevReset = st.s[3] > 0.5f;
  const bool resetHit = resetTrigger && !prevReset;
  st.s[3] = resetTrigger ? 1.0f : 0.0f;

  if (resetHit) {
    lastDurationHeld = runTime - startTime;  // LastDuration = elapsed segment at the reset edge
    startTime = runTime;
    accumulated = 0.0f;
  }

  // PlaybackSpeed != 0 → the accumulated (pause-aware) clock advances by the run-clock delta.
  if (tr.rate != 0.0) accumulated += runTime - lastUpdate;
  lastUpdate = runTime;

  const float timeInSecs = pauseWithPlayback ? accumulated : (runTime - startTime);

  // ConvertTime: TimeInSecs(0) → secs; BeatTime(1)/else → BarsFromSeconds(secs) = secs*bpm/240.
  out[0] = (mode == 0) ? timeInSecs : (float)(timeInSecs * tr.bpm / 240.0);
  out[1] = lastDurationHeld;  // LastDuration Slot held across frames (zeroed-out[] reconstruct)
}

// ==================== transport-YELLOW consumers (Cut86 補縫 — read-only) ====================
// Three more transport-fed ops on the SAME seam StopWatch opened. They consume tr.{bpm,run clock,
// playhead/wall bars} host-side; they NEVER touch the 16-byte GPU EvaluationContext. ConvertTime &
// RunTime are STATELESS (0 floats of st); DelayTriggerChange is a 6-float change-detector.

// --- ConvertTime (TiXL Lib/numbers/anim/time/ConvertTime.cs) — bpm bars<->secs converter.
//   Result = Mode switch { BarsToSeconds => SecondsFromBars(time), SecondsToBars => BarsFromSeconds(time) }.
//   TiXL Playback.SecondsFromBars(b)=b*240/bpm ; BarsFromSeconds(s)=s*bpm/240 (transport.h:37-38). The
//   seam carries bpm in tr.bpm → multiply inline (no transport.h call from runtime leaf, StopWatch
//   precedent). Reads the LIVE bpm so bpm=240 halves a BarsToSeconds vs bpm=120 (golden proves it).
//   .t3 defaults: Mode=0 (BarsToSeconds), Time=0. The TiXL null-Playback IStatusProvider warning is
//   DROPPED — simple_world has no status system and the seam always supplies a Transport (tr.bpm>0).
//   0 state (stateless), but lives in the stateful table because its value depends on the per-frame
//   transport snapshot the pure evaluate()/`in`-map cannot carry (same reason as StopWatch).
void stepConvertTime(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                     StatefulValueState&, float out[3], const TransportSnapshot& tr, ContextVarMap*,
                     const std::string&) {
  const float time = getIn(in, "Time", 0.0f);
  const int mode = (int)std::lround(getIn(in, "Mode", 0.0f));  // .t3 0=BarsToSeconds
  out[0] = (mode == 0) ? (float)(time * 240.0 / tr.bpm)   // BarsToSeconds = SecondsFromBars(time)
                       : (float)(time * tr.bpm / 240.0);   // SecondsToBars = BarsFromSeconds(time)
}

// --- RunTime (TiXL Lib/numbers/anim/time/RunTime.cs) — TimeInSeconds = (float)Playback.RunTimeInSecs.
//   The seam carries that PROCESS-LIFETIME wall run clock in tr.runTimeSecs (a Stopwatch started at
//   static init, Playback.cs:159) — independent of playhead / scrub / pause / rate, so RunTime keeps
//   advancing while the playback is paused (unlike PlayTime). 0 state.
//   R-1 FORK (named, same as StopWatch): TiXL's RunTimeInSecs is a real OS Stopwatch; our run clock is
//   a wall-dt accumulator seeded from the first cook (frame_cook s_runTimeSecs). The ABSOLUTE origin
//   differs by the launch→first-cook interval, but RunTime is a pure exposure of that clock and the
//   golden drives the accumulator directly (dt=0.5 → 0.5/1.0/1.5) so parity is exact on the seam value.
void stepRunTime(const std::map<std::string, float>&, float /*dt*/, float /*time*/,
                 StatefulValueState&, float out[3], const TransportSnapshot& tr, ContextVarMap*,
                 const std::string&) {
  out[0] = (float)tr.runTimeSecs;  // = (float)Playback.RunTimeInSecs
}

// --- DelayTriggerChange (TiXL Lib/numbers/bool/process/DelayTriggerChange.cs:30-95, ported VERBATIM)
//   A TWO-EDGE change detector (hasBeenChanged = isTriggered != _triggered) — NOT a rising-edge
//   WasTriggered. On ANY edge it snapshots the change time + the PRIOR delayed output. The delayed
//   output holds `stateIfDelayed` until `remainingTime = refTime - currentTime + delayDuration` runs
//   out, then passes the raw trigger through. .t3 defaults: TimeMode=6 (AppRunTime_InSecs), Mode=0
//   (DelayTrue), DelayDuration=1.0, Trigger=false.
//   State (6 floats, mapping TiXL's private fields):
//     s[0]=_lastTrueTime  s[1]=_lastFalseTime  s[2]=_lastChangeTime  s[3]=_triggered(0/1 bool)
//     s[4]=_stateBeforeChange(0/1 bool)  s[5]=DelayedTrigger.Value held (the prior delayed output —
//     frame_cook hands a zeroed out[] each frame, so the op must remember its own last output to feed
//     `_stateBeforeChange = DelayedTrigger.Value` on the next edge; TiXL reads the live Slot).
//   7 TimeModes → snapshot (currentTime, host-side). SecondsFromBars(bars)=bars*240/bpm (transport.h):
//     0 LocalFxTime_InBars → tr.localFxTimeBars
//     1 LocalFxTime_InSecs → localFxTimeBars*240/bpm
//     2 LocalTime_InBars   → tr.localTimeBars
//     3 LocalTime_InSecs   → localTimeBars*240/bpm
//     4 PlayTime_InBars    → tr.playbackTimeBars
//     5 PlayTime_InSecs    → playbackTimeBars*240/bpm
//     6 AppRunTime_InSecs  → tr.runTimeSecs   (the .t3 default)
//   F-1 FORK (named): our snapshot sets playbackTimeBars = localTimeBars = t.position (frame_cook.cpp
//     :210-212), so LocalTime_* and PlayTime_* read the SAME playhead clock here. TiXL's
//     context.LocalTime vs context.Playback.TimeInBars can diverge under nested time-remap subgraphs we
//     don't yet model; on the flat graph both are the playhead, so this is exact for the common case.
//   FAITHFUL first-second (DelayTrue): s[*] init to 0, so before the first edge with AppRunTime,
//     remainingTime = 0 - currentTime + 1 > 0 while currentTime < 1 → DelayedTrigger holds
//     stateIfDelayed=true even though Trigger=false. This is TiXL's literal behavior (no s0 seeding) —
//     asserted by golden, NOT seeded away.
void stepDelayTriggerChange(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                            StatefulValueState& st, float out[3], const TransportSnapshot& tr,
                            ContextVarMap*, const std::string&) {
  const bool isTriggered = getIn(in, "Trigger", 0.0f) > 0.5f;
  const float delayDuration = getIn(in, "DelayDuration", 1.0f);
  const int delayMode = (int)std::lround(getIn(in, "Mode", 0.0f));      // 0=DelayTrue .t3 default
  const int timeMode = (int)std::lround(getIn(in, "TimeMode", 6.0f));   // 6=AppRunTime_InSecs .t3 default

  // private fields → st.s[]
  bool prevTriggered = st.s[3] > 0.5f;
  const bool hasBeenChanged = isTriggered != prevTriggered;
  st.s[3] = isTriggered ? 1.0f : 0.0f;  // _triggered = isTriggered

  // currentTime: the 7-mode switch over the snapshot (bars→secs = bars*240/bpm inline).
  double currentTime;
  switch (timeMode) {
    case 0: currentTime = tr.localFxTimeBars; break;                       // LocalFxTime_InBars
    case 1: currentTime = tr.localFxTimeBars * 240.0 / tr.bpm; break;      // LocalFxTime_InSecs
    case 2: currentTime = tr.localTimeBars; break;                         // LocalTime_InBars
    case 3: currentTime = tr.localTimeBars * 240.0 / tr.bpm; break;        // LocalTime_InSecs
    case 4: currentTime = tr.playbackTimeBars; break;                      // PlayTime_InBars
    case 5: currentTime = tr.playbackTimeBars * 240.0 / tr.bpm; break;     // PlayTime_InSecs
    case 6: currentTime = tr.runTimeSecs; break;                           // AppRunTime_InSecs
    default: currentTime = 0.0; break;
  }

  if (isTriggered) st.s[0] = (float)currentTime;  // _lastTrueTime
  else             st.s[1] = (float)currentTime;  // _lastFalseTime

  if (hasBeenChanged) {
    st.s[2] = (float)currentTime;  // _lastChangeTime
    st.s[4] = st.s[5];             // _stateBeforeChange = DelayedTrigger.Value (prior delayed output)
  }

  double refTime = 0.0;
  bool stateIfDelayed = false;
  switch (delayMode) {
    case 0: refTime = st.s[0]; stateIfDelayed = true; break;            // DelayTrue  → _lastTrueTime
    case 1: refTime = st.s[1]; stateIfDelayed = false; break;           // DelayFalse → _lastFalseTime
    case 2: refTime = st.s[2]; stateIfDelayed = st.s[4] > 0.5f; break;  // DelayBoth  → _lastChangeTime / _stateBeforeChange
    default: break;
  }

  const double remainingTime = refTime - currentTime + delayDuration;
  const bool isDelayed = remainingTime > 0.0;

  out[1] = (float)remainingTime;                                  // RemainingTime
  const bool delayed = isDelayed ? stateIfDelayed : (st.s[3] > 0.5f);  // _triggered passthrough
  out[0] = delayed ? 1.0f : 0.0f;                                 // DelayedTrigger (bool→float)
  st.s[5] = out[0];  // remember this frame's DelayedTrigger.Value for next edge's _stateBeforeChange
}

// ============================ context-var YELLOW seam ops ============================
// These are "stateful" in the cook sense (evaluate==nullptr, cooked once per frame into extOut) but
// their cross-frame channel is the SHARED ContextVarMap, not StatefulValueState. They consume the
// resolved String VariableName param (varName, from ResidentNode::strInputs) + the per-frame `vars`
// map. The 2-pass ordering (cookStatefulValueNodes) guarantees all Set*Var run before any Get*Var.

// --- SetFloatVar (TiXL Lib/flow/context/SetFloatVar.cs) — writes vars.floatVars[name]=FloatValue.
// We implement ONLY the no-SubGraph branch (SetFloatVar.cs:42-45): there is no Command sub-tree in
// the value rail (NAMED FORK — see header). Empty name → no-op (cs:20-24, string.IsNullOrEmpty).
// The TiXL output is a Command passthrough (no value-rail analog) → out[0] ECHOES the written value
// (golden anchor; the real product is the map mutation). .t3 defaults: FloatValue=0, VariableName="f".
void stepSetFloatVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                     float out[3], const TransportSnapshot&, ContextVarMap* vars,
                     const std::string& varName) {
  const float newValue = getIn(in, "FloatValue", 0.0f);
  out[0] = newValue;                          // echo (Command has no value; this is the golden probe)
  if (varName.empty() || !vars) return;       // string.IsNullOrEmpty(name) → no-op
  vars->floatVars[varName] = newValue;        // no-SubGraph branch: context.FloatVariables[name]=v
}

// --- GetFloatVar (TiXL Lib/flow/context/GetFloatVar.cs:14-28) — reads vars.floatVars[name], else
// FallbackDefault. DROP ICustomDropdownHolder (editor UI). .t3 defaults: VariableName="f", Fallback=0.
void stepGetFloatVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                     float out[3], const TransportSnapshot&, ContextVarMap* vars,
                     const std::string& varName) {
  const float fallback = getIn(in, "FallbackDefault", 0.0f);
  if (vars) {
    auto it = vars->floatVars.find(varName);
    if (it != vars->floatVars.end()) { out[0] = it->second; return; }  // TryGetValue hit
  }
  out[0] = fallback;                          // unset → FallbackDefault.GetValue(context)
}

// --- SetIntVar (TiXL Lib/flow/context/SetIntVar.cs) — writes vars.intVars[name]=(int)Value (no-
// SubGraph branch cs:61-64). Value arrives on a Float port (no Int port type) → C# (int) cast =
// TRUNCATION toward zero ((long)std::trunc; 7.9→7), NOT rounding (CountInt convention :709). Empty
// name → no-op (cs:30-36). out[0] echoes the stored int (golden). .t3 defaults: Value=0, Name="i".
void stepSetIntVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                   float out[3], const TransportSnapshot&, ContextVarMap* vars,
                   const std::string& varName) {
  const long newValue = (long)std::trunc(getIn(in, "Value", 0.0f));  // C# (int) cast = truncate
  out[0] = (float)newValue;
  if (varName.empty() || !vars) return;
  vars->intVars[varName] = newValue;
}

// --- GetIntVar (TiXL Lib/flow/context/GetIntVar.cs:16-50) — reads vars.intVars[name], else
// FallbackValue. FallbackValue arrives on a Float port carrying an int (TiXL Slot<int>) → truncate
// toward zero. Result is int-valued. DROP LogLevels enum + ICustomDropdownHolder. NAMED FORK: TiXL
// returns early (no write) when variableName==null; our name is always a resolved string (never
// null) — an empty name is a normal lookup miss → fallback (cs:30-39). .t3 default: VariableName="i".
void stepGetIntVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                   float out[3], const TransportSnapshot&, ContextVarMap* vars,
                   const std::string& varName) {
  const long fallback = (long)std::trunc(getIn(in, "FallbackValue", 0.0f));
  if (vars) {
    auto it = vars->intVars.find(varName);
    if (it != vars->intVars.end()) { out[0] = (float)it->second; return; }  // TryGetValue hit
  }
  out[0] = (float)fallback;                   // unset → fallbackValue
}

// ============================ Anim* family — AnimValue (the canonical Result+WasHit op) ============
// --- AnimValue (TiXL Lib/numbers/anim/animators/AnimValue.cs) — the foundation of the whole Anim*
// family: an oscillator/shaper whose Result is a PURE function of inputs+context-time (delegated to
// the AnimMath shape engine, runtime/anim_math.h), and whose WasHit is the ONLY consumer of
// cross-frame state — a tooth that fires once when the integer part of normalizedTime advances.
// TiXL Update (AnimValue.cs:25-53) ported faithfully:
//   _normalizedTime = time * rateFactorFromContext * rate + phase;
//   Result          = CalcValueForNormalizedTime(shape, _normalizedTime, 0, bias, ratio)*amplitude + offset;
//   WasHit          = (int)originalTime != (int)_normalizedTime;   // originalTime = PRIOR _normalizedTime
//
// Outputs (TiXL output decl order, both DirtyFlagTrigger.Animated): Result(out[0]), WasHit(out[1]).
//   WasHit is Bool → Float 0/1 (Cut 32: no Bool port type; same dissolve as HasValueChanged.HasChanged).
// Inputs (TiXL Input decl order): OverrideTime, Shape(enum), Rate, Ratio, Phase, Amplitude, Offset,
//   Bias, AllowSpeedFactor(enum). .t3 defaults (AnimValue.t3, RE-READ & confirmed): Rate=1, Shape=1
//   (Ramps — the .t3 selector value, NOT the C# field default Endless=0), Phase=0, Amplitude=1,
//   Ratio=1, Offset=0, Bias=0.5, AllowSpeedFactor=1 (FactorA), OverrideTime=0.
//
// State (the cross-frame tooth): s[0] = _normalizedTime of the PRIOR cook. init=false on the very
//   first cook (TiXL _normalizedTime field-inits to 0, so originalTime=0 on frame 1 — faithful: we
//   read s[0] which is zero-initialized, so no `init` seeding needed; s[0] starts 0 exactly like
//   TiXL's _normalizedTime field). Only WasHit reads this; Result is pure (no state).
//
// FORKS (named) — same family precedent as Ease/HasValueChanged/HasTimeChanged:
//   • SINGLE-CLOCK time source. TiXL: `time = OverrideTime.HasInputConnections ? OverrideTime :
//     context.LocalFxTime`. The cook seam hands ONE clock via `time` (= wall fx seconds, the
//     single-clock substitution for context.LocalFxTime the whole time-op family already uses), and
//     resolveResidentFloatInputs gives the step fn only the RESOLVED Float map — it cannot see
//     `HasInputConnections` (a connected-input-feeding-0 is indistinguishable from the .t3 default 0).
//     So OverrideTime is honored when NONZERO and falls back to the seam `time` when 0. This is exact
//     for the two dominant cases (OverrideTime unconnected → default 0 → seam time; OverrideTime
//     driven nonzero → that value) and diverges ONLY in the narrow "OverrideTime connected and
//     feeding exactly 0.0" case, which reads seam time instead. NAMED, accepted: the seam has no
//     connection-presence channel, so this is the minimal faithful substitute (本質 seam constraint,
//     not a math change).
//   • The `Math.Abs(LocalFxTime - _lastUpdateTime) > double.Epsilon` WasHit double-eval guard is
//     DROPPED — frame_cook cooks each node exactly once per frame (Damp/Spring/Ease/CountInt
//     precedent), so the same-frame double-update that guard prevents cannot occur. WasHit is
//     therefore recomputed every cook from (originalTime, _normalizedTime), which is what TiXL does
//     once per real frame. No _lastUpdateTime stored.
//   • SpeedFactor context-var read. TiXL AnimMath.GetSpeedOverrideFromContext reads
//     context.FloatVariables["SpeedFactorA"/"SpeedFactorB"] (default 1 when absent) per the
//     AllowSpeedFactor enum (None=0/FactorA=1/FactorB=2). We read the SAME host-side ContextVarMap
//     the Set*FloatVar seam populates (`vars`), so a SetFloatVar("SpeedFactorA",k) upstream scales the
//     rate exactly as TiXL — wired through the existing context-var YELLOW seam, no new channel. When
//     `vars` is null (the many selftest callers that don't pass it) or the key is unset → factor 1.0,
//     TiXL's own TryGetValue-miss default.
// AnimValue TEETH hook (file-local; 0 = production, set by the --selftest-animvalue golden ONLY via
// setAnimValueBug). It corrupts a REAL production term so the golden's FIXED expected values bite:
//   1 = DROP the state write (st.s[0] never advances) → originalTime stays 0 → the cross-frame WasHit
//       tooth never fires after frame 1 (and Result is unaffected — Result is pure, so this bites
//       ONLY the state-dependent output, proving the state write is load-bearing).
//   2 = DROP the AnimMath call (Result forced to the raw normalizedTime, no shape/bias/amp/offset) →
//       the Result golden bites while WasHit (state) stays correct.
// Defaults 0 so the production cook (cookStatefulValueNodes) and every other caller are unchanged.
// The expected values in the golden are computed from the TiXL formula and are INDEPENDENT of this
// flag (no co-conditioning) — the flag breaks the live computation, the wants stay put.
int g_animValueBug = 0;

void stepAnimValue(const std::map<std::string, float>& in, float /*dt*/, float time,
                   StatefulValueState& st, float out[3], const TransportSnapshot&,
                   ContextVarMap* vars, const std::string&) {
  const float rate = getIn(in, "Rate", 1.0f);
  const float ratio = getIn(in, "Ratio", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);
  const float amplitude = getIn(in, "Amplitude", 1.0f);
  const float offset = getIn(in, "Offset", 0.0f);
  const float bias = getIn(in, "Bias", 0.5f);

  // Shape enum (.t3 default 1=Ramps); clamp to [0, count-1] like TiXL Shape.GetValue.Clamp.
  int shapeIdx = (int)std::lround(getIn(in, "Shape", 1.0f));
  if (shapeIdx < 0) shapeIdx = 0;
  else if (shapeIdx > anim_math::kShapeCount - 1) shapeIdx = anim_math::kShapeCount - 1;
  const anim_math::Shapes shape = (anim_math::Shapes)shapeIdx;

  // rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(AllowSpeedFactor):
  //   None(0) → 1 ; FactorA(1) → FloatVariables["SpeedFactorA"] (default 1) ; FactorB(2) → "SpeedFactorB".
  int speedSel = (int)std::lround(getIn(in, "AllowSpeedFactor", 1.0f));
  if (speedSel < 0) speedSel = 0;
  else if (speedSel > 2) speedSel = 2;  // Clamp(0, len-1)
  float rateFactorFromContext = 1.0f;
  if (vars && (speedSel == 1 || speedSel == 2)) {
    const char* key = (speedSel == 1) ? "SpeedFactorA" : "SpeedFactorB";
    auto it = vars->floatVars.find(key);
    if (it != vars->floatVars.end()) rateFactorFromContext = it->second;  // TryGetValue hit; miss → 1
  }

  // SINGLE-CLOCK time source (named fork above): OverrideTime when nonzero, else the seam clock.
  const float overrideTime = getIn(in, "OverrideTime", 0.0f);
  const double t = (overrideTime != 0.0f) ? (double)overrideTime : (double)time;

  const double originalTime = (double)st.s[0];  // prior frame's _normalizedTime (zero-init on frame 1)
  const double normalizedTime = t * (double)rateFactorFromContext * (double)rate + (double)phase;
  if (g_animValueBug != 1) st.s[0] = (float)normalizedTime;  // bug 1: DROP the state write (real defect)

  out[0] = (g_animValueBug == 2)  // bug 2: DROP the AnimMath call (real defect — raw nt, no shaping)
               ? (float)normalizedTime
               : anim_math::calcValueForNormalizedTime(shape, normalizedTime, 0, bias, ratio) * amplitude + offset;
  out[1] = ((int)originalTime != (int)normalizedTime) ? 1.0f : 0.0f;  // WasHit (Bool→Float)
}

// ============================ Anim* family — AnimInt (integer Result + WasHit) ====================
// --- AnimInt (TiXL Lib/numbers/anim/animators/AnimInt.cs) — the integer sibling of AnimValue: same
// normalizedTime clock, but Result is the INTEGER part of normalizedTime (optionally wrapped with a
// POSITIVE modulo) rather than an AnimMath-shaped float. WasHit is the SAME cross-frame integer tooth.
// TiXL Update (AnimInt.cs:30-49) ported faithfully:
//   _normalizedTime = time * rateFactorFromContext * rate + phase;
//   result          = (int)_normalizedTime;
//   Result          = modulo != 0 ? result.Mod(modulo) : result;   // .Mod = POSITIVE modulo
//   WasHit          = (int)originalTime != (int)_normalizedTime;    // originalTime = PRIOR _normalizedTime
//
// `.Mod` is MathUtils.cs:273 — the POSITIVE (floored) integer modulo: `x = val % repeat; if (x<0)
//   x = repeat + x; return x;` and `repeat==0 → 0` (but the modulo!=0 guard means we never call it
//   with 0). Result is NOT C `%` (which keeps the sign of the dividend) — for negative time it wraps
//   into [0, modulo). Reproduced 1:1 below.
//
// Outputs (TiXL output decl order, both DirtyFlagTrigger.Animated): Result(out[0]), WasHit(out[1]).
//   Result is int → Float (carried as a Float port; the int math is exact for the anim-time range).
//   WasHit is Bool → Float 0/1 (Cut 32 — no Bool port type; same dissolve as AnimValue.WasHit).
// Inputs (TiXL Input decl order): Modulo(int), OverrideTime(float), Rate(float), Phase(float),
//   AllowSpeedFactor(int enum). .t3 defaults (AnimInt.t3, RE-READ & confirmed): Modulo=0, Rate=1.0,
//   Phase=0.0, AllowSpeedFactor=1 (FactorA), OverrideTime=0.0.
//
// State (the cross-frame tooth): s[0] = _normalizedTime of the PRIOR cook (zero-init on frame 1,
//   exactly like TiXL's _normalizedTime field). Only WasHit reads it; Result is pure.
//
// FORKS (named) — IDENTICAL family forks to AnimValue (same seam, same justification):
//   • SINGLE-CLOCK time source. TiXL: `time = OverrideTime.HasInputConnections ? OverrideTime :
//     context.LocalFxTime`. The seam hands ONE resolved clock; the step fn can't see
//     HasInputConnections, so OverrideTime is honored when NONZERO and falls back to the seam `time`
//     when 0. Exact for the two dominant cases; diverges only in the "OverrideTime connected feeding
//     exactly 0.0" case (本質 seam constraint, not a math change).
//   • The `_lastUpdateFrame == Playback.FrameCount` frame-dedup guard is DROPPED — frame_cook cooks
//     each node exactly ONCE per frame (Damp/Ease/AnimValue precedent), so the same-frame double-cook
//     that guard prevents cannot occur. WasHit is recomputed every cook from (originalTime, nt), which
//     is what TiXL does once per real frame. No _lastUpdateFrame stored.
//   • SpeedFactor context-var read. Same host-side ContextVarMap channel as AnimValue
//     (FloatVariables["SpeedFactorA"/"SpeedFactorB"], default 1 on miss) per the AllowSpeedFactor enum.
// AnimInt TEETH hook (file-local; 0 = production, set ONLY by the --selftest-animint golden via
// setAnimIntBug). It corrupts a REAL production term so the golden's FIXED expected values bite:
//   1 = DROP the state write (st.s[0] never advances) → originalTime stays 0 → the cross-frame WasHit
//       tooth breaks (Result is pure, so this bites ONLY the state output — proving the write is
//       load-bearing).
//   2 = DROP the Modulo wrap (Result forced to the raw (int)nt, ignoring modulo) → the modulo Result
//       golden bites while WasHit (state) stays correct.
// The expected values in the golden are computed from the TiXL formula and are INDEPENDENT of this
// flag (no co-conditioning).
int g_animIntBug = 0;

// Positive (floored) integer modulo — MathUtils.Mod (MathUtils.cs:273) ported VERBATIM. repeat==0
// returns 0 (guarded by the modulo!=0 caller, but kept faithful). Differs from C `%` for negatives.
static int posMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

void stepAnimInt(const std::map<std::string, float>& in, float /*dt*/, float time,
                 StatefulValueState& st, float out[3], const TransportSnapshot&,
                 ContextVarMap* vars, const std::string&) {
  const float rate = getIn(in, "Rate", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);
  const int modulo = (int)std::lround(getIn(in, "Modulo", 0.0f));

  // rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(AllowSpeedFactor):
  //   None(0) → 1 ; FactorA(1) → FloatVariables["SpeedFactorA"] (default 1) ; FactorB(2) → "SpeedFactorB".
  int speedSel = (int)std::lround(getIn(in, "AllowSpeedFactor", 1.0f));
  if (speedSel < 0) speedSel = 0;
  else if (speedSel > 2) speedSel = 2;  // Clamp(0, len-1)
  float rateFactorFromContext = 1.0f;
  if (vars && (speedSel == 1 || speedSel == 2)) {
    const char* key = (speedSel == 1) ? "SpeedFactorA" : "SpeedFactorB";
    auto it = vars->floatVars.find(key);
    if (it != vars->floatVars.end()) rateFactorFromContext = it->second;  // TryGetValue hit; miss → 1
  }

  // SINGLE-CLOCK time source (named fork above): OverrideTime when nonzero, else the seam clock.
  const float overrideTime = getIn(in, "OverrideTime", 0.0f);
  const double t = (overrideTime != 0.0f) ? (double)overrideTime : (double)time;

  const double originalTime = (double)st.s[0];  // prior frame's _normalizedTime (zero-init on frame 1)
  const double normalizedTime = t * (double)rateFactorFromContext * (double)rate + (double)phase;
  if (g_animIntBug != 1) st.s[0] = (float)normalizedTime;  // bug 1: DROP the state write (real defect)

  const int result = (int)normalizedTime;
  // bug 2: DROP the modulo wrap (real defect — raw (int)nt, ignoring the positive modulo).
  out[0] = (g_animIntBug == 2) ? (float)result
                               : (float)((modulo != 0) ? posMod(result, modulo) : result);
  out[1] = ((int)originalTime != (int)normalizedTime) ? 1.0f : 0.0f;  // WasHit (Bool→Float)
}

// ============================ Anim* family — AnimBoolean (the pure TriggerOutput edge) ============
// --- AnimBoolean (TiXL Lib/numbers/anim/animators/AnimBoolean.cs) — the INVERSE of AnimValue/AnimInt:
// it has NO Result output at all; its ONLY output, TriggerOutput, IS the WasHit cross-frame edge.
// TiXL Update (AnimBoolean.cs:23-37) ported faithfully:
//   time            = context.LocalFxTime;                         // NO OverrideTime input here
//   _normalizedTime = time * rateFactorFromContext * rate + phase;
//   TriggerOutput   = (int)originalTime != (int)_normalizedTime;   // originalTime = PRIOR _normalizedTime
//
// Output (single, DirtyFlagTrigger.Animated): TriggerOutput(out[0]) = the integer tooth (Bool→Float
//   0/1, Cut 32). There is NO pure Result — this op exists solely to fire when (int)time advances.
// Inputs (TiXL Input decl order): Rate(float), Phase(float), AllowSpeedFactor(int enum). .t3 defaults
//   (AnimBoolean.t3, RE-READ & confirmed): Rate=1.0, ★AllowSpeedFactor=0 (None — DIFFERENT from
//   AnimValue/AnimInt which default to 1/FactorA), Phase=0.0. NO Modulo, NO OverrideTime, NO Ratio.
//
// State: s[0] = _normalizedTime of the PRIOR cook (zero-init on frame 1, like TiXL's field).
//
// FORKS (named):
//   • NO OverrideTime input → NO single-clock fork needed: time is ALWAYS the seam clock (the
//     single-clock substitution for context.LocalFxTime the whole time-op family uses). This is the
//     ONE Anim* op that is exact on the clock (TiXL itself reads only context.LocalFxTime).
//   • The `_lastUpdateFrame == Playback.FrameCount` frame-dedup guard is DROPPED (once-per-frame cook,
//     same as AnimValue/AnimInt).
//   • SpeedFactor context-var read — same channel as AnimValue/AnimInt.
// AnimBoolean TEETH hook (file-local; 0 = production, set ONLY by --selftest-animboolean via
// setAnimBooleanBug):
//   1 = DROP the state write (st.s[0] never advances) → originalTime frozen at 0 → the TriggerOutput
//       tooth breaks (fires/doesn't-fire wrong after frame 1). This op has ONLY the state output, so
//       bug 1 is the natural corruption — it proves the cross-frame state write is load-bearing.
//   2 = FREEZE the edge to 0 (TriggerOutput forced low, ignoring the comparison) → bites the frames
//       where the tooth SHOULD fire while leaving the no-fire frames correct (an independent defect
//       from bug 1, so the golden's want=1 frames bite even if state happens to look right).
// Expected values are computed from the TiXL formula and are INDEPENDENT of this flag.
int g_animBooleanBug = 0;

void stepAnimBoolean(const std::map<std::string, float>& in, float /*dt*/, float time,
                     StatefulValueState& st, float out[3], const TransportSnapshot&,
                     ContextVarMap* vars, const std::string&) {
  const float rate = getIn(in, "Rate", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);

  // rateFactorFromContext per AllowSpeedFactor (.t3 default 0=None → factor 1). Same channel as above.
  int speedSel = (int)std::lround(getIn(in, "AllowSpeedFactor", 0.0f));
  if (speedSel < 0) speedSel = 0;
  else if (speedSel > 2) speedSel = 2;  // Clamp(0, len-1)
  float rateFactorFromContext = 1.0f;
  if (vars && (speedSel == 1 || speedSel == 2)) {
    const char* key = (speedSel == 1) ? "SpeedFactorA" : "SpeedFactorB";
    auto it = vars->floatVars.find(key);
    if (it != vars->floatVars.end()) rateFactorFromContext = it->second;  // TryGetValue hit; miss → 1
  }

  // No OverrideTime — time is ALWAYS the seam clock (faithful: TiXL reads only context.LocalFxTime).
  const double t = (double)time;
  const double originalTime = (double)st.s[0];  // prior frame's _normalizedTime (zero-init on frame 1)
  const double normalizedTime = t * (double)rateFactorFromContext * (double)rate + (double)phase;
  if (g_animBooleanBug != 1) st.s[0] = (float)normalizedTime;  // bug 1: DROP the state write (real defect)

  // bug 2: FREEZE the edge to 0 (real defect — ignore the comparison entirely).
  out[0] = (g_animBooleanBug == 2) ? 0.0f
                                   : (((int)originalTime != (int)normalizedTime) ? 1.0f : 0.0f);  // TriggerOutput
}

struct StatefulOp {
  const char* type;
  // context-var seam: every step fn gains a trailing (ContextVarMap*, const std::string& varName).
  // The ~30 non-var ops ignore both (unnamed params, zero body diff — the TransportSnapshot
  // precedent); Set*/Get*Var read them. Uniform signature keeps ONE dispatch table.
  void (*step)(const std::map<std::string, float>&, float, float, StatefulValueState&, float[8],
               const TransportSnapshot&, ContextVarMap*, const std::string&);
};
// The data-driven table (rule 7): add a row to extend; frame_cook + the resident path stay untouched.
const StatefulOp kStatefulValueOps[] = {
    {"Damp", stepDamp},
    {"DampAngle", stepDampAngle},
    {"DampVec2", stepDampVec2},
    {"DampVec3", stepDampVec3},
    {"DeltaSinceLastFrame", stepDeltaSinceLastFrame},
    {"FreezeValue", stepFreezeValue},
    {"Spring", stepSpring},
    {"SpringVec2", stepSpringVec2},
    {"SpringVec3", stepSpringVec3},
    {"Ease", stepEase},
    {"EaseVec2", stepEaseVec2},
    {"EaseVec3", stepEaseVec3},
    {"HasValueIncreased", stepHasValueIncreased},
    {"HasValueDecreased", stepHasValueDecreased},
    {"HasValueChanged", stepHasValueChanged},
    {"DetectPulse", stepDetectPulse},
    {"Accumulator", stepAccumulator},
    {"HasVec2Changed", stepHasVec2Changed},
    {"HasVec3Changed", stepHasVec3Changed},
    {"PeakLevel", stepPeakLevel},
    {"CountInt", stepCountInt},
    {"FlipBool", stepFlipBool},
    {"HasIntChanged", stepHasIntChanged},
    {"ToggleBoolean", stepToggleBoolean},
    {"FlipFlop", stepFlipFlop},
    {"HasBooleanChanged", stepHasBooleanChanged},
    {"Trigger", stepTrigger},
    {"KeepBoolean", stepKeepBoolean},
    {"DampPeakDecay", stepDampPeakDecay},
    {"HasTimeChanged", stepHasTimeChanged},
    {"StopWatch", stepStopWatch},
    // transport-YELLOW consumers (Cut86 補縫): ConvertTime/RunTime stateless-of-transport, DelayTriggerChange 6-state.
    {"ConvertTime", stepConvertTime},
    {"RunTime", stepRunTime},
    {"DelayTriggerChange", stepDelayTriggerChange},
    // context-var YELLOW seam (block #1): writers + readers. Writers run in pass 1, readers pass 2.
    {"SetFloatVar", stepSetFloatVar},
    {"GetFloatVar", stepGetFloatVar},
    {"SetIntVar", stepSetIntVar},
    {"GetIntVar", stepGetIntVar},
    // Anim* family foundation: AnimValue (Result pure via anim_math, WasHit = cross-frame integer tooth).
    {"AnimValue", stepAnimValue},
    // AnimInt: integer Result (+ positive modulo wrap) + the same cross-frame WasHit tooth.
    {"AnimInt", stepAnimInt},
    // AnimBoolean: ONLY output is the TriggerOutput edge (= the WasHit cross-frame integer tooth).
    {"AnimBoolean", stepAnimBoolean},
};

const StatefulOp* findStatefulOp(const std::string& t) {
  for (const auto& o : kStatefulValueOps)
    if (t == o.type) return &o;
  return nullptr;
}

}  // namespace

bool isStatefulValueOp(const std::string& opType) { return findStatefulOp(opType) != nullptr; }

// context-var seam: the Set*Var writer family (run before any reader in the 2-pass cook). Kept as
// an explicit name list (4 ops) rather than a prefix match — explicit is refuter-auditable and a
// future "SetupX" op can't accidentally join the writer pass.
bool isContextVarWriter(const std::string& opType) {
  return opType == "SetFloatVar" || opType == "SetIntVar";
}

void cookStatefulValueOp(const std::string& opType, const std::map<std::string, float>& in,
                         float dt, float time, StatefulValueState& st, float out[8],
                         const TransportSnapshot& tr, ContextVarMap* vars,
                         const std::string& varName) {
  if (const StatefulOp* o = findStatefulOp(opType)) o->step(in, dt, time, st, out, tr, vars, varName);
}

// AnimValue teeth hook setter (the global lives in the anonymous namespace above; this gives the
// app-side --selftest-animvalue golden a handle to flip it around the REAL production cook).
void setAnimValueBug(int mode) { g_animValueBug = mode; }

// AnimInt / AnimBoolean teeth hook setters (globals in the anonymous namespace above; handles for the
// app-side --selftest-animint / --selftest-animboolean goldens to flip around the REAL prod cook).
void setAnimIntBug(int mode) { g_animIntBug = mode; }
void setAnimBooleanBug(int mode) { g_animBooleanBug = mode; }

// --- Isolated proof (frame-driven; hand-computed TiXL trajectories) ---
int runStatefulValueSelfTest(bool injectBug) {
  bool ok = true;
  const float eps = 1e-4f;
  const float dt60 = 1.0f / 60.0f;

  // ----- Damp, LinearInterpolation (Method 0), Damping=0.8 -----
  // First cook seeds dampedValue=Value (here 0). Then drive Value=1:
  //   d_{n+1} = Lerp(1, d_n, 0.8) = 1 + (d_n - 1)*0.8.
  //   d1 = 0.2, d2 = 0.36, d3 = 0.488.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("Damp", {{"Value", 0.0f}, {"Damping", 0.8f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
    bool pass0 = std::fabs(out[0] - 0.0f) < eps;  // init seeded to the input (0)
    ok = ok && pass0;
    printf("[selftest-statefulvalue] Damp.lin init=%.4f want=0.0000 -> %s\n", out[0], pass0 ? "PASS" : "FAIL");
    const float want[3] = {0.2f, 0.36f, 0.488f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("Damp", {{"Value", 1.0f}, {"Damping", 0.8f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
      // injectBug: a swapped-arg lerp (Lerp(current,target,d)) would give 0.8 on step 1.
      float w = (injectBug && i == 0) ? 0.8f : want[i];
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Damp.lin step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- Damp, DampedSpring (Method 1), Damping=0.5, dt fed HUGE (10s) -----
  // From rest one step: d1 = (k*ts)*ts with ts CLAMPED to 1/60 (proves the clamp — an unclamped
  // 10s step would explode). k = 0.5/(0.5+0.001) = 0.998004; d1 = 0.998004 * (1/60)^2 = 0.00027722.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("Damp", {{"Value", 0.0f}, {"Damping", 0.5f}, {"Method", 1.0f}}, 10.0f, 0.0f, st, out);  // init -> 0
    cookStatefulValueOp("Damp", {{"Value", 1.0f}, {"Damping", 0.5f}, {"Method", 1.0f}}, 10.0f, 0.0f, st, out);
    const float k = 0.5f / 0.501f;
    float want = injectBug ? 0.05f : (k * dt60 * dt60);  // ~0.00027722; injectBug = unclamped-ish wrong
    bool pass = std::fabs(out[0] - want) < eps;
    ok = ok && pass;
    printf("[selftest-statefulvalue] Damp.spring step1(dt=10 clamped)=%.6f want=%.6f -> %s\n", out[0], want, pass ? "PASS" : "FAIL");
  }

  // ----- Spring, Tension=0.5, Strength=1, Value=1 (overshoot then settle) -----
  //   springed = Lerp(springed, (1-result)*1, 0.5); result += springed.
  //   result: 0.5, 1.0, 1.25 (overshoot), 1.25, 1.125.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float want[5] = {0.5f, 1.0f, 1.25f, 1.25f, 1.125f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("Spring", {{"Value", 1.0f}, {"Tension", 0.5f}, {"Strength", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 1.0f : want[i];  // bug: no overshoot
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Spring step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- DampAngle, Linear (Method 0), Damping=0.5, Value=350° (shortest-path wrap) -----
  // No init seeding (damped=0). DeltaAngle(0,350) = -10 (short way), targetAngle=-10;
  //   d1 = Lerp(-10,0,0.5) = -5. Then DeltaAngle(-5,350)=-5, targetAngle=-10; d2 = Lerp(-10,-5,0.5)=-7.5.
  // A naive damp (no wrap) would chase +350 — so -5/-7.5 proves the angular re-target.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float want[2] = {-5.0f, -7.5f};
    for (int i = 0; i < 2; ++i) {
      cookStatefulValueOp("DampAngle", {{"Value", 350.0f}, {"Damping", 0.5f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 0) ? 175.0f : want[i];  // bug: no wrap → chases +350
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] DampAngle step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- DeltaSinceLastFrame: Value 5,8,8 → delta 5,3,0 (lastValue init 0) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float vals[3] = {5.0f, 8.0f, 8.0f};
    const float want[3] = {5.0f, 3.0f, 0.0f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("DeltaSinceLastFrame", {{"Value", vals[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 8.0f : want[i];  // bug: returns raw value, not the delta
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Delta step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- FreezeValue, Mode 0 (FreezeWhileTrue): track while Freeze=0, hold while Freeze=1 -----
  // frames (Value,Freeze): (2,0)(5,1)(9,1)(9,0) → Result 2,2,2,9 ; DeltaSinceFreeze 0,3,7,0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {2.0f, 5.0f, 9.0f, 9.0f};
    const float frz[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float wantR[4] = {2.0f, 2.0f, 2.0f, 9.0f};
    const float wantD[4] = {0.0f, 3.0f, 7.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("FreezeValue", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 0.0f}}, dt60, 0.0f, st, out);
      float wr = (injectBug && i == 2) ? 9.0f : wantR[i];  // bug: doesn't hold (tracks while frozen)
      bool pass = std::fabs(out[0] - wr) < eps && std::fabs(out[1] - wantD[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Freeze0 step%d R=%.4f(want %.4f) D=%.4f -> %s\n", i + 1, out[0], wr, out[1], pass ? "PASS" : "FAIL");
    }
  }

  // ----- FreezeValue, Mode 1 (UpdateWhenSwitchingToTrue): sample only on the Freeze rising edge -----
  // frames (Value,Freeze): (2,0)(5,1)(8,1) → Result 0,5,5 (samples 5 when Freeze goes 0→1).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[3] = {2.0f, 5.0f, 8.0f};
    const float frz[3] = {0.0f, 1.0f, 1.0f};
    const float wantR[3] = {0.0f, 5.0f, 5.0f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("FreezeValue", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 1.0f}}, dt60, 0.0f, st, out);
      bool pass = std::fabs(out[0] - wantR[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Freeze1 step%d R=%.4f want=%.4f -> %s\n", i + 1, out[0], wantR[i], pass ? "PASS" : "FAIL");
    }
  }

  // ----- DampVec3, Linear, Damping=0.5, Value=(1,2,4) — component-wise, no bleed, damps from 0 -----
  //   step1 = (0.5, 1.0, 2.0); step2 = (0.75, 1.5, 3.0).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float w1[3] = {0.5f, 1.0f, 2.0f};
    const float w2[3] = {0.75f, 1.5f, 3.0f};
    cookStatefulValueOp("DampVec3", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Value.z", 4.0f}, {"Damping", 0.5f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
    bool p1 = std::fabs(out[0] - w1[0]) < eps && std::fabs(out[1] - w1[1]) < eps && std::fabs(out[2] - (injectBug ? 9.9f : w1[2])) < eps;
    ok = ok && p1;
    printf("[selftest-statefulvalue] DampVec3 step1=(%.3f,%.3f,%.3f) -> %s\n", out[0], out[1], out[2], p1 ? "PASS" : "FAIL");
    cookStatefulValueOp("DampVec3", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Value.z", 4.0f}, {"Damping", 0.5f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
    bool p2 = std::fabs(out[0] - w2[0]) < eps && std::fabs(out[1] - w2[1]) < eps && std::fabs(out[2] - w2[2]) < eps;
    ok = ok && p2;
    printf("[selftest-statefulvalue] DampVec3 step2=(%.3f,%.3f,%.3f) -> %s\n", out[0], out[1], out[2], p2 ? "PASS" : "FAIL");
  }

  // ----- SpringVec2, Tension=0.5, Strength=1, Value=(1,2) — component-wise, no bleed -----
  //   step1 = (0.5, 1.0); step2 = (1.0, 2.0).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float w1[2] = {0.5f, 1.0f};
    const float w2[2] = {1.0f, 2.0f};
    cookStatefulValueOp("SpringVec2", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Tension", 0.5f}, {"Strength", 1.0f}}, dt60, 0.0f, st, out);
    bool p1 = std::fabs(out[0] - w1[0]) < eps && std::fabs(out[1] - w1[1]) < eps;
    ok = ok && p1;
    printf("[selftest-statefulvalue] SpringVec2 step1=(%.3f,%.3f) -> %s\n", out[0], out[1], p1 ? "PASS" : "FAIL");
    cookStatefulValueOp("SpringVec2", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Tension", 0.5f}, {"Strength", 1.0f}}, dt60, 0.0f, st, out);
    bool p2 = std::fabs(out[0] - w2[0]) < eps && std::fabs(out[1] - w2[1]) < eps;
    ok = ok && p2;
    printf("[selftest-statefulvalue] SpringVec2 step2=(%.3f,%.3f) -> %s\n", out[0], out[1], p2 ? "PASS" : "FAIL");
  }

  // ----- Ease, Linear (Interpolation 0, Direction In), Duration=1.0 -----
  // Cook1 time=0 input=0: seeds (no restart, dist=0) → 0. Cook2 time=0 input=1: dist=1>0.001 →
  // RESTART startTime=0, initial=0, target=1; progress=0 → 0. Then sample the SAME animation as
  // wall time advances: t=0.25/0.5/1.0 → progress=0.25/0.5/1.0 → Result=0.25/0.5/1.0 (linear).
  // Proves elapsed/duration progress + restart capture.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("Ease", {{"Value", val}, {"Duration", 1.0f}, {"Direction", 0.0f}, {"Interpolation", 0.0f}}, dt60, time, st, out);
    };
    cook(0.0f, 0.0f);  // seed prevInput=0
    cook(0.0f, 1.0f);  // restart at t=0
    bool p0 = std::fabs(out[0] - 0.0f) < eps;  // progress 0 at restart
    ok = ok && p0;
    printf("[selftest-statefulvalue] Ease.lin restart=%.4f want=0.0000 -> %s\n", out[0], p0 ? "PASS" : "FAIL");
    const float t[3] = {0.25f, 0.5f, 1.0f};
    const float want[3] = {0.25f, 0.5f, 1.0f};
    for (int i = 0; i < 3; ++i) {
      cook(t[i], 1.0f);
      float w = (injectBug && i == 0) ? 0.5f : want[i];  // bug: progress not driven by elapsed/duration
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Ease.lin t=%.2f=%.4f want=%.4f -> %s\n", t[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- Ease, Quad + Out (Interpolation 2, Direction 1), Duration=1.0, at progress=0.5 -----
  // Restart toward 1 at t=0, then sample t=0.5 → OutQuad(0.5)=0.5*(2-0.5)=0.75 → Result=0.75.
  // A linear curve would give 0.5 here — so 0.75 PROVES the EasingFunctions dispatch is wired.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("Ease", {{"Value", val}, {"Duration", 1.0f}, {"Direction", 1.0f}, {"Interpolation", 2.0f}}, dt60, time, st, out);
    };
    cook(0.0f, 0.0f);
    cook(0.0f, 1.0f);          // restart
    cook(0.5f, 1.0f);          // progress 0.5 → OutQuad(0.5)=0.75
    float w = injectBug ? 0.5f : 0.75f;  // bug: linear (ignores the easing curve)
    bool pass = std::fabs(out[0] - w) < eps;
    ok = ok && pass;
    printf("[selftest-statefulvalue] Ease.outQuad p=0.5=%.4f want=%.4f -> %s\n", out[0], w, pass ? "PASS" : "FAIL");
  }

  // ----- EaseVec3, Linear, Duration=1.0, input (1,2,4): per-channel, NO cross-channel bleed -----
  // Cook1 t=0 restart: initial=(0,0,0), target=(1,2,4), progress=0 → (0,0,0). Cook2 t=0.5: progress
  // 0.5 → (0.5,1.0,2.0). Each component eases on its OWN initial/target (proves the s[12] layout).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time) {
      cookStatefulValueOp("EaseVec3", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Value.z", 4.0f}, {"Duration", 1.0f}, {"Direction", 0.0f}, {"Interpolation", 0.0f}}, dt60, time, st, out);
    };
    cook(0.0f);  // restart, progress 0 → (0,0,0)
    cook(0.5f);  // progress 0.5 → (0.5,1.0,2.0)
    // injectBug: a channel-bleed bug (e.g. y using x's target) would break the .y expectation.
    float wy = injectBug ? 0.5f : 1.0f;
    bool pass = std::fabs(out[0] - 0.5f) < eps && std::fabs(out[1] - wy) < eps && std::fabs(out[2] - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-statefulvalue] EaseVec3 p=0.5=(%.3f,%.3f,%.3f) want=(0.500,%.3f,2.000) -> %s\n", out[0], out[1], out[2], wy, pass ? "PASS" : "FAIL");
  }

  // ----- HasValueIncreased, Threshold=0.5: Value 1,3,3.2,2 → flag 1,1,0,0 -----
  // lastValue inits 0. f1: 1>0+0.5 → 1. f2: 3>1+0.5 → 1. f3: 3.2>3+0.5? (3.5) no → 0. f4: 2>3.2+0.5 no → 0.
  // (f3 proves the Threshold band — a +0.2 rise under the 0.5 threshold does NOT trigger.)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {1.0f, 3.0f, 3.2f, 2.0f};
    const float want[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasValueIncreased", {{"Value", val[i]}, {"Threshold", 0.5f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 1.0f : want[i];  // bug: ignores Threshold → +0.2 falsely fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasIncreased step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- HasValueDecreased, Threshold=0.5: Value 5,2,1.9,3 → flag 1,1,0,0 -----
  // lastValue inits 0. f1: 5<0-0.5 no → 0. Wait: f1 5<-0.5 false → 0. So expect 0,1,0,0.
  // f1: 5 < 0-0.5? no → 0. f2: 2 < 5-0.5 (4.5)? yes → 1. f3: 1.9 < 2-0.5 (1.5)? no → 0. f4: 3 < 1.9-0.5 no → 0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {5.0f, 2.0f, 1.9f, 3.0f};
    const float want[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasValueDecreased", {{"Value", val[i]}, {"Threshold", 0.5f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 0.0f : want[i];  // bug: never fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasDecreased step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== HasValueChanged (TiXL float/logic/HasValueChanged.cs) =====
  // Helper to cook one HasValueChanged frame; checks HasChanged(out0), Delta(out1), DeltaOnHit(out2).
  // ----- (A) Mode=Increased, Threshold=0.5, time=0, MinTime=0, Prevent=0: cross/no-cross + Delta -----
  // lastValue inits 0. f1 V=1: 1>0+0.5 → HC=1, Delta=1, DeltaOnHit=|1-0|=1. f2 V=1.2: 1.2>1+0.5(1.5)?
  // no → HC=0, Delta=0.2. f3 V=2: 2>1.2+0.5(1.7)? yes → HC=1, Delta=0.8, DeltaOnHit=|2-1.2|=0.8.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float val) {
      cookStatefulValueOp("HasValueChanged",
                          {{"Value", val}, {"Threshold", 0.5f}, {"Mode", 1.0f},
                           {"MinTimeBetweenHits", 0.0f}, {"PreventContinuedChanges", 0.0f}},
                          dt60, 0.0f, st, out);
    };
    const float val[3] = {1.0f, 1.2f, 2.0f};
    const float wHC[3] = {1.0f, 0.0f, 1.0f};
    const float wDelta[3] = {1.0f, 0.2f, 0.8f};
    const float wHit[3] = {1.0f, 1.0f, 0.8f};
    for (int i = 0; i < 3; ++i) {
      cook(val[i]);
      float hc = (injectBug && i == 1) ? 1.0f : wHC[i];  // bug: ignores Threshold → +0.2 falsely fires
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - wDelta[i]) < eps &&
                  std::fabs(out[2] - wHit[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasChanged.inc step%d HC=%.1f(want %.1f) D=%.3f Hit=%.3f -> %s\n",
             i + 1, out[0], hc, out[1], out[2], pass ? "PASS" : "FAIL");
    }
  }

  // ----- (B) MinTimeBetweenHits gate, Mode=Changed, Threshold=0.5, MinTime=2.0, Prevent=0 -----
  // Two qualifying changes within < MinTime → the 2nd is suppressed (HC forced 0); after the gate
  // elapses it fires again. A false frame between rising edges re-arms wasHit so wasTriggered fires.
  // f1 t=10 V=3: absΔ3>0.5 T, edge(prev F)→T, gate|10-0|=10≥2 → HIT, lastHitTime=10, lastHitDelta=3, HC=1.
  // f2 t=10.5 V=3: absΔ0 → HC=0 (re-arm). f3 t=10.5 V=6: absΔ3 T, edge T, gate|10.5-10|=0.5≥2? no →
  //   HC FORCED 0 (suppressed), DeltaOnHit stays 3. f4 t=13 V=6: absΔ0 → HC=0 (re-arm).
  // f5 t=13 V=9: absΔ3 T, edge T, gate|13-10|=3≥2 → HIT, lastHitTime=13, lastHitDelta=3, HC=1.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("HasValueChanged",
                          {{"Value", val}, {"Threshold", 0.5f}, {"Mode", 0.0f},
                           {"MinTimeBetweenHits", 2.0f}, {"PreventContinuedChanges", 0.0f}},
                          dt60, time, st, out);
    };
    const float tm[5] = {10.0f, 10.5f, 10.5f, 13.0f, 13.0f};
    const float val[5] = {3.0f, 3.0f, 6.0f, 6.0f, 9.0f};
    const float wHC[5] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float wHit[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};  // DeltaOnHit holds last accepted absDelta
    for (int i = 0; i < 5; ++i) {
      cook(tm[i], val[i]);
      float hc = (injectBug && i == 2) ? 1.0f : wHC[i];  // bug: gate not enforced → 2nd hit fires early
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[2] - wHit[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasChanged.gate step%d HC=%.1f(want %.1f) Hit=%.3f -> %s\n",
             i + 1, out[0], hc, out[2], pass ? "PASS" : "FAIL");
    }
  }

  // ----- (C) WasTriggered edge + PreventContinuedChanges semantics (FAITHFUL TiXL, NOT the brief) -----
  // Sustained increase, Mode=Increased, Threshold=0, MinTime=2.0. The gate body runs only when
  // `hasChanged && (prevent || wasTriggered)`. So:
  //   Prevent=0: a CONTINUED change (wasTriggered=F) SKIPS the gate → HC passes through raw=1 each
  //     frame. Sequence HC = 1,1,1,1 (continued raw changes get through).
  //   Prevent=1: EVERY qualifying frame enters the gate → continued frames within MinTime are FORCED
  //     0 (this is what "prevent continued changes" means); only frames past MinTime fire.
  //     Sequence HC = 1,0,0,1. (NOTE: the brief described these two inverted — TiXL is authority;
  //     flagged in the dossier. _wasHit stores the raw hasChanged BEFORE the gate, per TiXL order.)
  {
    const float tm[4] = {10.0f, 10.5f, 11.0f, 13.0f};
    const float val[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    // Prevent=0 → all raw changes pass.
    {
      StatefulValueState st;
      float out[3] = {0, 0, 0};
      const float wHC[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      for (int i = 0; i < 4; ++i) {
        cookStatefulValueOp("HasValueChanged",
                            {{"Value", val[i]}, {"Threshold", 0.0f}, {"Mode", 1.0f},
                             {"MinTimeBetweenHits", 2.0f}, {"PreventContinuedChanges", 0.0f}},
                            dt60, tm[i], st, out);
        bool pass = std::fabs(out[0] - wHC[i]) < eps;
        ok = ok && pass;
        printf("[selftest-statefulvalue] HasChanged.prevent0 step%d HC=%.1f want=%.1f -> %s\n",
               i + 1, out[0], wHC[i], pass ? "PASS" : "FAIL");
      }
    }
    // Prevent=1 → continued changes within MinTime suppressed; fires only after gate elapses.
    {
      StatefulValueState st;
      float out[3] = {0, 0, 0};
      const float wHC[4] = {1.0f, 0.0f, 0.0f, 1.0f};
      for (int i = 0; i < 4; ++i) {
        cookStatefulValueOp("HasValueChanged",
                            {{"Value", val[i]}, {"Threshold", 0.0f}, {"Mode", 1.0f},
                             {"MinTimeBetweenHits", 2.0f}, {"PreventContinuedChanges", 1.0f}},
                            dt60, tm[i], st, out);
        float hc = (injectBug && i == 3) ? 0.0f : wHC[i];  // bug: gate never re-fires after suppression
        bool pass = std::fabs(out[0] - hc) < eps;
        ok = ok && pass;
        printf("[selftest-statefulvalue] HasChanged.prevent1 step%d HC=%.1f want=%.1f -> %s\n",
               i + 1, out[0], hc, pass ? "PASS" : "FAIL");
      }
    }
  }

  // ===== DetectPulse (TiXL float/process/DetectPulse.cs) =====
  // Damping=0.5, Threshold=0.5, MinTimeBetweenHits=2.0. dampedValue inits 0, lastHitTime=−∞, wasHit=F.
  // Hand-computed trajectory (delta=dampedPre−new; damped'=Lerp(new,dampedPre,0.5)=new+(dampedPre−new)*0.5):
  //   f1 t=10  V=10: delta=0−10=−10  (no exceed); damped→Lerp(10,0,.5)=5 ;            HC=0 Dbg=−10
  //   f2 t=10.5 V=0 : delta=5−0=5    exceed,edge(prevHit F)→T; gate huge≥2 → HIT;
  //                     damped→Lerp(0,5,.5)=2.5 ; lastHit=10.5 ;                        HC=1 Dbg=5
  //   f3 t=10.5 V=0 : delta=2.5      exceed but edge=F (wasHit T) → no fire (rising-edge only);
  //                     damped→Lerp(0,2.5,.5)=1.25 ;                                    HC=0 Dbg=2.5
  //   f4 t=11   V=5 : delta=1.25−5=−3.75 (no exceed, re-arm wasHit→F);
  //                     damped→Lerp(5,1.25,.5)=3.125 ;                                  HC=0 Dbg=−3.75
  //   f5 t=11.5 V=0 : delta=3.125    exceed,edge T; gate |11.5−10.5|=1<2 → SUPPRESSED;
  //                     damped→Lerp(0,3.125,.5)=1.5625 ;                                HC=0 Dbg=3.125
  //   f6 t=11.5 V=5 : delta=1.5625−5=−3.4375 (no exceed, re-arm);
  //                     damped→Lerp(5,1.5625,.5)=3.28125 ;                              HC=0 Dbg=−3.4375
  //   f7 t=13   V=0 : delta=3.28125  exceed,edge T; gate |13−10.5|=2.5≥2 → HIT;
  //                     damped→Lerp(0,3.28125,.5)=1.640625 ; lastHit=13 ;              HC=1 Dbg=3.28125
  // f3 proves rising-edge-only (sustained exceed fires once); f5 proves the MinTime gate; f7 proves it
  // re-fires after the window. DebugValue is asserted = pre-update damped − new on every frame.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("DetectPulse",
                          {{"Value", val}, {"Threshold", 0.5f}, {"Damping", 0.5f},
                           {"MinTimeBetweenHits", 2.0f}},
                          dt60, time, st, out);
    };
    const float tm[7]  = {10.0f, 10.5f, 10.5f, 11.0f,  11.5f, 11.5f,   13.0f};
    const float val[7] = {10.0f,  0.0f,  0.0f,  5.0f,   0.0f,  5.0f,    0.0f};
    const float wHC[7] = { 0.0f,  1.0f,  0.0f,  0.0f,   0.0f,  0.0f,    1.0f};
    const float wDbg[7]= {-10.0f, 5.0f,  2.5f, -3.75f,  3.125f,-3.4375f,3.28125f};
    for (int i = 0; i < 7; ++i) {
      cook(tm[i], val[i]);
      // injectBug on f5: corrupts the MinTime-gate expectation so the gate-suppression frame is
      // asserted to FIRE — the live (correct) HC=0 then mismatches and FAILS.
      float hc = (injectBug && i == 4) ? 1.0f : wHC[i];
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - wDbg[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] DetectPulse step%d HC=%.1f(want %.1f) Dbg=%.4f(want %.4f) -> %s\n",
             i + 1, out[0], hc, out[1], wDbg[i], pass ? "PASS" : "FAIL");
    }
  }

  // ----- Accumulator PerFrame, Increment=1, Running=1: v=0→ 1,2,3 -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Accumulate", 0.0f}, {"Running", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 5.0f : (float)(i + 1);  // bug: wrong running total
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Accum.perFrame step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- Accumulator Running=0 freezes at 0; then ResetTrigger reloads StartValue=10 then +1 → 11 -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Running", 0.0f}}, dt60, 0.0f, st, out);
    bool p0 = std::fabs(out[0] - 0.0f) < eps;  // not running → stays 0
    cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Running", 1.0f}, {"StartValue", 10.0f}, {"ResetTrigger", 1.0f}}, dt60, 0.0f, st, out);
    float wR = injectBug ? 1.0f : 11.0f;  // bug: reset ignored → 0+1
    bool pR = std::fabs(out[0] - wR) < eps;  // reset→10 then +1 (same frame) → 11
    ok = ok && p0 && pR;
    printf("[selftest-statefulvalue] Accum.freeze=%.1f(want 0) reset+inc=%.1f(want %.1f) -> %s\n", 0.0f, out[0], wR, (p0 && pR) ? "PASS" : "FAIL");
  }
  // ----- Accumulator Modulo=3, +1/frame: v 1,2,3,4 → fmod(.,3) = 1,2,0,1 -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float want[4] = {1.0f, 2.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Running", 1.0f}, {"Modulo", 3.0f}}, dt60, 0.0f, st, out);
      bool pass = std::fabs(out[0] - want[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Accum.mod3 step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], want[i], pass ? "PASS" : "FAIL");
    }
  }
  // ----- Accumulator PerSeconds, Increment=60, dt=1/60 → +1/frame: 1,2 (proves dt path) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    for (int i = 0; i < 2; ++i) {
      cookStatefulValueOp("Accumulator", {{"Increment", 60.0f}, {"Accumulate", 1.0f}, {"Running", 1.0f}}, dt60, 0.0f, st, out);
      float w = (float)(i + 1);
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Accum.perSec step%d=%.2f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- HasVec2Changed, Threshold=0.5: moves (0,0)→(1,0)→(1,0)→(1,1) → HasChanged 0,1,0,1 -----
  // Delta(signed) = newValue-lastValue: (0,0),(1,0),(0,0),(0,1). (Euclidean dist > 0.5 fires.)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float vx[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    const float vy[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float wHC[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const float wDy[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasVec2Changed", {{"Value.x", vx[i]}, {"Value.y", vy[i]}, {"Threshold", 0.5f}}, dt60, 0.0f, st, out);
      float whc = (injectBug && i == 1) ? 0.0f : wHC[i];  // bug: misses the move
      bool pass = std::fabs(out[0] - whc) < eps && std::fabs(out[2] - wDy[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasVec2Changed step%d HC=%.1f(want %.1f) Dy=%.1f -> %s\n", i + 1, out[0], whc, out[2], pass ? "PASS" : "FAIL");
    }
  }

  // ----- HasVec3Changed (7-output, proves >3-out seam), Threshold=0.5, Mode=Changed -----
  // (0,0,0)→(0,3,0)→(0,3,0): HasChanged 0,1,0; Delta.y 0,3,0; DeltaOnHit.y captured 0,3,3 (holds).
  {
    StatefulValueState st;
    float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float vy[3] = {0.0f, 3.0f, 3.0f};
    const float wHC[3] = {0.0f, 1.0f, 0.0f};
    const float wDy[3] = {0.0f, 3.0f, 0.0f};   // Delta.y (signed) = out[2]
    const float wDoh[3] = {0.0f, 3.0f, 3.0f};  // DeltaOnHit.y = out[5] (held after the hit)
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("HasVec3Changed", {{"Value.x", 0.0f}, {"Value.y", vy[i]}, {"Value.z", 0.0f}, {"Threshold", 0.5f}, {"Mode", 0.0f}}, dt60, 0.0f, st, out);
      float whc = (injectBug && i == 1) ? 0.0f : wHC[i];
      bool pass = std::fabs(out[0] - whc) < eps && std::fabs(out[2] - wDy[i]) < eps && std::fabs(out[5] - wDoh[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasVec3Changed step%d HC=%.1f(want %.1f) Dy=%.1f DoH.y=%.1f -> %s\n", i + 1, out[0], whc, out[2], out[5], pass ? "PASS" : "FAIL");
    }
  }
  // ----- PeakLevel (4-output, proves out[3] seam): Threshold=0.5. value 0,1,1.2,2.5 (time 1,2,2.5,3) -----
  // increase 0,1,0.2,1.3 → FoundPeak 0,1,0,1; MovingSum 0,1,1.2,2.5 (accumulator at out[3]).
  {
    StatefulValueState st;
    float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 1.2f, 2.5f};
    const float tm[4] = {1.0f, 2.0f, 2.5f, 3.0f};
    const float wFP[4] = {0.0f, 1.0f, 0.0f, 1.0f};   // FoundPeak = out[1]
    const float wMS[4] = {0.0f, 1.0f, 1.2f, 2.5f};   // MovingSum = out[3]
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("PeakLevel", {{"Value", val[i]}, {"Threshold", 0.5f}, {"MinTimeBetweenPeaks", 0.0f}}, dt60, tm[i], st, out);
      float wms = (injectBug && i == 3) ? 1.3f : wMS[i];  // bug: MovingSum forgets to accumulate
      bool pass = std::fabs(out[1] - wFP[i]) < eps && std::fabs(out[3] - wms) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] PeakLevel step%d FoundPeak=%.1f(want %.1f) MovingSum=%.2f(want %.2f) -> %s\n", i + 1, out[1], wFP[i], out[3], wms, pass ? "PASS" : "FAIL");
    }
  }

  // ----- CountInt DEFAULT held-true free-running: counts EVERY evaluated frame → 1,2,3,4 -----
  // The load-bearing case (the BLOCK). With .t3 defaults (TriggerIncrement=true, OnlyCountChanges=false,
  // Delta=1) CountInt is a free-running per-frame counter: TiXL CountInt.cs:36 `if (triggeredIncrement)
  // Result.Value += delta;` fires on the raw LEVEL every evaluated frame. The FIRST cook reloads
  // DefaultValue=0 (CountInt.cs:45 !_initialized, AFTER the +=1), so it emits 0; then each held-true
  // cook adds 1 → 1,2,3,4. (A rising-edge port would stick at 0 forever — exactly the BLOCK.)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // init cook → 0
    bool p0 = std::fabs(out[0] - 0.0f) < eps;
    const float want[4] = {1.0f, 2.0f, 3.0f, 4.0f};  // free-running per-frame count
    bool seqOk = p0;
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 2.0f : want[i];  // bug: held-true stops counting (rising-edge regression) → stuck at 2
      bool pass = std::fabs(out[0] - w) < eps;
      seqOk = seqOk && pass;
      printf("[selftest-statefulvalue] CountInt.level step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
    ok = ok && seqOk;
  }
  // ----- CountInt OnlyCountChanges=true: counts ONLY on frames where the trigger value CHANGED -----
  // The gate (CountInt.cs:28-30) skips the WHOLE step on frames where neither trigger changed since
  // last frame. The init cook (inc=true, CHANGED from lastInc=false) is NOT gated → +=1 then the
  // !_initialized reload clobbers to default 0, sets initialized + stores lastInc=true.
  // Then TriggerIncrement = false,false,true,true,false,true (held OnlyCountChanges=true):
  //   f1 inc=false (CHANGED true→false → step: inc false so no add, hold 0, store prev=false)
  //   f2 inc=false (==prev false, notChanged → GATED, hold 0)
  //   f3 inc=true  (CHANGED false→true → step: +=1 → 1, store prev=true)
  //   f4 inc=true  (==prev true, notChanged → GATED, hold 1)   ← LEVEL would give 2; gate blocks it
  //   f5 inc=false (CHANGED true→false → step: no add, hold 1, store prev=false)
  //   f6 inc=true  (CHANGED false→true → step: +=1 → 2)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"OnlyCountChanges", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // init → 0, prev=true
    bool p0 = std::fabs(out[0] - 0.0f) < eps;
    const float trig[6] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[6] = {0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 2.0f};
    bool seqOk = p0;
    for (int i = 0; i < 6; ++i) {
      cookStatefulValueOp("CountInt", {{"TriggerIncrement", trig[i]}, {"OnlyCountChanges", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 3) ? 2.0f : want[i];  // bug: gate fails to skip held-true → counts again → 2
      bool pass = std::fabs(out[0] - w) < eps;
      seqOk = seqOk && pass;
      printf("[selftest-statefulvalue] CountInt.onlychg step%d(t=%.0f)=%.1f want=%.1f -> %s\n", i + 1, trig[i], out[0], w, pass ? "PASS" : "FAIL");
    }
    ok = ok && seqOk;
  }
  // ----- CountInt first-cook inits to DefaultValue, then LEVEL decrement, then ResetTrigger→DefaultValue -----
  // TiXL reloads DefaultValue on the FIRST cook (!_initialized), AFTER the step — so frame 1 = default(0)
  // regardless of any trigger. Then a held decrement → −1 (level, one frame). Then TriggerReset → 10.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 0.0f}, {"TriggerDecrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // first cook → default 0
    bool p1 = std::fabs(out[0] - 0.0f) < eps;  // !_initialized reload (any trigger overridden)
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 0.0f}, {"TriggerDecrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // level dec → −1
    bool p2 = std::fabs(out[0] - (-1.0f)) < eps;
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 0.0f}, {"TriggerReset", 1.0f}, {"DefaultValue", 10.0f}}, dt60, 0.0f, st, out);  // reset → 10
    float wR = injectBug ? -1.0f : 10.0f;  // bug: reset ignored
    bool pR = std::fabs(out[0] - wR) < eps;
    ok = ok && p1 && p2 && pR;
    printf("[selftest-statefulvalue] CountInt.dec/reset init=%.1f dec=%.1f reset=%.1f(want %.1f) -> %s\n",
           0.0f, -1.0f, out[0], wR, (p1 && p2 && pR) ? "PASS" : "FAIL");
  }
  // ----- CountInt Modulo=3 with held-true level inc: count 1,2,3,4,5 → 1,2,0,1,2 (C# truncated %) -----
  // Init cook reloads default 0; then held TriggerIncrement counts 1,2,3,4,5 each wrapped %3.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}, {"Modulo", 3.0f}}, dt60, 0.0f, st, out);  // init → 0
    const float want[5] = {1.0f, 2.0f, 0.0f, 1.0f, 2.0f};  // raw 1,2,3,4,5 %3
    bool seqOk = true;
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}, {"Modulo", 3.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 3.0f : want[i];  // bug: modulo dropped → 3 instead of 0
      bool pass = std::fabs(out[0] - w) < eps;
      seqOk = seqOk && pass;
      printf("[selftest-statefulvalue] CountInt.mod3 step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
    ok = ok && seqOk;
  }

  // ----- FlipBool rising-edge toggle: Trigger false→true→false→true → bool 0,1,1,0 -----
  // Toggles only on the rising edge; the held-true frame (step3) must NOT re-toggle.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float trig[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("FlipBool", {{"Trigger", trig[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: held-true re-toggles
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] FlipBool.toggle step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- FlipBool two clean rising edges 0→1→0→1 + ResetTrigger→DefaultValue(=0) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    // edge1: false→true → 1 ; arm low ; edge2: false→true → 0 ; arm low ; edge3: false→true → 1
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // 0→1
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("FlipBool", {{"Trigger", 0.0f}}, dt60, 0.0f, st, out);  // arm low (holds 1)
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // 1→0
    bool p2 = std::fabs(out[0] - 0.0f) < eps;
    cookStatefulValueOp("FlipBool", {{"Trigger", 0.0f}}, dt60, 0.0f, st, out);  // arm low (holds 0)
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // 0→1
    bool p3 = std::fabs(out[0] - 1.0f) < eps;
    // reset → DefaultValue(0), even though a rising edge is ALSO present (reset wins over the toggle)
    cookStatefulValueOp("FlipBool", {{"Trigger", 0.0f}}, dt60, 0.0f, st, out);  // arm low (holds 1)
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}, {"ResetTrigger", 1.0f}, {"DefaultValue", 0.0f}}, dt60, 0.0f, st, out);
    float wR = injectBug ? 1.0f : 0.0f;  // bug: reset loses to toggle → 1→0... err 1 (toggle of held-1)
    bool pR = std::fabs(out[0] - wR) < eps;
    bool flips = p1 && p2 && p3;
    ok = ok && flips && pR;
    printf("[selftest-statefulvalue] FlipBool.seq 0->1=%.1f 1->0=%.1f 0->1=%.1f reset(wins)=%.1f(want %.1f) -> %s\n",
           1.0f, 0.0f, 1.0f, out[0], wR, (flips && pR) ? "PASS" : "FAIL");
  }

  // ----- HasIntChanged (Mode=Changed=3): feed 5,5,7,7,3 → changed 0,0,1,0,1 -----
  // First frame compares against the zero-init lastValue (5≠0 → changed=1). To assert the brief's
  // "first/0" we seed with a leading 0 frame so the 5,5,7,7,3 run reads against a known prior.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasIntChanged", {{"Value", 0.0f}, {"ReturnTrueIf", 3.0f}}, dt60, 0.0f, st, out);  // seed lastValue=0
    const float val[5] = {5.0f, 5.0f, 7.0f, 7.0f, 3.0f};
    const float want[5] = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f};  // 0→5 changes; 5,5 no; 5→7 yes; 7,7 no; 7→3 yes
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("HasIntChanged", {{"Value", val[i]}, {"ReturnTrueIf", 3.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 1.0f : want[i];  // bug: stale-value compare → false-fire on 5,5
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasIntChanged.changed step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- HasIntChanged Increased(1)/Decreased(2) modes + int-truncate (4.9 → 4) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasIntChanged", {{"Value", 5.0f}, {"ReturnTrueIf", 1.0f}}, dt60, 0.0f, st, out);  // seed last=5 (0→5 increased=1)
    cookStatefulValueOp("HasIntChanged", {{"Value", 4.9f}, {"ReturnTrueIf", 1.0f}}, dt60, 0.0f, st, out);  // 4.9→4 (int); 4<5 NOT increased → 0
    bool pInc = std::fabs(out[0] - 0.0f) < eps;
    cookStatefulValueOp("HasIntChanged", {{"Value", 4.0f}, {"ReturnTrueIf", 2.0f}}, dt60, 0.0f, st, out);  // last int=4; 4<4 false (Decreased) → 0
    bool pDec0 = std::fabs(out[0] - 0.0f) < eps;
    cookStatefulValueOp("HasIntChanged", {{"Value", 2.0f}, {"ReturnTrueIf", 2.0f}}, dt60, 0.0f, st, out);  // 2<4 → Decreased=1
    float wDec = injectBug ? 0.0f : 1.0f;  // bug: never fires
    bool pDec1 = std::fabs(out[0] - wDec) < eps;
    ok = ok && pInc && pDec0 && pDec1;
    printf("[selftest-statefulvalue] HasIntChanged.modes inc(4.9->4)=%.1f dec(4)=%.1f dec(2)=%.1f(want %.1f) -> %s\n",
           0.0f, 0.0f, out[0], wDec, (pInc && pDec0 && pDec1) ? "PASS" : "FAIL");
  }

  // ----- ToggleBoolean rising-edge flip: TriggerToggle 0,1,1,0,1 → active 0,1,1,1,0 -----
  // Flips ONLY on the rising edge (the SetTypedInputValue(false) self-clear == an edge debounce). The
  // HELD-true frame (step3) must NOT re-flip — exactly the held-input case (the CountInt-blood lesson).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float trig[5] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[5] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f};  // edge flips: f2 0→1, f5 0→1 (back to 0)
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("ToggleBoolean", {{"TriggerToggle", trig[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: held-true re-flips (level, not edge)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] ToggleBoolean.edge step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- ToggleBoolean reset leg: flip on, then TriggerReset edge clears to false -----
  // edge-toggle to 1, then a rising-edge TriggerReset forces false (checked AFTER toggle in TiXL).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("ToggleBoolean", {{"TriggerToggle", 1.0f}}, dt60, 0.0f, st, out);  // 0→1
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("ToggleBoolean", {{"TriggerToggle", 0.0f}, {"TriggerReset", 1.0f}}, dt60, 0.0f, st, out);  // reset edge → 0
    float wR = injectBug ? 1.0f : 0.0f;  // bug: reset ignored
    bool pR = std::fabs(out[0] - wR) < eps;
    ok = ok && p1 && pR;
    printf("[selftest-statefulvalue] ToggleBoolean.reset on=%.1f reset=%.1f(want %.1f) -> %s\n", 1.0f, out[0], wR, (p1 && pR) ? "PASS" : "FAIL");
  }

  // ----- FlipFlop LEVEL set + HOLD: Trigger 0,1,1,0 → result 0,1,1,1 (holds after Trigger drops) -----
  // Trigger only ever SETS to true (no edge, no clear). step4 Trigger=0 must HOLD the prior 1 (no else
  // branch in TiXL) — the load-bearing held/hold case. A clear-on-low bug would drop step4 to 0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float trig[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 1.0f, 1.0f};  // set on f2, holds through f3 (level) and f4 (drop)
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("FlipFlop", {{"Trigger", trig[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 3) ? 0.0f : want[i];  // bug: Trigger-low clears (loses the latch)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] FlipFlop.setHold step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- FlipFlop reset leg: set true, then LEVEL ResetTrigger → DefaultValue(0); reset wins -----
  // After latching true, ResetTrigger=1 reloads DefaultValue (here 0) even though Trigger=1 also held
  // (reset is checked first → wins over the set).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("FlipFlop", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // set → 1
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("FlipFlop", {{"Trigger", 1.0f}, {"ResetTrigger", 1.0f}, {"DefaultValue", 0.0f}}, dt60, 0.0f, st, out);  // reset wins → 0
    float wR = injectBug ? 1.0f : 0.0f;  // bug: trigger wins over reset → stays 1
    bool pR = std::fabs(out[0] - wR) < eps;
    ok = ok && p1 && pR;
    printf("[selftest-statefulvalue] FlipFlop.reset set=%.1f reset(wins)=%.1f(want %.1f) -> %s\n", 1.0f, out[0], wR, (p1 && pR) ? "PASS" : "FAIL");
  }

  // ----- HasBooleanChanged DEFAULT Mode=Increased(1): Value 0,1,1,0,1 → changed 0,1,0,0,1 -----
  // The .t3 default is Increased (NOT Changed) — fires ONLY on False→True. lastValue inits false.
  // f1 0 (0!=0? no)→0; f2 1 (1!=0 && 1)→1; f3 1 (held, 1==1)→0 [held case]; f4 0 (Decreased, &&new=F)→0;
  // f5 1 (False→True again)→1. step3 (held-true) and step4 (True→False) both 0 prove the Increased gate.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[5] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[5] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("HasBooleanChanged", {{"Value", val[i]}, {"Mode", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 3) ? 1.0f : want[i];  // bug: treats Increased as Changed → True→False false-fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasBoolChanged.inc step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- HasBooleanChanged Changed(0) + Decreased(2) modes -----
  // Changed: fires on EITHER edge. Decreased: fires ONLY True→False.
  {
    // Changed(0): Value 0,1,1,0 → 0,1,0,1 (both edges fire; held step3 → 0).
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasBooleanChanged", {{"Value", val[i]}, {"Mode", 0.0f}}, dt60, 0.0f, st, out);
      bool pass = std::fabs(out[0] - want[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasBoolChanged.changed step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], want[i], pass ? "PASS" : "FAIL");
    }
  }
  {
    // Decreased(2): Value 0,1,0,0 → 0,0,1,0 (only True→False at f3 fires).
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float want[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasBooleanChanged", {{"Value", val[i]}, {"Mode", 2.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: Decreased never fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasBoolChanged.dec step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== Trigger (TiXL bool/logic/Trigger.cs) =====
  // (A) OnlyOnDown=true (.t3 default): a held-true BoolValue pulses EXACTLY ONCE on the rising edge.
  // BoolValue 0,1,1,0,1 → Result 0,1,0,0,1 (f2 rising edge fires; f3 held-true does NOT re-fire; f5
  // re-fires after the input dropped at f4). _isSet inits false → frame-1 true would itself be an edge.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[5] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[5] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("Trigger", {{"BoolValue", val[i]}, {"OnlyOnDown", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 1.0f : want[i];  // bug: held-true re-fires (level, not edge)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Trigger.down step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // (B) OnlyOnDown=false: transparent pass-through of the raw level. BoolValue 0,1,1,0 → Result 0,1,1,0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("Trigger", {{"BoolValue", val[i]}, {"OnlyOnDown", 0.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: pass-through edge-gates (drops held-true)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Trigger.thru step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== KeepBoolean (TiXL bool/process/KeepBoolean.cs) — bool twin of FreezeValue + TimeSinceFreeze =====
  // (A) Mode=FreezeWhileTrue(0): track Value while !Freeze, HOLD while Freeze. TimeSinceFreeze counts
  // up from each rising Freeze edge (LocalTime → seam `time`). frames (time,Value,Freeze):
  //   (1,1,0): not frozen → Result=1; no freeze edge, freezeTime=0 → TSF=1.
  //   (2,1,1): freeze rising edge → freezeTime=2, hold Result=1 → TSF=0.
  //   (3,0,1): still frozen → hold Result=1 → TSF=3-2=1. (Value=0 ignored while frozen — proves HOLD.)
  //   (4,0,0): unfrozen → track Result=0 → TSF=4-2=2 (freezeTime unchanged → clock keeps running).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float tm[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const float val[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    const float frz[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float wR[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float wT[4] = {1.0f, 0.0f, 1.0f, 2.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("KeepBoolean", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 0.0f}}, dt60, tm[i], st, out);
      float wr = (injectBug && i == 2) ? 0.0f : wR[i];  // bug: tracks Value while frozen (no hold)
      bool pass = std::fabs(out[0] - wr) < eps && std::fabs(out[1] - wT[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] KeepBool.while step%d R=%.1f(want %.1f) TSF=%.2f -> %s\n", i + 1, out[0], wr, out[1], pass ? "PASS" : "FAIL");
    }
  }
  // (B) Mode=UpdateWhenSwitchingToTrue(1): sample Value ONLY on the rising Freeze edge, hold otherwise.
  // frames (Value,Freeze): (1,0)(1,1)(0,1) → Result 0,1,1 (f1 no edge → init 0; f2 edge samples Value=1;
  //   f3 no edge → holds 1, ignoring Value=0). Proves edge-sampling, not level-tracking.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[3] = {1.0f, 1.0f, 0.0f};
    const float frz[3] = {0.0f, 1.0f, 1.0f};
    const float wR[3] = {0.0f, 1.0f, 1.0f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("KeepBoolean", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 1.0f}}, dt60, 0.0f, st, out);
      float wr = (injectBug && i == 2) ? 0.0f : wR[i];  // bug: re-samples Value every frozen frame
      bool pass = std::fabs(out[0] - wr) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] KeepBool.switch step%d R=%.1f want=%.1f -> %s\n", i + 1, out[0], wr, pass ? "PASS" : "FAIL");
    }
  }

  // ===== DampPeakDecay (TiXL floats/process/DampPeakDecay.cs) — snap UP, ease DOWN by Decay =====
  // Decay=0.5: Value 0,4,2,1 → Result 0,4,3,2.
  //   f1: 0>0? no → snap 0.            f2: 0>4? no → snap 4 (instant attack).
  //   f3: 4>2? yes → Lerp(4,2,0.5)=3.  f4: 3>1? yes → Lerp(3,1,0.5)=2.
  // f2 proves instant rise; f3/f4 prove the asymmetric ease-down. A symmetric damp would lag the rise.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 4.0f, 2.0f, 1.0f};
    const float want[4] = {0.0f, 4.0f, 3.0f, 2.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("DampPeakDecay", {{"Value", val[i]}, {"Decay", 0.5f}}, dt60, 0.0f, st, out);
      // injectBug on f2: a symmetric-damp bug would lag the rise (Lerp(0,4,0.5)=2 instead of snapping to 4).
      float w = (injectBug && i == 1) ? 2.0f : want[i];
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] DampPeakDecay step%d(v=%.0f)=%.3f want=%.3f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== HasTimeChanged (TiXL anim/time/HasTimeChanged.cs) — time-edge detector, ADVANCING clock =====
  // Multi-frame: `time` MUST vary per cook (that is the input). lastTime inits 0. DeltaTime=time−lastTime,
  // wasAdvanced=time>lastTime+thr, wasRewind=time<lastTime−thr. The .t3 default Mode=DidChange(2).
  // (A) Mode=DidChange(2), Threshold=0 — advance, paused frame, rewind. HasChanged + DeltaTime asserted.
  //   f1 t=0    : adv 0>0? no, rew? no            → HC=0, DT=0      ; lastTime→0
  //   f2 t=0.5  : adv 0.5>0 yes                    → HC=1, DT=0.5    ; lastTime→0.5
  //   f3 t=0.5  : adv no, rew no (paused)          → HC=0, DT=0      ; lastTime→0.5  (proves a still frame)
  //   f4 t=1.0  : adv yes                          → HC=1, DT=0.5    ; lastTime→1.0
  //   f5 t=0.3  : rew 0.3<1.0 yes (DidChange)      → HC=1, DT=-0.7   ; lastTime→0.3  (DidChange catches rewind)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float tm[5]  = {0.0f, 0.5f, 0.5f, 1.0f, 0.3f};
    const float wHC[5] = {0.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    const float wDT[5] = {0.0f, 0.5f, 0.0f, 0.5f, -0.7f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("HasTimeChanged", {{"Mode", 2.0f}, {"Threshold", 0.0f}, {"WhichTime", 1.0f}}, dt60, tm[i], st, out);
      // injectBug on f5: a no-rewind bug (DidChange treated as DidAdvanced) would miss the rewind → HC=0.
      float hc = (injectBug && i == 4) ? 0.0f : wHC[i];
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - wDT[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasTimeChanged.change step%d(t=%.1f) HC=%.1f(want %.1f) DT=%.3f(want %.3f) -> %s\n",
             i + 1, tm[i], out[0], hc, out[1], wDT[i], pass ? "PASS" : "FAIL");
    }
  }
  // (B) Mode=DidAdvanced(1): a rewind frame must NOT fire (proves the mode split vs DidChange above).
  //   f1 t=0.5 : adv yes                 → HC=1, DT=0.5 ; lastTime→0.5
  //   f2 t=0.2 : adv 0.2>0.5? no (rewind)→ HC=0, DT=-0.3; lastTime→0.2   (DidAdvanced ignores the rewind)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}}, dt60, 0.5f, st, out);
    bool p1 = std::fabs(out[0] - 1.0f) < eps && std::fabs(out[1] - 0.5f) < eps;
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}}, dt60, 0.2f, st, out);
    float hc = injectBug ? 1.0f : 0.0f;  // bug: DidAdvanced falsely fires on a rewind
    bool p2 = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - (-0.3f)) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.adv adv(t=0.5)=%.1f rewind(t=0.2)HC=%.1f(want %.1f) DT=%.3f -> %s\n",
           1.0f, out[0], hc, out[1], (p1 && p2) ? "PASS" : "FAIL");
  }
  // (C) Mode=DidRewind(0): the SAME rewind frame DOES fire (the mirror of (B)).
  //   f1 t=0.5 : rew no  → HC=0, DT=0.5 ; lastTime→0.5
  //   f2 t=0.2 : rew 0.2<0.5 yes → HC=1, DT=-0.3 ; lastTime→0.2
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 0.0f}}, dt60, 0.5f, st, out);
    bool p1 = std::fabs(out[0] - 0.0f) < eps;  // forward advance is NOT a rewind
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 0.0f}}, dt60, 0.2f, st, out);
    float hc = injectBug ? 0.0f : 1.0f;  // bug: DidRewind misses the rewind
    bool p2 = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - (-0.3f)) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.rewind adv(t=0.5)HC=%.1f rewind(t=0.2)HC=%.1f(want %.1f) -> %s\n",
           0.0f, out[0], hc, (p1 && p2) ? "PASS" : "FAIL");
  }
  // (D) Threshold band, Mode=DidAdvanced(1), Threshold=0.25: a small advance UNDER threshold must NOT fire.
  //   f1 t=0.1 : adv 0.1>0+0.25(0.25)? no  → HC=0, DT=0.1 ; lastTime→0.1   (under-threshold advance)
  //   f2 t=0.5 : adv 0.5>0.1+0.25(0.35)? yes→ HC=1, DT=0.4 ; lastTime→0.5
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}, {"Threshold", 0.25f}}, dt60, 0.1f, st, out);
    float hc1 = injectBug ? 1.0f : 0.0f;  // bug: ignores Threshold → tiny advance falsely fires
    bool p1 = std::fabs(out[0] - hc1) < eps && std::fabs(out[1] - 0.1f) < eps;
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}, {"Threshold", 0.25f}}, dt60, 0.5f, st, out);
    bool p2 = std::fabs(out[0] - 1.0f) < eps && std::fabs(out[1] - 0.4f) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.thresh small(t=0.1)HC=%.1f(want %.1f) big(t=0.5)HC=%.1f -> %s\n",
           out[0], hc1, 1.0f, (p1 && p2) ? "PASS" : "FAIL");
  }
  // (E) Mode=DidAdvancedWithMotionBlur(3), no __MotionBlurPass var (seam has no var-map) → DEGRADES to
  //   DidAdvanced: fires on advance, NOT on rewind. f1 t=0.4 adv → HC=1; f2 t=0.1 rewind → HC=0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 3.0f}}, dt60, 0.4f, st, out);
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 3.0f}}, dt60, 0.1f, st, out);
    float hc = injectBug ? 1.0f : 0.0f;  // bug: mode 3 falsely behaves as DidChange (fires on rewind)
    bool p2 = std::fabs(out[0] - hc) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.mb(varAbsent) adv(t=0.4)=%.1f rewind(t=0.1)HC=%.1f(want %.1f) -> %s\n",
           1.0f, out[0], hc, (p1 && p2) ? "PASS" : "FAIL");
  }

  // ===== StopWatch (TiXL anim/time/StopWatch.cs) — run-clock stopwatch, TRANSPORT-fed =====
  // Driven through an explicit TransportSnapshot (the playback-transport seam). The op reads the run
  // clock from tr.runTimeSecs (NOT dt/time), bpm/rate from tr. dt60/time args are inert for this op.
  auto trSnap = [](double runSecs, double bpm, double rate) {
    TransportSnapshot tr; tr.runTimeSecs = runSecs; tr.bpm = bpm; tr.rate = rate; return tr;
  };

  // ----- A: TimeInSecs Delta accumulation. No reset, rate=1, mode=TimeInSecs(0). _startTime stays 0,
  //   so Delta == runTime == Σdt. Drive runTime = 0.1, 0.2, .. 0.5 → Delta == that. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    bool pass = true;
    for (int i = 1; i <= 5; ++i) {
      const double runT = 0.1 * i;
      cookStatefulValueOp("StopWatch", {{"ResetTrigger", 0.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                          dt60, 0.0f, st, out, trSnap(runT, 120.0, 1.0));
      const float want = (float)runT;  // Delta = runTime - 0
      bool p = std::fabs(out[0] - want) < 1e-5f;
      pass = pass && p;
      printf("[selftest-statefulvalue] StopWatch.A.secs step%d(rt=%.1f) Delta=%.5f(want %.5f) -> %s\n",
             i, runT, out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  // ----- B: BeatTime mode, bpm=120. Delta == secs*120/240 == secs*0.5 bars. injectBug corrupts the
  //   bars constant (240→120) so the expected becomes secs*120/120==secs, which mismatches the correct
  //   secs*0.5 op output → Golden B flips RED. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    bool pass = true;
    for (int i = 1; i <= 4; ++i) {
      const double runT = 0.1 * i;
      cookStatefulValueOp("StopWatch", {{"ResetTrigger", 0.0f}, {"DurationIn", 1.0f}, {"PauseWithPlayback", 0.0f}},
                          dt60, 0.0f, st, out, trSnap(runT, 120.0, 1.0));
      const float wantGood = (float)(runT * 120.0 / 240.0);          // secs * 0.5 bars
      const float wantBug = (float)(runT * 120.0 / 120.0);           // 240→120 corruption (== secs)
      const float want = injectBug ? wantBug : wantGood;
      bool p = std::fabs(out[0] - want) < 1e-5f;
      pass = pass && p;
      printf("[selftest-statefulvalue] StopWatch.B.beat step%d(rt=%.1f) Delta=%.5f(want %.5f) -> %s\n",
             i, runT, out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  // ----- C: reset edge. Run rt=0.1,0.2,0.3 (no reset) → Delta=rt. Then rt=0.4 with ResetTrigger rising
  //   → LastDuration = 0.4-0 = 0.4 captured, baseline resets so Delta = 0.4-0.4 = 0. Hold ResetTrigger
  //   high at rt=0.5 (NO new edge, WasTriggered) → Delta = 0.5-0.4 = 0.1, LastDuration holds 0.4. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    bool pass = true;
    const double pre[3] = {0.1, 0.2, 0.3};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("StopWatch", {{"ResetTrigger", 0.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                          dt60, 0.0f, st, out, trSnap(pre[i], 120.0, 1.0));
      bool p = std::fabs(out[0] - (float)pre[i]) < 1e-5f && std::fabs(out[1] - 0.0f) < 1e-5f;
      pass = pass && p;
    }
    // rising edge at rt=0.4
    cookStatefulValueOp("StopWatch", {{"ResetTrigger", 1.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                        dt60, 0.0f, st, out, trSnap(0.4, 120.0, 1.0));
    bool pEdge = std::fabs(out[1] - 0.4f) < 1e-5f && std::fabs(out[0] - 0.0f) < 1e-5f;  // LastDuration=0.4, Delta=0
    // held high at rt=0.5: no new edge → Delta = 0.5-0.4 = 0.1, LastDuration still 0.4
    cookStatefulValueOp("StopWatch", {{"ResetTrigger", 1.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                        dt60, 0.0f, st, out, trSnap(0.5, 120.0, 1.0));
    bool pHold = std::fabs(out[0] - 0.1f) < 1e-5f && std::fabs(out[1] - 0.4f) < 1e-5f;
    pass = pass && pEdge && pHold;
    ok = ok && pass;
    printf("[selftest-statefulvalue] StopWatch.C.reset edge(rt=0.4)LastDur=%.5f Delta=%.5f hold(rt=0.5)Delta=%.5f LastDur=%.5f -> %s\n",
           0.4f, 0.0f, out[0], out[1], pass ? "PASS" : "FAIL");
  }

  // ----- D: PauseWithPlayback, rate threading. PauseWithPlayback=true → Delta = _accumulatedDuration,
  //   which advances ONLY when rate != 0. rt=0.1,0.2 (rate=1): accum 0.1,0.2. rt=0.3,0.4 (rate=0):
  //   accum FROZEN at 0.2. Proves rate=0 freezes the accumulated clock (the pause-detect seam). -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    // rate=1 frames: accumulated grows
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.1, 120.0, 1.0));
    bool p1 = std::fabs(out[0] - 0.1f) < 1e-5f;
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.2, 120.0, 1.0));
    bool p2 = std::fabs(out[0] - 0.2f) < 1e-5f;
    // rate=0 frames: accumulated FROZEN at 0.2 (run clock still advances, accum does not)
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.3, 120.0, 0.0));
    bool p3 = std::fabs(out[0] - 0.2f) < 1e-5f;
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.4, 120.0, 0.0));
    bool p4 = std::fabs(out[0] - 0.2f) < 1e-5f;
    bool pass = p1 && p2 && p3 && p4;
    ok = ok && pass;
    printf("[selftest-statefulvalue] StopWatch.D.pause rate1(0.1,0.2)=%.3f,%.3f rate0(0.3,0.4)frozen=%.3f,%.3f(want 0.200) -> %s\n",
           0.1f, 0.2f, 0.2f, out[0], pass ? "PASS" : "FAIL");
  }

  // ===== ConvertTime (TiXL anim/time/ConvertTime.cs) — bpm bars<->secs, transport-fed =====
  // BarsToSeconds(0) = time*240/bpm ; SecondsToBars(1) = time*bpm/240. The live bpm read is the point:
  // bpm=120 → B2S(1 bar)=2s, S2B(2s)=1 bar; bpm=240 → B2S(1 bar)=1s (proves it reads tr.bpm, not const).
  {
    StatefulValueState st;  // 0 state; reused, never written
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("ConvertTime", {{"Time", 1.0f}, {"Mode", 0.0f}}, dt60, 0.0f, st, out, trSnap(0, 120.0, 1.0));
    // injectBug: corrupt the 240 constant to 120 → B2S(1)@120 becomes 1*240/240=... no; inject swaps to *bpm/240
    const float wantB2S120 = injectBug ? (float)(1.0 * 120.0 / 240.0) : 2.0f;  // good = 1*240/120 = 2
    bool pa = std::fabs(out[0] - wantB2S120) < 1e-5f;
    printf("[selftest-statefulvalue] ConvertTime.B2S bpm120 time=1 -> %.5f(want %.5f) -> %s\n", out[0], wantB2S120, pa ? "PASS" : "FAIL");

    cookStatefulValueOp("ConvertTime", {{"Time", 2.0f}, {"Mode", 1.0f}}, dt60, 0.0f, st, out, trSnap(0, 120.0, 1.0));
    bool pb = std::fabs(out[0] - 1.0f) < 1e-5f;  // S2B(2s)@120 = 2*120/240 = 1 bar
    printf("[selftest-statefulvalue] ConvertTime.S2B bpm120 time=2 -> %.5f(want 1.00000) -> %s\n", out[0], pb ? "PASS" : "FAIL");

    cookStatefulValueOp("ConvertTime", {{"Time", 1.0f}, {"Mode", 0.0f}}, dt60, 0.0f, st, out, trSnap(0, 240.0, 1.0));
    bool pc = std::fabs(out[0] - 1.0f) < 1e-5f;  // B2S(1 bar)@240 = 1*240/240 = 1s — live-bpm proof
    printf("[selftest-statefulvalue] ConvertTime.B2S bpm240 time=1 -> %.5f(want 1.00000, live-bpm proof) -> %s\n", out[0], pc ? "PASS" : "FAIL");

    bool pass = pa && pb && pc;
    ok = ok && pass;
  }

  // ===== RunTime (TiXL anim/time/RunTime.cs) — TimeInSeconds = Playback.RunTimeInSecs (wall run clock) =====
  // Drive the run clock 0.5/1.0/1.5 (frame_cook accumulates dt) → out exactly tracks it, independent of
  // scrub/pause (rate=0, playhead frozen at bars=9.9 — RunTime ignores all of it). R-1 origin fork.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const double runs[3] = {0.5, 1.0, 1.5};
    bool pass = true;
    for (int i = 0; i < 3; ++i) {
      TransportSnapshot tr = trSnap(runs[i], 120.0, 0.0);  // rate=0 (paused), playhead irrelevant
      tr.localTimeBars = 9.9; tr.playbackTimeBars = 9.9;   // frozen playhead — RunTime must ignore it
      cookStatefulValueOp("RunTime", {}, dt60, 0.0f, st, out, tr);
      const float want = injectBug ? (float)runs[i] + 0.01f : (float)runs[i];
      bool p = std::fabs(out[0] - want) < 1e-5f;
      pass = pass && p;
      printf("[selftest-statefulvalue] RunTime step%d(runClk=%.1f,paused) -> %.5f(want %.5f) -> %s\n",
             i + 1, runs[i], out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  // ===== DelayTriggerChange (TiXL bool/process/DelayTriggerChange.cs:30-95) — two-edge change detector =====
  // currentTime is fed via the chosen TimeMode (AppRunTime/LocalFxTime); we drive tr to advance it.
  // Helper: cook one frame at a given run-clock with a given trigger.
  auto dtc = [&](StatefulValueState& st, float out[3], bool trig, double runClk, float dur,
                 int mode, int timeMode, double bpm) {
    TransportSnapshot tr = trSnap(runClk, bpm, 1.0);
    tr.localFxTimeBars = runClk; tr.localTimeBars = runClk; tr.playbackTimeBars = runClk;  // for bars/secs modes
    cookStatefulValueOp("DelayTriggerChange",
                        {{"Trigger", trig ? 1.0f : 0.0f}, {"DelayDuration", dur},
                         {"Mode", (float)mode}, {"TimeMode", (float)timeMode}},
                        dt60, 0.0f, st, out, tr);
  };

  // ----- A: DelayTrue, dur=1, AppRunTime(6). FAITHFUL first-second: s0 init=0 so before any edge
  //   remainingTime = 0 - currentTime + 1 > 0 while currentTime<1 → delayed=true even with Trigger=false.
  //   Then rising edge at t=2 (Trigger true): _lastTrueTime=2, remaining = 2-2+1 = 1>0 → delayed=true,
  //   passthrough is also true. At t=2.5 hold trigger: remaining = 2-2.5+1 = 0.5>0 → true. At t=3.5:
  //   remaining = 2-3.5+1 = -0.5 → not delayed → passthrough _triggered(true) → still true (held high). -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    dtc(st, out, false, 0.5, 1.0f, 0, 6, 120.0);  // t=0.5, Trigger=false
    bool pFaithful = (out[0] > 0.5f) && std::fabs(out[1] - 0.5f) < 1e-5f;  // delayed=true(faithful), remaining=0-0.5+1=0.5
    printf("[selftest-statefulvalue] DTC.A faithful t=0.5 Trig=F -> delayed=%.0f(want 1) remain=%.5f(want 0.50000) -> %s\n",
           out[0], out[1], pFaithful ? "PASS" : "FAIL");
    dtc(st, out, false, 1.5, 1.0f, 0, 6, 120.0);  // t=1.5, still false: remaining=0-1.5+1=-0.5<0 → passthrough false
    bool pAfter = (out[0] < 0.5f) && std::fabs(out[1] - (-0.5f)) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.A faithful-end t=1.5 Trig=F -> delayed=%.0f(want 0) remain=%.5f(want -0.50000) -> %s\n",
           out[0], out[1], pAfter ? "PASS" : "FAIL");
    dtc(st, out, true, 2.0, 1.0f, 0, 6, 120.0);  // rising edge t=2: lastTrue=2, remaining=2-2+1=1 → delayed true
    const float wantRemA = injectBug ? 0.0f : 1.0f;  // injectBug: drop +delayDuration → remaining=0
    bool pRise = (out[0] > 0.5f) && std::fabs(out[1] - wantRemA) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.A rise t=2 Trig=T -> delayed=%.0f(want 1) remain=%.5f(want %.5f) -> %s\n",
           out[0], out[1], wantRemA, pRise ? "PASS" : "FAIL");
    // t=3.5 held true: _lastTrueTime UPDATES every frame while triggered (cs:55-58 runs unconditionally
    // inside if(isTriggered), not just on the edge), so refTime=3.5, remaining=3.5-3.5+1=1>0 → delayed=true.
    dtc(st, out, true, 3.5, 1.0f, 0, 6, 120.0);
    bool pHold = (out[0] > 0.5f) && std::fabs(out[1] - 1.0f) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.A hold t=3.5 Trig=T -> delayed=%.0f(want 1) remain=%.5f(want 1.00000, lastTrue tracks) -> %s\n",
           out[0], out[1], pHold ? "PASS" : "FAIL");
    ok = ok && pFaithful && pAfter && pRise && pHold;
  }

  // ----- B: DelayFalse(1), dur=1, AppRunTime(6). stateIfDelayed=false, refTime=_lastFalseTime. Start
  //   Trigger=TRUE at t=0.5: no _lastFalseTime yet (s1=0), remaining=0-0.5+1=0.5>0 → delayed=stateIfDelayed=FALSE
  //   (DelayFalse holds the OFF state into the trigger). Drop to false (edge) at t=1.0: lastFalse=1,
  //   remaining=1-1+1=1>0 → delayed=false. At t=2.5: remaining=1-2.5+1=-0.5 → passthrough _triggered(false)=false.
  //   To see a TRUE we keep trigger high long enough: re-rise t=3 then t=5 (remaining=lastFalse(1)-5+1<0) → passthrough true. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    dtc(st, out, true, 0.5, 1.0f, 1, 6, 120.0);   // Trigger=T but DelayFalse holds false (remaining=0.5>0)
    bool p1 = (out[0] < 0.5f);
    dtc(st, out, false, 1.0, 1.0f, 1, 6, 120.0);  // edge to false: lastFalse=1, remaining=1-1+1=1>0 → false
    bool p2 = (out[0] < 0.5f) && std::fabs(out[1] - 1.0f) < 1e-5f;
    dtc(st, out, true, 5.0, 1.0f, 1, 6, 120.0);   // edge to true t=5: refTime=lastFalse=1, remaining=1-5+1=-3<0 → passthrough true
    const float wantB = injectBug ? 0.0f : 1.0f;  // injectBug breaks passthrough → 0
    bool p3 = std::fabs(out[0] - wantB) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.B DelayFalse: holdFalse@T=%.0f edgeFalse remain=%.5f passthroughTrue@t5=%.0f(want %.0f) -> %s\n",
           p1 ? 0.0f : 1.0f, out[1], out[0], wantB, (p1 && p2 && p3) ? "PASS" : "FAIL");
    ok = ok && p1 && p2 && p3;
  }

  // ----- C: bpm bars<->secs conversion of currentTime via TimeMode=1 (LocalFxTime_InSecs). DelayTrue, dur=1.
  //   We set localFxTimeBars=2 (bars), bpm=120 → currentTime = 2*240/120 = 4 secs. Rising edge there:
  //   lastTrue=4, remaining = 4-4+1 = 1 > 0 → delayed=true. Proves the bars→secs path uses tr.bpm. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    // prime _triggered=false at currentTime far back, then rising edge with the bars-secs time.
    {
      TransportSnapshot tr = trSnap(0, 120.0, 1.0); tr.localFxTimeBars = 0;
      cookStatefulValueOp("DelayTriggerChange",
                          {{"Trigger", 0.0f}, {"DelayDuration", 1.0f}, {"Mode", 0.0f}, {"TimeMode", 1.0f}},
                          dt60, 0.0f, st, out, tr);
    }
    TransportSnapshot tr = trSnap(0, 120.0, 1.0); tr.localFxTimeBars = 2.0;  // 2 bars @120 = 4 secs
    cookStatefulValueOp("DelayTriggerChange",
                        {{"Trigger", 1.0f}, {"DelayDuration", 1.0f}, {"Mode", 0.0f}, {"TimeMode", 1.0f}},
                        dt60, 0.0f, st, out, tr);
    const float wantRemC = injectBug ? (float)(2.0 * 120.0 / 240.0 - 0.0 + 1.0) : 1.0f;  // good: currentTime=4 → remaining=1; bug: wrong bars*bpm/240=1+1=2... distinct
    bool pc = (out[0] > 0.5f) && std::fabs(out[1] - wantRemC) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.C LocalFxSecs 2bars@120=4s rising -> delayed=%.0f(want 1) remain=%.5f(want %.5f) -> %s\n",
           out[0], out[1], wantRemC, pc ? "PASS" : "FAIL");
    ok = ok && pc;
  }

  // ----- D: DelayBoth(2), dur=1, AppRunTime(6). Reconstruct held _stateBeforeChange across an edge.
  //   DelayBoth: refTime=_lastChangeTime, stateIfDelayed=_stateBeforeChange (= the DelayedTrigger value
  //   just BEFORE this edge, carried via s[5]). Sequence:
  //   t=0.5 Trig=F: first frame, no edge (prev _triggered=0 == false → hasBeenChanged=false). delayed:
  //     refTime=_lastChangeTime=0, remaining=0-0.5+1=0.5>0 → stateIfDelayed=_stateBeforeChange=0 → false.
  //   t=1.0 Trig=T (EDGE): _stateBeforeChange = prior DelayedTrigger (false=0). lastChange=1.
  //     remaining=1-1+1=1>0 → delayed=stateIfDelayed=false (holds the PRE-edge state). passthrough would be true,
  //     but delayed wins → STILL FALSE (this is the "hold previous state during delay" behavior).
  //   t=2.5 Trig=T: no edge, remaining=1-2.5+1=-0.5<0 → not delayed → passthrough _triggered(true) → TRUE. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    dtc(st, out, false, 0.5, 1.0f, 2, 6, 120.0);  // no edge, delayed holds stateBeforeChange=0 → false
    bool p1 = (out[0] < 0.5f);
    dtc(st, out, true, 1.0, 1.0f, 2, 6, 120.0);   // EDGE: stateBeforeChange=prior false → delayed holds false despite Trigger=T
    bool p2 = (out[0] < 0.5f) && std::fabs(out[1] - 1.0f) < 1e-5f;
    dtc(st, out, true, 2.5, 1.0f, 2, 6, 120.0);   // delay elapsed → passthrough true
    const float wantD = injectBug ? 0.0f : 1.0f;
    bool p3 = std::fabs(out[0] - wantD) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.D DelayBoth: preEdge=%.0f edge-holds-prev(false)=%.0f remain=%.5f postDelay=%.0f(want %.0f) -> %s\n",
           p1 ? 0.0f : 1.0f, p2 ? 0.0f : 1.0f, out[1], out[0], wantD, (p1 && p2 && p3) ? "PASS" : "FAIL");
    ok = ok && p1 && p2 && p3;
  }

  printf("[selftest-statefulvalue] %s%s\n", ok ? "PASS" : "FAIL", injectBug ? " (injectBug)" : "");
  return ok ? 0 : 1;
}

}  // namespace sw
