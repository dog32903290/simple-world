// runtime/beat_timing — port of TiXL Editor/Gui/Interaction/Timing/BeatTiming.cs (228 lines).
// Zone: runtime — pure computation, zero dependencies on app/ui/platform/verify.
//
// Key design notes:
//   - BeatTime counts in BARS (1 bar = BeatsPerBar × BeatsPerMeasure beats).
//   - _beatDuration is the duration of ONE beat in seconds (not a bar).
//   - MeasureDuration = _beatDuration × BeatsPerMeasure (= _beatDuration × 4 at 4/4).
//   - All state is file-static (TiXL BeatTiming is a C# static class — single process-global).
//
// FORK list (every divergence from TiXL):
//   F1. Clock source: TiXL reads Playback.RunTimeInSecs (a global); we accept runTimeSecs as a
//       parameter — better isolation, caller owns the clock.
//   F2. Tap algorithm: TiXL uses AVERAGE inter-tap for _beatDuration (ProcessBeatTaps:
//       durationSum / stepCount) AND a phase-delta correction (_phaseDelta). We faithfully port
//       both — the MEDIAN algorithm in the task brief is NOT what TiXL does; we diverge FROM the
//       brief, not from TiXL. Documented here explicitly.
//   F3. ResyncMeasure implementation: TiXL calls BeatSynchronizer.Resync(Bpm) which integrates
//       with an audio-locked bar average (BeatSynchronizer is a separate class). We port only the
//       BeatSynchronizer-free path: snap _syncMeasureOffset so that BeatTime lands on an integer
//       bar boundary. This is the VJ use-case (manual resync without audio-locked BPM drift).
//   F4. AudioBeatLocking branch (AdvanceBeatTime): TiXL has
//       "if (EnableAudioBeatLocking && _resynced)" — uses BeatSynchronizer.BarProgress.
//       sw has no audio-beat-locking infrastructure; we always take the manual-timing branch.
//   F5. Log.Warning / Log.Debug: replaced with no-ops (no logging subsystem in runtime).
//   F6. MaxTapsCount: TiXL uses 16 (BeatTiming.cs private const MaxTapsCount = 16); task brief
//       says 8 in the header comment but TiXL source is authoritative — we use 16.
#include "runtime/beat_timing.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sw::runtime {

namespace {

// ---- Constants (TiXL BeatTiming.cs private fields) -----
static constexpr int kBeatsPerBar    = 4;   // BeatTiming.BeatsPerBar = 4
static constexpr int kBarsPerMeasure = 4;   // BeatTiming.BarsPerMeasure = 4
static constexpr int kBeatsPerMeasure = kBeatsPerBar * kBarsPerMeasure;  // = 16
static constexpr int kMaxTapsCount    = 16; // BeatTiming.MaxTapsCount = 16 (F6: task says 8, TiXL = 16)
static constexpr double kThreshold    = 0.3;// BeatTiming.Threshold = 0.3

// ---- State (mirrors TiXL static fields) -----
static double s_beatTime        = 0.0;   // BeatTiming.BeatTime
static double s_beatDuration    = 0.5;   // BeatTiming._beatDuration (0.5s = 120 BPM)
static double s_phaseDelta      = 0.0;   // BeatTiming._phaseDelta
static double s_measureStartTime = 0.0;  // BeatTiming._measureStartTime
static double s_measureCount    = 0.0;   // BeatTiming._measureCount
static double s_syncMeasureOffset = 0.0; // BeatTiming._syncMeasureOffset
static bool   s_resynced        = false; // BeatTiming._resynced
static bool   s_tapTriggeredLastFrame      = false;
static bool   s_syncMeasureTriggeredLastFrame = false;

static std::vector<double> s_tapTimes;      // BeatTiming._tapTimes
static std::vector<double> s_resyncTimes;   // BeatTiming._resyncTimes

// ---- Helpers -----

// BeatTiming.MeasureDuration (property)
static double measureDuration() {
  return s_beatDuration * kBeatsPerMeasure;
}

// TiXL BeatTiming.GetDeltaToSync(double time) — offset from nearest beat boundary.
static double getDeltaToSync(double time) {
  double timeInMeasure = time - s_measureStartTime;
  double beatCount = std::round(std::abs(timeInMeasure) / s_beatDuration);
  return timeInMeasure - s_beatDuration * beatCount;
}

// Append runTime to queue, evicting old entries.
// = TiXL BeatTiming.AppendRunTimeToQueue (local function inside Update).
static void appendRunTimeToQueue(std::vector<double>& list, int maxLength,
                                  double maxDuration, double runTime) {
  while (list.size() > (size_t)maxLength ||
         (list.size() > 1 && runTime - list[0] > maxDuration)) {
    list.erase(list.begin());
  }
  list.push_back(runTime);
}

}  // anonymous namespace

// ---- Public API -----

void beatTimingUpdate(double runTimeSecs) {
  // F1 fork: runTimeSecs passed in by caller instead of read from Playback.RunTimeInSecs.
  const double runTime = runTimeSecs;

  // TiXL BeatTiming.Update() — DistanceToMeasure / DistanceToBeat (debug details, not stored).
  // We compute them locally where needed.

  bool tappedBeatSync = s_tapTriggeredLastFrame;
  s_tapTriggeredLastFrame = false;
  bool tappedMeasureSync = s_syncMeasureTriggeredLastFrame;
  s_syncMeasureTriggeredLastFrame = false;

  if (tappedMeasureSync) {
    // F3 fork: no BeatSynchronizer.Resync() — just mark resynced so AdvanceBeatTime below
    // snaps to measure boundary via the _syncMeasureOffset path.
    s_resynced = true;
  }

  // TiXL BeatTiming.Update() lines ~65-73: fix BPM if out of control.
  // "if (double.IsNaN(Bpm) || Bpm < 20 || Bpm > 600)"
  {
    float bpm = beatTimingBpm();
    if (std::isnan(bpm) || bpm < 20.0f || bpm > 600.0f) {
      // F5 fork: no Log.Warning
      s_beatDuration    = 0.5; // 120 BPM
      s_phaseDelta      = 0.0;
      s_syncMeasureOffset = 0.0;
      s_tapTimes.clear();
      s_resyncTimes.clear();
    }
  }

  // ------ ProcessBeatTaps (TiXL ~L78-L113) ------
  // FORK F2: TiXL uses AVERAGE inter-tap + phase correction, not median.
  if (tappedBeatSync) {
    s_resynced = false;

    appendRunTimeToQueue(s_tapTimes, kMaxTapsCount, 2.0 * measureDuration(), runTime);

    // TiXL: "if (_tapTimes.Count < 4)" → skip BPM update, zero phase
    if ((int)s_tapTimes.size() < 4) {
      s_phaseDelta = 0.0;
    } else {
      // TiXL: "if (runTime - lastTapTime > 4)" → clear and reset
      double lastTapTime = s_tapTimes.back();
      if (runTime - lastTapTime > 4.0) {
        s_phaseDelta = 0.0;
        s_tapTimes.clear();
      } else {
        double durationSum    = 0.0;
        double phaseOffsetSum = 0.0;
        double lastT          = 0.0;

        for (int i = 0; i < (int)s_tapTimes.size(); ++i) {
          double t  = s_tapTimes[i];
          double dt = t - lastT;
          lastT = t;
          if (i == 0) continue;
          phaseOffsetSum += getDeltaToSync(t);
          durationSum    += dt;
        }

        int stepCount       = (int)s_tapTimes.size() - 1;
        s_beatDuration      = durationSum / stepCount;       // AVERAGE inter-tap (TiXL line ~L107)
        s_phaseDelta        = phaseOffsetSum / stepCount;    // average phase correction
      }
    }
  }

  // ------ ProcessMeasureSyncTaps (TiXL ~L115-L141) ------
  if (tappedMeasureSync) {
    double distanceToMeasure = 1.0 - std::abs(std::fmod(s_beatTime, 4.0) / 4.0 - 0.5) * 2.0;

    if (distanceToMeasure > kThreshold) {
      // F5 fork: no Log.Debug
      s_resyncTimes.clear();
      s_resyncTimes.push_back(runTime);
    } else {
      appendRunTimeToQueue(s_resyncTimes, 8, 8.5 * measureDuration(), runTime);
      if ((int)s_resyncTimes.size() > 1) {
        double totalMeasureDuration = runTime - s_resyncTimes[0];
        double totalMeasureCount = std::round(totalMeasureDuration / measureDuration());
        if (totalMeasureCount > 0.0) {
          // TiXL: "Refining BPM rate {originalBpm} -> {Bpm}"
          s_beatDuration = totalMeasureDuration / totalMeasureCount / kBeatsPerMeasure;
        }
      }
    }

    // Snap the measure offset so BeatTime lands on a measure boundary at current runTime.
    s_syncMeasureOffset = -(runTime - s_measureStartTime) / measureDuration();
  }

  // ------ AdvanceBeatTime (TiXL ~L143-L162) ------
  {
    double timeInMeasure = runTime - s_measureStartTime;
    if (timeInMeasure > measureDuration()) {
      s_measureCount++;
      s_measureStartTime += measureDuration();
    }

    // F4 fork: no AudioBeatLocking branch (requires BeatSynchronizer).
    // We always take the manual-timing path:
    // "var tInMeasure = (runTime - _measureStartTime) / MeasureDuration;"
    // "BeatTime = (_measureCount + tInMeasure + _syncMeasureOffset) * BeatsPerBar;"
    double tInMeasure = (runTime - s_measureStartTime) / measureDuration();
    s_beatTime = (s_measureCount + tInMeasure + s_syncMeasureOffset) * kBeatsPerBar;
  }
}

void beatTimingSetBpm(float bpm) {
  // TiXL BeatTiming.SetBpmRate(float bpm):
  //   _beatDuration = 60f / bpm;
  //   _tapTimes.Clear();
  //   _resynced = false;
  s_beatDuration = 60.0 / (double)bpm;
  s_tapTimes.clear();
  s_resynced = false;
}

void beatTimingTriggerSyncTap() {
  // TiXL BeatTiming.TriggerSyncTap() => _tapTriggeredLastFrame = true
  s_tapTriggeredLastFrame = true;
}

void beatTimingTriggerResyncMeasure() {
  // TiXL BeatTiming.TriggerResyncMeasure() => _syncMeasureTriggeredLastFrame = true
  s_syncMeasureTriggeredLastFrame = true;
}

double beatTimingBeatTime() { return s_beatTime; }

float beatTimingBpm() {
  // TiXL BeatTiming.Bpm => (float)(60f / _beatDuration)
  if (s_beatDuration < 1e-9) return 120.0f;  // guard div/0
  return (float)(60.0 / s_beatDuration);
}

}  // namespace sw::runtime
