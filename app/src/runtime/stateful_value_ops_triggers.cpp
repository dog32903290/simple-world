// runtime/stateful_value_ops_triggers — the trigger/latch family:
//   DetectPulse / Trigger / FlipBool / ToggleBoolean / FlipFlop / KeepBoolean.
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn / lerpf

namespace sw {
namespace {

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

}  // namespace

static const StatefulOpReg _reg_DetectPulse{"DetectPulse", stepDetectPulse};
static const StatefulOpReg _reg_Trigger{"Trigger", stepTrigger};
static const StatefulOpReg _reg_FlipBool{"FlipBool", stepFlipBool};
static const StatefulOpReg _reg_ToggleBoolean{"ToggleBoolean", stepToggleBoolean};
static const StatefulOpReg _reg_FlipFlop{"FlipFlop", stepFlipFlop};
static const StatefulOpReg _reg_KeepBoolean{"KeepBoolean", stepKeepBoolean};

}  // namespace sw
