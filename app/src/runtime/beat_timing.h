// runtime/beat_timing — continuous BeatTime for live/VJ situations.
//
// Port of TiXL Editor/Gui/Interaction/Timing/BeatTiming.cs (228 lines).
// BeatTime is a bar-unit continuous clock driven by tap-tempo or manual BPM.
//
// Zone: runtime (pure computation, zero app/ui/platform deps).
// Callers: ui/toolbar.cpp calls beatTimingUpdate() each frame and exposes the Tap button.
#pragma once

namespace sw::runtime {

// Advance the beat clock by one frame.
// runTimeSecs = process-lifetime wall clock in seconds (analogous to TiXL Playback.RunTimeInSecs).
// FORK: TiXL reads Playback.RunTimeInSecs as a static field; we accept it as a parameter for
// cleaner isolation (caller owns the clock — no static coupling to a global playback singleton).
void beatTimingUpdate(double runTimeSecs);

// Manually set BPM and clear all stored tap times.
// = TiXL BeatTiming.SetBpmRate(float bpm) — clears _tapTimes, resets _resynced.
void beatTimingSetBpm(float bpm);

// Record a tap event; accumulates up to 16 tap times and recomputes BPM from average inter-tap.
// = TiXL BeatTiming.TriggerSyncTap() — sets _tapTriggeredLastFrame = true (deferred to Update).
void beatTimingTriggerSyncTap();

// Resync to the nearest measure boundary without changing BPM.
// = TiXL BeatTiming.TriggerResyncMeasure() — sets _syncMeasureTriggeredLastFrame = true.
void beatTimingTriggerResyncMeasure();

// Current bar-unit beat time (1.0 = one bar = 4 beats at 4/4).
// = TiXL BeatTiming.BeatTime (double).
double beatTimingBeatTime();

// Current BPM derived from _beatDuration.
// = TiXL BeatTiming.Bpm — (float)(60f / _beatDuration).
float beatTimingBpm();

// Enable/disable the audio-locked BeatTime branch (= TiXL EnableAudioBeatLocking setting).
// When ON and a resync has happened, beatTimingUpdate() drives BeatTime from the smoothed
// BeatSynchronizer.BarProgress (sliding_average de-jittered) instead of the manual clock.
// DEFAULT off → manual-timing path stays byte-for-byte identical (existing golden unaffected).
void beatTimingSetAudioBeatLocking(bool enable);
bool beatTimingAudioBeatLockingEnabled();

// Reset ALL beat-timing state to defaults (120 BPM, cleared taps/resyncs, lock off, average cleared).
// Test-only entry so the audio-lock integration golden starts from a clean process-global state.
void beatTimingResetForTest();

}  // namespace sw::runtime
