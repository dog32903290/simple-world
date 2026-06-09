#include "runtime/audio_reaction.h"

#include <cmath>
#include <cstdio>

namespace sw {
namespace {
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

AudioReactionOut cookAudioReaction(const SpectrumSnapshot& spec, const AudioReactionParams& p,
                                   double time, AudioReactionState& st) {
  // Reset on the rising edge (TiXL MathUtils.WasTriggered).
  if (p.reset && !st.prevReset) { st.hitCount = 0; st.accumulatedLevel = 0.0; }
  st.prevReset = p.reset;

  const double timeSinceLastHit = time - st.lastHitTime;

  // Pick the bin array by InputBand mode (TiXL InputModes).
  const float* bins = spec.bands.data();
  int count = kBandCount;
  switch (p.inputBand) {
    case 0: bins = spec.fftGain.data();       count = kFftBins;   break;  // RawFft
    case 1: bins = spec.fftNormalized.data(); count = kFftBins;   break;  // NormalizedFft
    case 2: bins = spec.bands.data();         count = kBandCount; break;  // FrequencyBands
    case 3: bins = spec.peaks.data();         count = kBandCount; break;  // FrequencyBandsPeaks
    case 4: bins = spec.attacks.data();       count = kBandCount; break;  // FrequencyBandsAttacks
    default: break;
  }

  const float windowCenter = clampf(p.windowCenter, 0.0f, 1.0f);
  const float windowWidth  = clampf(p.windowWidth, 0.0f, 1.0f);
  const float windowEdge   = clampf(p.windowEdge, 0.0001f, 1.0f);

  float sum = 0.0f;
  bool wasHit = false;
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      const float f = (count > 1) ? (float)i / (count - 1) : 0.0f;
      const float factor =
          1.0f - clampf(std::fabs((f - windowCenter) / windowEdge) - windowWidth / windowEdge, 0.0f, 1.0f);
      sum += bins[i] * factor;
    }
    sum /= (count * (windowWidth + windowEdge / 2.0f));

    const bool couldBeHit = sum > p.threshold;
    if (couldBeHit != st.isHitActive) {
      if (!st.isHitActive && timeSinceLastHit >= p.minTimeBetweenHits) {
        wasHit = true;
        st.isHitActive = couldBeHit;
        st.lastHitTime = time;
        st.hitCount++;
      } else {
        st.isHitActive = couldBeHit;
      }
    }
    if (p.threshold > 1e-6f)
      st.accumulatedLevel += std::pow((sum * 2.0) / p.threshold, 2.0) * 0.001 * p.amplitude;
  }
  st.sum = sum;

  float v = 0.0f;
  switch (p.output) {
    case 0:  // Pulse — a decaying spike after each hit
      v = std::pow(clampf(1.0f - (float)timeSinceLastHit, 0.0f, 1.0f), p.bias) * p.amplitude;
      break;
    case 1:  // TimeSinceHit
      v = std::pow((float)std::max(0.0, timeSinceLastHit), p.bias) * p.amplitude;
      break;
    case 2:  // Count
      v = (float)st.hitCount * p.amplitude;
      break;
    case 3:  // Level — the windowed band energy
      v = std::pow(sum, p.bias) * p.amplitude;
      break;
    case 4: {  // AccumulatedLevel — ever-growing, wrapped to keep float precision
      const double wrap = 10000.0;
      v = (float)(st.accumulatedLevel - std::floor(st.accumulatedLevel / wrap) * wrap);
      break;
    }
    default:
      v = std::pow(sum, p.bias) * p.amplitude;
      break;
  }
  if (!std::isfinite(v)) v = 0.0f;

  AudioReactionOut out;
  out.level = v;
  out.wasHit = wasHit;
  out.hitCount = st.hitCount;
  return out;
}

// ---- Isolated proof -------------------------------------------------------------------
int runAudioReactionSelfTest(bool injectBug) {
  SpectrumSnapshot spec;  // all-zero bands
  AudioReactionParams p;
  p.inputBand = 2;        // FrequencyBands
  p.windowCenter = 0.5f; p.windowWidth = 1.0f; p.windowEdge = 1.0f;  // whole-spectrum window
  p.threshold = 0.2f; p.minTimeBetweenHits = 0.1f; p.amplitude = 1.0f; p.bias = 1.0f;
  p.output = 3;           // Level

  // Level mode: one energized band -> a positive windowed level.
  AudioReactionState st;
  spec.bands[16] = 0.8f;
  const AudioReactionOut o1 = cookAudioReaction(spec, p, 1.0, st);
  const bool levelOk = o1.level > 0.0f;

  // Hit detection on a fresh state: a loud spectrum rising past Threshold fires WasHit once.
  AudioReactionState st2;
  for (auto& b : spec.bands) b = 1.0f;
  const AudioReactionOut h1 = cookAudioReaction(spec, p, 1.00, st2);
  const bool hit1 = h1.wasHit && h1.hitCount == 1;

  // Still loud immediately after -> already active, no double-count.
  const AudioReactionOut h2 = cookAudioReaction(spec, p, 1.02, st2);
  const bool noDouble = !h2.wasHit && h2.hitCount == 1;

  // Drop to silence (deactivate), then loud again after minTime -> hit #2.
  for (auto& b : spec.bands) b = 0.0f;
  cookAudioReaction(spec, p, 1.05, st2);
  for (auto& b : spec.bands) b = 1.0f;
  const AudioReactionOut h3 = cookAudioReaction(spec, p, 1.30, st2);
  const bool hit2 = h3.wasHit && h3.hitCount == 2;

  // Reset zeroes the hit count.
  p.reset = true;
  const AudioReactionOut r = cookAudioReaction(spec, p, 1.31, st2);
  const bool resetOk = r.hitCount == 0;

  bool ok = levelOk && hit1 && noDouble && hit2 && resetOk;
  if (injectBug) ok = !ok;
  std::printf("[selftest-audioreaction] level=%.4f hit1=%d noDouble=%d hit2=%d(count=%d) reset=%d -> %s\n",
              o1.level, hit1, noDouble, hit2, h3.hitCount, resetOk, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
