// runtime/stateful_value_ops_damp — Damp family (Damp / DampAngle / DampVec2 / DampVec3).
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
// Self-registers via StatefulOpReg (the data-driven sink that replaced kStatefulValueOps[]).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"           // StatefulValueState / TransportSnapshot / ContextVarMap
#include "runtime/stateful_value_op_registry.h"   // StatefulOpReg
#include "runtime/stateful_value_ops_internal.h"  // getIn / dampenFloat / methodOf / getInC / kVecComp / lerpf

namespace sw {
namespace {

// --- Damp (TiXL Lib/numbers/float/process/Damp.cs) ---
// Ports: Value, Damping, Method(enum 0=LinearInterpolation, 1=DampedSpring), UseAppRunTime(bool).
// State: s[0]=dampedValue, s[1]=velocity. First cook seeds dampedValue=Value (TiXL _isFirstEval).
// fork-damp-useapprunetime-inert (named): the UseAppRunTime [Input] is EXPOSED (inspector parity with
//   TiXL, default false) but FAITHFULLY INERT — it is NOT read here. In TiXL, UseAppRunTime only
//   selects the clock fed to the 1ms MinTimeElapsedBeforeEvaluation guard + _lastEvalTime bookkeeping;
//   DampFunctions.DampenFloat ITSELF samples Playback.LastFrameDuration regardless (MathUtils.cs:711).
//   frame_cook cooks each node exactly once per frame, so that sub-ms double-eval guard is DROPPED →
//   the clock the knob switches has no consumer → the knob changes no output. This is NOT a dead-knob
//   lie: it mirrors TiXL's own input, which likewise does not affect the smoothing math. (The 1ms guard
//   is dropped for the same once-per-frame reason.)
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
// fork-damp-useapprunetime-inert (same as Damp): UseAppRunTime exposed but inert; 1ms guard dropped.
// NOTE TiXL DampAngle has NO _isFirstEval — it damps from 0 on frame 1 (faithful: no init seeding here).
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

}  // namespace

static const StatefulOpReg _reg_Damp{"Damp", stepDamp};
static const StatefulOpReg _reg_DampAngle{"DampAngle", stepDampAngle};
static const StatefulOpReg _reg_DampVec2{"DampVec2", stepDampVec2};
static const StatefulOpReg _reg_DampVec3{"DampVec3", stepDampVec3};

}  // namespace sw
