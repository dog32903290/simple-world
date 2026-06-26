// runtime/beat_synchronizer — audio-locked BPM/bar tracker.
//
// Port of TiXL Core/Audio/BeatSynchronizer.cs (static class). Latches onto the
// soundtrack's tempo: from an initial (tapped) BPM + downbeat it watches per-band
// onsets, compares them to RhythmicTemplates (bass on 1 & 1/4, snares on 2 & 4, …),
// derives a phase error, and runs a P-controller to nudge _currentBpm and _barTime
// so visual timing follows live tempo drift.
//
// Zone: runtime — pure computation, zero app/ui/platform/verify deps. Feeds
// beat_timing's audio-lock branch (the consumer of sliding_average's smoothing).
//
// FORK list (every divergence from TiXL, mirrors beat_timing's F-numbering):
//   F1. Clock source: TiXL reads WasapiAudioInput.LastUpdateTime / TimeSinceLastUpdate
//       (global WASAPI clock, ms). sw accepts currentTimeSecs / deltaTimeSecs as
//       parameters — caller owns the clock (same手法 as beat_timing F1). Internally we
//       still work in ms to keep the magic constants (BarDurationMs, MinOnsetIntervalMs,
//       ToleranceMs) byte-identical to TiXL.
//   F2. Onset source: TiXL reads AudioAnalysis.FrequencyBandOnSets (static global). sw
//       feeds the same 32-band onset array via SpectrumSnapshot.onsets[] (= TiXL
//       FrequencyBandOnSets, identical band count) passed into beatSyncUpdate().
//   F5. EnableBeatSyncProfiling / DebugDataRecording.KeepTraceData → no-ops (no profiling
//       subsystem in runtime; same手法 as beat_timing F5 Log no-ops).
#pragma once

#include "runtime/spectrum_analyzer.h"

namespace sw::runtime {

// Manually resynchronize: set BPM (clamped to [50,190]) and snap _barTime to the start
// of the last measure. Called by the user via Resync (= TiXL BeatSynchronizer.Resync).
void beatSyncResync(double initialBpm);

// One audio-buffer update: advance _barTime, detect onsets from spec.onsets[], compute the
// templated phase error, and run the P-controller on BPM + bar phase.
// = TiXL BeatSynchronizer.UpdateBeatTimer (F1: caller passes the clock; F2: caller passes onsets).
void beatSyncUpdate(const SpectrumSnapshot& spec, double currentTimeSecs, double deltaTimeSecs);

// Current bar phase (in BARS, advanced unbounded — Resync floors it to a measure boundary;
// callers read _barTime % 1 for the within-bar fraction). = TiXL BeatSynchronizer.BarProgress.
double beatSyncBarProgress();

// Current estimated BPM. = TiXL BeatSynchronizer.CurrentBpm.
double beatSyncCurrentBpm();

// Isolated proof (Rule 5): a deterministic bass-on-downbeat onset stream drives phase error
// toward 0 and BPM toward stability; injectBug flips proportionalBpmAdjustment's sign so the
// controller diverges and the verdict FAILs.
int runBeatSyncSelfTest(bool injectBug);

}  // namespace sw::runtime
