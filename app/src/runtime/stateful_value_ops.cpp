#include "runtime/stateful_value_ops.h"

#include <cmath>
#include <cstdio>

namespace sw {
namespace {

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
              StatefulValueState& st, float out[3]) {
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
                   StatefulValueState& st, float out[3]) {
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
                             StatefulValueState& st, float out[3]) {
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
                     StatefulValueState& st, float out[3]) {
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
                StatefulValueState& st, float out[3]) {
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
void stepDampVec2(const std::map<std::string, float>& in, float dt, float, StatefulValueState& st, float out[3]) { dampVecImpl(in, dt, st, out, 2); }
void stepDampVec3(const std::map<std::string, float>& in, float dt, float, StatefulValueState& st, float out[3]) { dampVecImpl(in, dt, st, out, 3); }

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
void stepSpringVec2(const std::map<std::string, float>& in, float, float, StatefulValueState& st, float out[3]) { springVecImpl(in, st, out, 2); }
void stepSpringVec3(const std::map<std::string, float>& in, float, float, StatefulValueState& st, float out[3]) { springVecImpl(in, st, out, 3); }

struct StatefulOp {
  const char* type;
  void (*step)(const std::map<std::string, float>&, float, float, StatefulValueState&, float[3]);
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
};

const StatefulOp* findStatefulOp(const std::string& t) {
  for (const auto& o : kStatefulValueOps)
    if (t == o.type) return &o;
  return nullptr;
}

}  // namespace

bool isStatefulValueOp(const std::string& opType) { return findStatefulOp(opType) != nullptr; }

void cookStatefulValueOp(const std::string& opType, const std::map<std::string, float>& in,
                         float dt, float time, StatefulValueState& st, float out[3]) {
  if (const StatefulOp* o = findStatefulOp(opType)) o->step(in, dt, time, st, out);
}

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

  printf("[selftest-statefulvalue] %s%s\n", ok ? "PASS" : "FAIL", injectBug ? " (injectBug)" : "");
  return ok ? 0 : 1;
}

}  // namespace sw
