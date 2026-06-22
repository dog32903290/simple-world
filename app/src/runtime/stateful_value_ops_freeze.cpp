// runtime/stateful_value_ops_freeze — the sample-and-hold / clock family:
//   FreezeValue / StopWatch.  (StopWatch reads the per-frame TransportSnapshot.)
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

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


}  // namespace

static const StatefulOpReg _reg_FreezeValue{"FreezeValue", stepFreezeValue};
static const StatefulOpReg _reg_StopWatch{"StopWatch", stepStopWatch};

}  // namespace sw
