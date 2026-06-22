// runtime/stateful_value_ops_change_detectors — the change/edge detector family:
//   HasValueIncreased / HasValueDecreased / HasValueChanged / HasVec2Changed / HasVec3Changed /
//   HasIntChanged / HasBooleanChanged / HasTimeChanged.
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn / getInC

namespace sw {
namespace {

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

}  // namespace

static const StatefulOpReg _reg_HasValueIncreased{"HasValueIncreased", stepHasValueIncreased};
static const StatefulOpReg _reg_HasValueDecreased{"HasValueDecreased", stepHasValueDecreased};
static const StatefulOpReg _reg_HasValueChanged{"HasValueChanged", stepHasValueChanged};
static const StatefulOpReg _reg_HasVec2Changed{"HasVec2Changed", stepHasVec2Changed};
static const StatefulOpReg _reg_HasVec3Changed{"HasVec3Changed", stepHasVec3Changed};
static const StatefulOpReg _reg_HasIntChanged{"HasIntChanged", stepHasIntChanged};
static const StatefulOpReg _reg_HasBooleanChanged{"HasBooleanChanged", stepHasBooleanChanged};
static const StatefulOpReg _reg_HasTimeChanged{"HasTimeChanged", stepHasTimeChanged};

}  // namespace sw
