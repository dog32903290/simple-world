// runtime/stateful_value_ops_numeric — the numeric accumulator/counter family:
//   DeltaSinceLastFrame / Accumulator / CountInt / PeakLevel / DampPeakDecay.
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

// --- DeltaSinceLastFrame (TiXL Lib/numbers/floats/process/DeltaSinceLastFrame.cs) ---
// Output = Value − previousValue. State: s[0]=lastValue (init 0 → first frame delta = Value).
// Ports: Value, Threshold (declared in TiXL but UNUSED in its math — kept for port parity, no fork).
void stepDeltaSinceLastFrame(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                             StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float value = getIn(in, "Value", 0.0f);
  out[0] = value - st.s[0];
  st.s[0] = value;
}

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

}  // namespace

static const StatefulOpReg _reg_DeltaSinceLastFrame{"DeltaSinceLastFrame", stepDeltaSinceLastFrame};
static const StatefulOpReg _reg_Accumulator{"Accumulator", stepAccumulator};
static const StatefulOpReg _reg_PeakLevel{"PeakLevel", stepPeakLevel};
static const StatefulOpReg _reg_CountInt{"CountInt", stepCountInt};
static const StatefulOpReg _reg_DampPeakDecay{"DampPeakDecay", stepDampPeakDecay};

}  // namespace sw
