// runtime/beat_synchronizer — port of TiXL Core/Audio/BeatSynchronizer.cs.
// See beat_synchronizer.h for the FORK list. Zone: runtime (pure computation).
//
// Faithful port notes:
//   - State is file-static (TiXL BeatSynchronizer is a C# static class → process-global).
//   - We compute in MILLISECONDS internally so every magic constant (BarDurationMs,
//     MinOnsetIntervalMs, ToleranceMs, the maxAge cleanup) stays byte-identical to TiXL.
//     The public API takes seconds (F1) and converts once at the seam.
//   - SumBandAttacks in TiXL reads AudioAnalysis.FrequencyBandOnSets (NOT the attacks array,
//     despite the method name); sw reads SpectrumSnapshot.onsets[] — the same 32-band signal.
#include "runtime/beat_synchronizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

namespace sw::runtime {

namespace {

// ---- FrequencyRangeType (TiXL enum; Undefined is the count sentinel) -----
enum FrequencyRangeType { kBass = 0, kSnare = 1, kHihat = 2, kUndefined = 3 };
static constexpr int kFreqBandCount = (int)kUndefined;  // = 3 tracked types

// ---- Configuration constants (TiXL private consts) -----
static constexpr double kMinBpm              = 50.0;   // BeatSynchronizer.MinBpm
static constexpr double kMaxBpm              = 190.0;  // BeatSynchronizer.MaxBpm
static constexpr double kMinOnsetIntervalMs  = 50.0;   // BeatSynchronizer.MinOnsetIntervalMs
static constexpr int    kOnsetHistoryWindow  = 200;    // OnsetHistoryWindowSizeFrames
static constexpr double kProportionalBpmAdjustment = -0.4;  // P gain (TiXL line ~173)
static constexpr double kPhaseAdjustmentAmount     = 0.01;  // phase nudge (TiXL line ~176)

// ---- FrequencyBand table (TiXL _bands) -----
struct FreqBand { FrequencyRangeType type; int startBand; int endBand; float onSetThresholdFactor; };
static const FreqBand kBands[] = {
    {kBass,  0, 7,  3.5f},   // SumBandAttacks bands [0,7]
    {kSnare, 4, 26, 3.0f},   // [4,26]
    {kHihat, 20, 31, 3.0f},  // [20,31]
};
static constexpr int kBandTableCount = (int)(sizeof(kBands) / sizeof(kBands[0]));

// ---- RhythmicTemplate table (TiXL _onsetRhythmicTemplates) -----
struct RhythmicTemplate { float normalizedBarPosition; float impactWeight; float toleranceMs; };
// Bass
static const RhythmicTemplate kTplBass[] = {
    {0.00f, 1.3f, 50}, {0.25f, 0.5f, 50}, {0.50f, 0.8f, 50}, {0.75f, 0.5f, 50},
};
// Snare
static const RhythmicTemplate kTplSnare[] = {
    {0.25f, 1.0f, 50}, {0.75f, 1.0f, 50}, {0.00f, 0.3f, 30}, {0.50f, 0.3f, 30},
};
// Hihat
static const RhythmicTemplate kTplHihat[] = {
    {0.000f, 1.0f, 60}, {0.125f, 0.7f, 60}, {0.250f, 0.5f, 60}, {0.375f, 0.7f, 60},
    {0.500f, 0.5f, 60}, {0.625f, 0.7f, 60}, {0.750f, 0.5f, 60}, {0.875f, 0.7f, 60},
};
struct TemplateSpan { const RhythmicTemplate* data; int count; };
static const TemplateSpan kTemplates[kFreqBandCount] = {
    {kTplBass,  (int)(sizeof(kTplBass) / sizeof(kTplBass[0]))},
    {kTplSnare, (int)(sizeof(kTplSnare) / sizeof(kTplSnare[0]))},
    {kTplHihat, (int)(sizeof(kTplHihat) / sizeof(kTplHihat[0]))},
};

// ---- Onset record (TiXL Onset) -----
struct Onset { double timeMs; float amplitude; FrequencyRangeType type; };

// ---- State (mirrors TiXL static fields) -----
static double s_currentBpm = 120.0;  // BeatSynchronizer._currentBpm
static double s_barTime     = 0.0;   // BeatSynchronizer._barTime (in BARS)
static bool   s_initialized = false; // BeatSynchronizer._initialized

static std::vector<Onset> s_detectedOnsets;                       // _detectedOnsets
static std::deque<float>  s_recentTypeOnsetStrengths[kFreqBandCount];  // _recentTypeOnsetStrengths
static float              s_totalTypeOnsetStrengths[kFreqBandCount] = {};  // _totalTypeOnsetStrengths
static double             s_lastAnyOnsetDetectionTimes[kFreqBandCount] = {};  // _lastAnyOnsetDetectionTimes

// ---- Helpers -----

// TiXL BarDurationMs => (60000 / _currentBpm) * 4.
static double barDurationMs() { return (60000.0 / s_currentBpm) * 4.0; }

// TiXL BeatSynchronizer.Initialize() — lazy, idempotent.
static void initialize() {
  if (s_initialized) return;
  for (int i = 0; i < kFreqBandCount; ++i) {
    s_recentTypeOnsetStrengths[i].clear();
    s_totalTypeOnsetStrengths[i] = 0.0f;
  }
  s_initialized = true;
}

// TiXL SumBandAttacks(startBand, endBand): mean of FrequencyBandOnSets over [start, end].
// NOTE: TiXL divides by (actualEndBand - startBand), NOT +1 — ported verbatim.
static float sumBandAttacks(const SpectrumSnapshot& spec, int startBand, int endBand) {
  float sum = 0.0f;
  int actualEndBand = std::min(endBand, kBandCount - 1);
  for (int i = startBand; i <= actualEndBand; ++i) sum += spec.onsets[i];
  int denom = actualEndBand - startBand;
  return denom != 0 ? sum / (float)denom : sum;  // guard div/0 (TiXL bands never hit this)
}

// TiXL TryDetectAndQueueOnsetStrength — update the per-type sliding window and check threshold.
static bool tryDetectAndQueueOnsetStrength(const FreqBand& band, float currentStrength,
                                           double currentTimeMs, Onset& outOnset) {
  int typeIndex = (int)band.type;
  outOnset = Onset{currentTimeMs, currentStrength, band.type};

  s_recentTypeOnsetStrengths[typeIndex].push_back(currentStrength);
  s_totalTypeOnsetStrengths[typeIndex] += currentStrength;

  if ((int)s_recentTypeOnsetStrengths[typeIndex].size() > kOnsetHistoryWindow) {
    s_totalTypeOnsetStrengths[typeIndex] -= s_recentTypeOnsetStrengths[typeIndex].front();
    s_recentTypeOnsetStrengths[typeIndex].pop_front();
  }

  float averageStrength = 0.0f;
  if (!s_recentTypeOnsetStrengths[typeIndex].empty()) {
    averageStrength = s_totalTypeOnsetStrengths[typeIndex] /
                      (float)s_recentTypeOnsetStrengths[typeIndex].size();
  }

  bool hasEnoughPause =
      (currentTimeMs - s_lastAnyOnsetDetectionTimes[typeIndex]) > kMinOnsetIntervalMs;
  bool hasEnoughStrength = currentStrength > averageStrength * band.onSetThresholdFactor * 1.4f;

  return hasEnoughPause && hasEnoughStrength;
}

// P gain — a runtime-mutable copy so the selftest can flip the sign for injectBug
// (mirrors TiXL hot-reload tweaking of proportionalBpmAdjustment; production = const value).
static double s_proportionalBpmAdjustment = kProportionalBpmAdjustment;

}  // anonymous namespace

// ---- Public API -----

void beatSyncResync(double initialBpm) {
  initialize();
  s_currentBpm = std::clamp(initialBpm, kMinBpm, kMaxBpm);
  s_barTime = (double)((int)(s_barTime / 4) * 4);  // reset to start of last measure
  s_detectedOnsets.clear();
  for (int i = 0; i < kFreqBandCount; ++i) s_lastAnyOnsetDetectionTimes[i] = 0.0;  // see note below
  for (int i = 0; i < kFreqBandCount; ++i) {
    s_recentTypeOnsetStrengths[i].clear();
    s_totalTypeOnsetStrengths[i] = 0.0f;
  }
}
// NOTE on _lastAnyOnsetDetectionTimes length: TiXL declares it as new double[FrequencyBandCount]
// where FrequencyBandCount == (int)Undefined == 3. UpdateBeatTimer indexes it by the BAND-table
// index (0..2), never by the 32 spectrum bands, and Resync clears its full .Length. We mirror that
// exactly: size kFreqBandCount (3), touch only [0,2].

void beatSyncUpdate(const SpectrumSnapshot& spec, double currentTimeSecs, double deltaTimeSecs) {
  initialize();

  // F1: caller-owned clock, converted to ms once at the seam (TiXL works in ms throughout).
  const double currentTimeMs = currentTimeSecs * 1000.0;
  const double deltaTimeMs   = deltaTimeSecs * 1000.0;

  // Advance time (TiXL: _barTime += _currentBpm/60/1000/4 * deltaTimeMs).
  s_barTime += s_currentBpm / 60.0 / 1000.0 / 4.0 * deltaTimeMs;

  const double maxAge = (60000.0 / kMinBpm * 2.0);
  s_detectedOnsets.erase(
      std::remove_if(s_detectedOnsets.begin(), s_detectedOnsets.end(),
                     [&](const Onset& o) { return currentTimeMs - o.timeMs > maxAge; }),
      s_detectedOnsets.end());

  // Find onsets per band.
  for (int index = 0; index < kBandTableCount; ++index) {
    const FreqBand& band = kBands[index];
    float currentOnsetStrength = sumBandAttacks(spec, band.startBand, band.endBand);
    Onset onset;
    if (!tryDetectAndQueueOnsetStrength(band, currentOnsetStrength, currentTimeMs, onset)) continue;
    // F5: EnableBeatSyncProfiling / KeepTraceData → no-op.
    s_lastAnyOnsetDetectionTimes[index] = currentTimeMs;
    s_detectedOnsets.push_back(onset);
  }

  // Adjust bpmRate through phase offset.
  double totalWeightedPhaseError = 0.0;
  double totalWeight = 0.0;

  if (!s_detectedOnsets.empty()) {
    const double barDurMs = barDurationMs();
    for (const Onset& onset : s_detectedOnsets) {
      double conceptualBarStartTimeMs = currentTimeMs - std::fmod(s_barTime, 1.0) * barDurMs;
      double offsetFromNearestConceptualBarStartMs =
          std::fmod(onset.timeMs - conceptualBarStartTimeMs, barDurMs);
      if (offsetFromNearestConceptualBarStartMs < 0)
        offsetFromNearestConceptualBarStartMs += barDurMs;
      double onsetNormalizedBarPosition = offsetFromNearestConceptualBarStartMs / barDurMs;

      const TemplateSpan& span = kTemplates[(int)onset.type];
      for (int t = 0; t < span.count; ++t) {
        const RhythmicTemplate& tpl = span.data[t];
        double rawErrorToTemplateNormalized = onsetNormalizedBarPosition - tpl.normalizedBarPosition;
        if (rawErrorToTemplateNormalized > 0.5) rawErrorToTemplateNormalized -= 1.0;
        if (rawErrorToTemplateNormalized < -0.5) rawErrorToTemplateNormalized += 1.0;

        double errorToTemplateMs = rawErrorToTemplateNormalized * barDurMs;
        if (!(std::abs(errorToTemplateMs) <= tpl.toleranceMs * 2)) continue;  // HACK (TiXL)

        totalWeightedPhaseError += rawErrorToTemplateNormalized * tpl.impactWeight;
        totalWeight += tpl.impactWeight * onset.amplitude;
      }
    }
  }

  double currentPhaseErrorNormalized = 0.0;
  bool hasRelevantOnsets = totalWeight > 0.0;
  if (hasRelevantOnsets) currentPhaseErrorNormalized = totalWeightedPhaseError / totalWeight;
  if (!hasRelevantOnsets) return;

  double bpmCorrection = s_proportionalBpmAdjustment * currentPhaseErrorNormalized;
  // F5: profiling trace → no-op.
  double phaseCorrection = currentPhaseErrorNormalized * kPhaseAdjustmentAmount;

  s_currentBpm += bpmCorrection;
  s_currentBpm = std::clamp(s_currentBpm, kMinBpm, kMaxBpm);
  s_barTime -= phaseCorrection;
}

double beatSyncBarProgress() { return s_barTime; }
double beatSyncCurrentBpm()  { return s_currentBpm; }

// ---- Isolated proof (Rule 5) -----
int runBeatSyncSelfTest(bool injectBug) {
  int fail = 0;
  auto expect = [&](const char* what, bool ok) {
    if (!ok) { ++fail; std::printf("  [beatsync] FAIL %s\n", what); }
    else std::printf("  [beatsync] ok   %s\n", what);
  };
  auto expectNear = [&](const char* what, double got, double want, double eps) {
    bool ok = std::abs(got - want) <= eps;
    if (!ok) { ++fail; std::printf("  [beatsync] FAIL %s got=%.8f want=%.8f\n", what, got, want); }
    else std::printf("  [beatsync] ok   %s = %.8f\n", what, got);
  };

  std::printf("[selftest] beatsync (audio-locked BPM/bar P-controller)\n");

  // Fresh state for a deterministic run (state is process-global; this is the only test touching it).
  s_currentBpm = 120.0;
  s_barTime = 0.0;
  s_initialized = false;
  s_detectedOnsets.clear();
  for (int i = 0; i < kFreqBandCount; ++i) {
    s_recentTypeOnsetStrengths[i].clear();
    s_totalTypeOnsetStrengths[i] = 0.0f;
  }
  for (int i = 0; i < kFreqBandCount; ++i) s_lastAnyOnsetDetectionTimes[i] = 0.0;
  // injectBug flips the P gain's sign → phase error is amplified, BPM diverges off its anchor.
  s_proportionalBpmAdjustment = injectBug ? -kProportionalBpmAdjustment : kProportionalBpmAdjustment;

  // ===== leg G1-a: pure bar-time advance with NO onsets is exactly the TiXL formula. =====
  {
    s_currentBpm = 120.0;
    s_barTime = 0.0;
    // 120 BPM → barDuration = 60/120*4 = 2.0s. A 100ms frame advances barTime by
    // 120/60/1000/4 * 100 = 0.05 bars. Empty spectrum → no onset → no correction.
    SpectrumSnapshot empty{};  // all-zero onsets
    const double dt = 0.1;     // 100ms
    double t = 0.0;
    for (int i = 0; i < 5; ++i) { t += dt; beatSyncUpdate(empty, t, dt); }
    // 5 frames × 0.05 bars = 0.25 bars, BPM untouched (no relevant onsets → early return).
    expectNear("G1-a: barTime advanced by currentBpm/60/1000/4*dtMs (no-onset)", s_barTime, 0.25, 1e-9);
    expectNear("G1-a: BPM untouched with no onsets", s_currentBpm, 120.0, 1e-12);
  }

  // ===== leg G1-b: a single bass onset at a KNOWN off-template phase → deterministic BPM nudge. =====
  // The onset is timestamped at the update moment, so its normalized bar position == fmod(_barTime,1)
  // at hit time. We drive _barTime to exactly 0.53 on the hit frame: that lands +0.03 past the bass
  // 0.50 template (within its ±0.05 tolerance window), giving a raw template error of +0.03.
  // The phase error is amplitude-WEIGHTED: currentPhaseErrorNormalized = Σ(raw·w) / Σ(w·amplitude).
  // With one matching template, that = raw / amplitude. The bass onset amplitude is sumBandAttacks
  // over [0,7] = (8 bands × 1.0) / (7) = 1.142857, so phaseError = 0.03 / 1.142857 = 0.02625.
  // Faithful P gain (-0.4): bpmCorrection = -0.4 × 0.02625 = -0.0105 → BPM DECREASES below 120.
  // injectBug flips the gain → bpmCorrection = +0.0105 → BPM INCREASES above 120. THAT sign is the tooth.
  {
    beatSyncResync(120.0);  // bpm=120 (clamped), barTime snapped to measure start (0)
    expectNear("G1-b: resync sets bpm to the clamped initial value", s_currentBpm, 120.0, 1e-12);

    SpectrumSnapshot bassHit{};
    for (int b = 0; b <= 7; ++b) bassHit.onsets[b] = 1.0f;  // strong energy in bass bands [0,7]
    SpectrumSnapshot silence{};

    // 5 silence frames @100ms: drive the bass per-type sliding average to ~0 (so the hit clears the
    // 3.5×1.4 threshold) and advance barTime to 5×0.05 = 0.25 bars, clock to 0.5s.
    double t = 0.0;
    for (int i = 0; i < 5; ++i) { t += 0.1; beatSyncUpdate(silence, t, 0.1); }
    expectNear("G1-b: barTime at 0.25 after 5 silence frames", s_barTime, 0.25, 1e-9);

    // Hit frame: dt = 0.56s advances barTime by 0.28 → exactly 0.53 (fmod = 0.53). Onset lands at
    // bar position 0.53. Onset interval since last detection (>500ms) clears the 50ms pause gate.
    double bpmBefore = s_currentBpm;
    t += 0.56;
    size_t before = s_detectedOnsets.size();
    beatSyncUpdate(bassHit, t, 0.56);
    expect("G1-b: the off-template bass onset was detected", s_detectedOnsets.size() > before);

    double bpmDelta = s_currentBpm - bpmBefore;
    // ONE faithful expectation (no injectBug branch): -0.4 × (0.03 / 1.142857) = -0.0105 → BPM
    // moves DOWN by ~0.0105. With injectBug the P gain's sign is flipped, so the SAME onset pushes
    // BPM UP by +0.0105 → this assertion FAILs (RED). That's the tooth: the controller diverges.
    expectNear("G1-b: faithful P controller nudges BPM down by 0.4×phaseError", bpmDelta, -0.0105, 1e-4);
    expect("G1-b: BPM stays within the [50,190] clamp", s_currentBpm >= kMinBpm && s_currentBpm <= kMaxBpm);
  }

  // restore the production gain (state is process-global).
  s_proportionalBpmAdjustment = kProportionalBpmAdjustment;

  std::printf("[selftest] beatsync %s (%d failures)\n", fail ? "FAIL" : "PASS", fail);
  return fail ? 1 : 0;
}

}  // namespace sw::runtime
