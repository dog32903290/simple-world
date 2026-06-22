// runtime/stateful_value_ops_transport — the transport-YELLOW consumer family:
//   ConvertTime / RunTime / DelayTriggerChange.  These read the per-frame TransportSnapshot.
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

}  // namespace

static const StatefulOpReg _reg_ConvertTime{"ConvertTime", stepConvertTime};
static const StatefulOpReg _reg_RunTime{"RunTime", stepRunTime};
static const StatefulOpReg _reg_DelayTriggerChange{"DelayTriggerChange", stepDelayTriggerChange};

}  // namespace sw
