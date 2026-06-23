// runtime/bpm_detection — implementation + isolated golden. Ported LINE-FOR-LINE from
// external/tixl/Editor/Gui/Interaction/Timing/BpmDetection.cs:18-182. See header for the algorithm.
#include "runtime/bpm_detection.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "runtime/anim_math.h"  // sw::clampf (anim_math.h:89) — reused for .Clamp, not reimplemented

namespace sw {

// BpmDetection.cs:177 — SampleBufferSize => (int)SampleDurationInSec.Clamp(1,60) * FramesPerSecond
int BpmDetection::sampleBufferSize() const {
  return (int)anim_math::clampf(sampleDurationInSec, 1.0f, 60.0f) * kFramesPerSecond;
}

// BpmDetection.cs:18-24
void BpmDetection::addFftSample(const float* fftBuffer, int n) {
  // BpmRangeMin = BpmRangeMin.Clamp(50, 200); BpmRangeMax = BpmRangeMax.Clamp(BpmRangeMin, 200);
  bpmRangeMin = (int)anim_math::clampf((float)bpmRangeMin, 50.0f, 200.0f);
  bpmRangeMax = (int)anim_math::clampf((float)bpmRangeMax, (float)bpmRangeMin, 200.0f);
  updateSampleBuffer(fftBuffer, n);
}

// BpmDetection.cs:86-112
void BpmDetection::updateSampleBuffer(const float* fftBuffer, int n) {
  // SampleDurationInSec = SampleDurationInSec.Clamp(1, 60);
  sampleDurationInSec = anim_math::clampf(sampleDurationInSec, 1.0f, 60.0f);
  const int bufSize = sampleBufferSize();
  if ((int)sampleBuffer_.size() != bufSize)
    sampleBuffer_.assign(bufSize, 0.0f);

  // fork-bpm-fft-resolution: TiXL uses FftResolution=512; sw passes a 1024-bin buffer, so the
  // borders are computed against kBpmFftResolution=1024. The NormalizedFrequencyRange*resolution
  // is a ratio, so the 0..0.2 band covers the same fraction of the normalized spectrum.
  int lowerBorder = (int)(anim_math::clampf(normalizedFrequencyRangeMin, 0.0f, 1.0f) * kBpmFftResolution);
  int upperBorder = (int)(anim_math::clampf(normalizedFrequencyRangeMax, 0.0f, 1.0f) * kBpmFftResolution);
  if (upperBorder > n) upperBorder = n;  // never read past the caller's buffer
  if (upperBorder <= lowerBorder)
    return;

  addedSampleCount_++;

  float sum = 0.0f;
  for (int index = lowerBorder; index < upperBorder; index++) {
    sum += fftBuffer[index];
  }

  // _sampleBuffer.Add(sum); if (Count > SampleBufferSize) RemoveAt(0);  — sliding window.
  sampleBuffer_.push_back(sum);
  if ((int)sampleBuffer_.size() > bufSize)
    sampleBuffer_.erase(sampleBuffer_.begin());
}

// BpmDetection.cs:120-138 — subtract a boxcar moving-average to cancel volume/energy drift.
void BpmDetection::smoothBuffer() {
  const int count = (int)sampleBuffer_.size();
  if ((int)smoothedSampleBuffer_.size() != count)
    smoothedSampleBuffer_.assign(count, 0.0f);

  const int smoothSteps = 5;
  if (count < smoothSteps * 2 + 1)
    return;
  for (int i = smoothSteps; i < count - smoothSteps; i++) {
    float sum = 0.0f;
    // NOTE the .cs asymmetry: j runs [-5, 4] (10 terms, `j < smoothSteps`) but the divisor is
    // smoothSteps*2+1 = 11. Ported VERBATIM (BpmDetection.cs:131-136).
    for (int j = -smoothSteps; j < smoothSteps; j++) {
      sum += sampleBuffer_[i + j];
    }
    smoothedSampleBuffer_[i] = std::fmax(0.0f, sampleBuffer_[i] - sum / (smoothSteps * 2 + 1));
  }
}

// BpmDetection.cs:146-168 — coarse autocorrelation: difference between time-shifted copies.
float BpmDetection::measureEnergyDifference(float bpm) {
  const float dt = (240.0f / bpm * 60.0f / 4.0f);
  float sum = 0.1f;

  const int slideScans = 4;
  const int clipStart = (int)(240.0f / 80.0f * 60.0f / 4.0f) * slideScans + 1;

  const int len = (int)smoothedSampleBuffer_.size();
  for (int j = 1; j < slideScans; j++) {
    // -bug: detune the autocorrelation lag by a fixed +7 frames. The lag at which a beat aligns
    // with its time-shifted copy is what encodes the period (and hence the BPM); shifting every
    // lag by a constant moves the energy-difference minimum to a DIFFERENT dt, so the argmin
    // recovers a WRONG bpm (golden RED). Unlike a symmetric [i+offset] flip — which an even/
    // periodic signal is invariant to — a constant detune genuinely breaks the period match even
    // on a clean planted beat. Production path: offset = (int)(dt*j) verbatim.
    const int offset = injectBug_ ? (int)(dt * j) + 7 : (int)(dt * j);
    int startIndex = clipStart > offset ? clipStart : offset;  // Math.Max(clipStart, offset)
    if (startIndex >= len)
      continue;

    for (int i = startIndex; i < len; i++) {
      sum += std::fabs(smoothedSampleBuffer_[i] - smoothedSampleBuffer_[i - offset]);
    }
  }

  return sum;
}

// BpmDetection.cs:77-82 — fall-off curve biasing toward the current lock.
float BpmDetection::computeFocusFactor(float value, float targetValue, float range, float amplitude) {
  const float deviance = std::fabs(value - targetValue);
  const float bump =
      std::fmax(0.0f, 1.0f - (1.0f / (range * range) * deviance * deviance)) * amplitude + 1.0f;
  return std::fmax(bump, 1.0f);
}

// BpmDetection.cs:31-70
float BpmDetection::computeBpmRate() {
  const int bpmStepCount = bpmRangeMax - bpmRangeMin;
  if ((int)bpmEnergies_.size() != bpmStepCount)
    bpmEnergies_.assign(bpmStepCount, 0.0f);

  smoothBuffer();

  float bestBpm = 0.0f;
  float bestMeasurement = INFINITY;  // float.PositiveInfinity

  for (int bpm = bpmRangeMin; bpm < bpmRangeMax; bpm++) {
    const float m =
        measureEnergyDifference((float)bpm) / computeFocusFactor((float)bpm, currentBpm_, 4.0f, lockInFactor);
    if (m < bestMeasurement) {
      bestMeasurement = m;
      bestBpm = (float)bpm;
    }
    bpmEnergies_[bpm - bpmRangeMin] = m;
  }

  // searchOffsets refinement (BpmDetection.cs:55-66). NOTE: the hard 70..170 range here is NOT
  // the configurable BpmRangeMin/Max — it's a fixed asymmetric clamp ported verbatim from the .cs.
  for (float offset : searchOffsets_) {
    const float bpm = currentBpm_ + offset;
    if (bpm < 70.0f || bpm > 170.0f)
      continue;

    const float m = measureEnergyDifference(bpm) / computeFocusFactor(bpm, currentBpm_, 2.0f, 0.01f);
    if (!(m < bestMeasurement))
      continue;
    bestMeasurement = m;
    bestBpm = bpm;
  }

  currentBpm_ = bestBpm;
  return bestBpm;
}

// ============================ Isolated golden (Rule 5) ============================
//
// Closed-form recovery: synthesize a band-0..0.2 energy spike of height 1.0 every `period` frames
// (period derived from the SAME dt the algorithm autocorrelates against), feed sampleDuration*60
// frames, recover the planted BPM within ±1. Run for 120 AND 90 so a hard-coded answer can't pass.
// Intermediate-buffer equivalence re-derives SmoothBuffer + MeasureEnergyDifference from the .cs
// and matches to 5 decimals (the refuter anchor). hasSufficientSampleData guards before fill.
// injectBug drops the SmoothBuffer subtract -> recovered BPM wrong -> ±1 assertion FAILS (RED).

namespace {

// Period (in frames) of a beat at `bpm`, matching the algorithm's dt = 240/bpm*60/4. The planted
// spike lands every `dt` frames so the autocorrelation at offset dt sees aligned copies.
int beatPeriodFrames(float bpm) {
  return (int)std::lround(240.0f / bpm * 60.0f / 4.0f);
}

// Build a 1024-bin FFT frame: energy `height` packed into the band-0..0.2 window, ~0 elsewhere.
void makeFftFrame(std::vector<float>& fft, float height) {
  fft.assign(kBpmFftResolution, 0.0f);
  const int upper = (int)(0.2f * kBpmFftResolution);  // mirror updateSampleBuffer's border math
  // spread the energy across the band so the per-frame band-sum == height (one bin would do too,
  // but spreading keeps it robust to the exact border rounding)
  const float per = height / (float)upper;
  for (int i = 0; i < upper; i++) fft[i] = per;
}

// Drive a fresh detector with a planted beat at `bpmTarget`, return the recovered BPM.
float recoverBpm(float bpmTarget, bool injectBug) {
  BpmDetection d;
  d.setInjectBug(injectBug);
  const int period = beatPeriodFrames(bpmTarget);
  const int frames = (int)(d.sampleDurationInSec * 60.0f);  // 1500 for the 25s default
  std::vector<float> spike, quiet;
  makeFftFrame(spike, 1.0f);
  makeFftFrame(quiet, 0.0f);
  for (int f = 0; f < frames; f++) {
    const bool onBeat = (f % period) == 0;
    d.addFftSample((onBeat ? spike : quiet).data(), kBpmFftResolution);
  }
  return d.computeBpmRate();
}

}  // namespace

int runBpmDetectionSelfTest(bool injectBug) {
  bool ok = true;

  // ---- (1) Closed-form recovery: 120 and 90, ±1, not hard-coded ----
  const float r120 = recoverBpm(120.0f, injectBug);
  const float r90 = recoverBpm(90.0f, injectBug);
  std::printf("[bpm-detect] recovered: target 120 -> %.3f, target 90 -> %.3f%s\n", r120, r90,
              injectBug ? "  (injectBug)" : "");
  if (std::fabs(r120 - 120.0f) > 1.0f) {
    std::printf("[bpm-detect] FAIL: 120 target recovered %.3f (>1 off)\n", r120);
    ok = false;
  }
  if (std::fabs(r90 - 90.0f) > 1.0f) {
    std::printf("[bpm-detect] FAIL: 90 target recovered %.3f (>1 off)\n", r90);
    ok = false;
  }

  // ---- (2) Intermediate-buffer equivalence: hand-derive SmoothBuffer + MeasureEnergyDifference
  //          from the .cs formulas for a SMALL fixed buffer, assert sw matches to ~5 decimals.
  //          (Refuter anchor — source of truth is the .cs, not self-consistency. NOT under -bug:
  //           this proves the production math; -bug is exercised only via (1)'s recovery.)
  {
    // Feed a known sequence into a 300-frame buffer (bufSize=300, via 5s duration), then
    // hand-derive SmoothBuffer at a few interior indices + MeasureEnergyDifference for a few bpm
    // values, FROM the .cs formulas. 300 > clipStart(181) so the autocorrelation inner loop
    // actually FIRES — the hand-derived MeasureEnergyDifference is a real non-trivial number, not
    // just the 0.1 seed. Source of truth = the .cs math, not self-consistency. (Always clean —
    // the bug only flows through (1)'s recovery via injectBug.)
    constexpr int kN = 300;
    BpmDetection e;
    e.sampleDurationInSec = 5.0f;  // bufSize = (int)clamp(5,1,60)*60 = 300
    // band-sum per frame seq[f] = f % 7 (exactly representable; structure for the autocorrelation).
    std::vector<float> fft;
    for (int f = 0; f < kN; f++) {
      makeFftFrame(fft, (float)(f % 7));  // band-sum == f%7 exactly (per*upper == height)
      e.addFftSample(fft.data(), kBpmFftResolution);
    }
    e.smoothBufferForTest();
    const std::vector<float>& sm = e.smoothedSampleBuffer();

    // Hand-derive SmoothBuffer (BpmDetection.cs:128-137): for interior i in [5, kN-6],
    //   cleaned[i] = max(0, s[i] - (sum_{j=-5}^{4} s[i+j]) / 11)   with s[k] = k % 7.
    auto handSmooth = [](int i) {
      float sum = 0.0f;
      for (int j = -5; j < 5; j++) sum += (float)((i + j) % 7);
      const float v = (float)(i % 7) - sum / 11.0f;
      return v > 0.0f ? v : 0.0f;
    };
    for (int i : {10, 137, 280}) {
      const float hand = handSmooth(i);
      if (std::fabs(sm[i] - hand) > 1e-4f) {  // float accumulation tolerance
        std::printf("[bpm-detect] FAIL: SmoothBuffer[%d] sw=%.6f hand=%.6f\n", i, sm[i], hand);
        ok = false;
      }
    }

    // Hand-derive MeasureEnergyDifference (BpmDetection.cs:148-167) over the SAME smoothed buffer:
    //   dt = 240/bpm*60/4 ; clipStart = (int)(240/80*60/4)*4 + 1 = 45*4+1 = 181 ; sum starts 0.1;
    //   for j in [1,3]: offset=(int)(dt*j); start=max(181,offset); if start<len accumulate
    //   abs(sm[i]-sm[i-offset]). With len=300 the autocorrelation runs for real.
    auto handMeasure = [&](float bpm) {
      const float dt = 240.0f / bpm * 60.0f / 4.0f;
      float sum = 0.1f;
      const int clipStart = (int)(240.0f / 80.0f * 60.0f / 4.0f) * 4 + 1;
      const int len = (int)sm.size();
      for (int j = 1; j < 4; j++) {
        const int offset = (int)(dt * j);
        const int start = clipStart > offset ? clipStart : offset;
        if (start >= len) continue;
        for (int i = start; i < len; i++) sum += std::fabs(sm[i] - sm[i - offset]);
      }
      return sum;
    };
    for (float bpm : {120.0f, 90.0f, 160.0f}) {
      const float hand = handMeasure(bpm);
      const float swv = e.measureEnergyDifferenceForTest(bpm);
      // relative tolerance — sum accumulates hundreds of float terms.
      if (std::fabs(swv - hand) > 1e-3f * std::fabs(hand) + 1e-5f) {
        std::printf("[bpm-detect] FAIL: MeasureEnergyDifference(%.0f) sw=%.6f hand=%.6f\n", bpm, swv, hand);
        ok = false;
      } else if (!injectBug) {
        std::printf("[bpm-detect] intermediate match: MeasureEnergyDifference(%.0f) sw=%.6f hand=%.6f\n",
                    bpm, swv, hand);
      }
    }
  }

  // ---- (3) hasSufficientSampleData guard: false before the buffer fills ----
  {
    BpmDetection g;
    g.sampleDurationInSec = 1.0f;  // bufSize = 60
    std::vector<float> fft;
    makeFftFrame(fft, 1.0f);
    if (g.hasSufficientSampleData()) {
      std::printf("[bpm-detect] FAIL: hasSufficientSampleData true with empty buffer\n");
      ok = false;
    }
    for (int f = 0; f < 59; f++) g.addFftSample(fft.data(), kBpmFftResolution);
    if (g.hasSufficientSampleData()) {
      std::printf("[bpm-detect] FAIL: hasSufficientSampleData true at 59/60 samples\n");
      ok = false;
    }
    g.addFftSample(fft.data(), kBpmFftResolution);  // 60th
    if (!g.hasSufficientSampleData()) {
      std::printf("[bpm-detect] FAIL: hasSufficientSampleData false at 60/60 samples\n");
      ok = false;
    }
  }

  if (ok) std::printf("[bpm-detect] PASS\n");
  return ok ? 0 : 1;
}

}  // namespace sw
