// runtime/tap_provider — the beat-tap / resync / slide-sync hand-off singleton for [ForwardBeatTaps].
//
// = TiXL Core/IO/TapProvider.Instance (referenced by ForwardBeatTaps.cs:19-35). The ONE process-global
// channel between the [ForwardBeatTaps] VJ operator (the writer) and the editor's per-frame beat/resync
// consumer. Mirror of bpm_provider.h (the [SetBpm] BpmProvider) — the SAME process-global provider pattern
// (BpmProvider.Instance / TapProvider.Instance are both static readonly singletons in T3.Core.IO), so the
// two providers are shaped identically.
//
// ForwardBeatTaps.cs writes THREE fields (cs:23,27,34):
//   BeatTapTriggered = WasTriggered(TriggerBeatTap, ref _wasBeatTriggered)  → a RISING-edge pulse (one frame)
//   ResyncTriggered  = WasTriggered(TriggerResync,  ref _wasResyncTriggered) → a RISING-edge pulse
//   SlideSyncTime    = SlideSyncTimeOffset  (only when NOT NaN; NaN leaves the previous value, cs:30-35)
//
// NAMED FORK `tapprovider-edge-state-process-global`: TiXL's rising-edge memory (_wasBeatTriggered /
// _wasResyncTriggered) is PER-INSTANCE on the ForwardBeatTaps object; sw's cmd-rail ops carry no per-node
// cross-frame state channel, so the edge memory rides HERE on the process-global provider — behaviour-
// identical for the single-instance VJ usage (a comp has one ForwardBeatTaps, exactly as it has one SetBpm
// writing the single BpmProvider). The edge-detect (WasTriggered) is done inside setTriggers() so the
// provider owns both the previous-state memory AND the published pulse, one source of truth.
//
// Zone: runtime leaf — pure host state, no hardware / no UI. ForwardBeatTaps (a runtime cmd op) writes it;
// the editor's per-frame loop (app) reads it. runtime→runtime and app→runtime are both legal directions.
#pragma once

namespace sw {

// Mirror of T3.Core.IO.TapProvider (the singleton + its published fields + the edge memory). Not
// thread-safe (TiXL's isn't either — single editor thread writes & reads). Defaults match the C# field
// zero-init: every trigger false, SlideSyncTime 0.
class TapProvider {
 public:
  // = TapProvider.Instance. The one process-global.
  static TapProvider& instance();

  // = ForwardBeatTaps.cs:22-27 (the operator's write): edge-detect BOTH trigger LEVELS against the stored
  // previous state (MathUtils.WasTriggered — a false→true RISING edge; MathUtils.cs:531-538) and publish the
  // resulting pulses. beatLevel / resyncLevel are the raw bool input levels this frame (TriggerBeatTap /
  // TriggerResync). Call ONCE per ForwardBeatTaps cook per frame.
  void setTriggers(bool beatLevel, bool resyncLevel);

  // = ForwardBeatTaps.cs:29-35: set SlideSyncTime to `offset` UNLESS it is NaN (NaN → leave the prior value,
  // the cs:30 !float.IsNaN gate). Call after setTriggers() (order matches the .cs body).
  void setSlideSyncTime(float offset);

  // The published pulses (this-frame rising-edge results) + the slide-sync value. = the static getters
  // ForwardBeatTaps exposes (cs:46-48). The per-frame consumer reads these.
  bool beatTapTriggered() const { return beatTapTriggered_; }
  bool resyncTriggered() const { return resyncTriggered_; }
  float slideSyncTime() const { return slideSyncTime_; }

  // Test seam only: reset the singleton to its zero-init state between golden cases (TiXL has no such
  // reset — the process-global persists for the app's life; the golden needs a clean slate per case).
  void resetForTest();

 private:
  TapProvider() = default;
  // Published (the getters). Pulses are RISING-edge (true for exactly the one frame the level rose).
  bool beatTapTriggered_ = false;
  bool resyncTriggered_ = false;
  float slideSyncTime_ = 0.0f;
  // Edge memory (fork tapprovider-edge-state-process-global): the previous trigger LEVEL, for WasTriggered.
  bool wasBeatTriggered_ = false;   // = ForwardBeatTaps._wasBeatTriggered
  bool wasResyncTriggered_ = false; // = ForwardBeatTaps._wasResyncTriggered
};

}  // namespace sw
