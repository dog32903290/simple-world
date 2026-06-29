// runtime/stateful_value_ops_spring — Spring family (Spring / SpringVec2 / SpringVec3).
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn / getInC / lerpf

namespace sw {
namespace {

// --- Spring (TiXL Lib/numbers/float/process/Spring.cs) ---
// Ports: Value, Tension, Strength, UseAppRunTime(bool). State: s[0]=springedValue, s[1]=result.
// Fork (named): TiXL Spring uses NO dt term — it is purely iterative per frame (frame-rate
//   dependent); kept faithful (dt is ignored). fork-damp-useapprunetime-inert: UseAppRunTime is
//   EXPOSED for parity (default false) but FAITHFULLY INERT — TiXL's SpringDamp samples
//   Playback.LastFrameDuration regardless; the knob only fed the 1ms guard, which is dropped for the
//   same once-per-frame-cook reason as Damp. Not read here; changes no output. (Same fork as Damp.)
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

}  // namespace

static const StatefulOpReg _reg_Spring{"Spring", stepSpring};
static const StatefulOpReg _reg_SpringVec2{"SpringVec2", stepSpringVec2};
static const StatefulOpReg _reg_SpringVec3{"SpringVec3", stepSpringVec3};

}  // namespace sw
