#include "runtime/spectrum_analyzer.h"

#include <Accelerate/Accelerate.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sw {
namespace {

constexpr int   kLog2N    = 11;     // 2048 = 1<<11
constexpr int   kHop      = 512;    // run an FFT every 512 new samples (75% overlap @ 2048)
constexpr float kLowestHz = 55.0f;  // TiXL InitializeBandLookupTable
constexpr float kHighestHz = 15000.0f;
constexpr int   NoBand    = -1;

inline float remapClamp(float v, float inMin, float inMax, float outMin, float outMax) {
  float t = (v - inMin) / (inMax - inMin);
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return outMin + t * (outMax - outMin);
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

struct SpectrumAnalyzer::Impl {
  // --- vDSP FFT state ---
  FFTSetup setup = nullptr;
  std::array<float, kFftSize> hann{};
  float windowSum = 1.0f;
  std::array<float, kFftSize> windowed{};
  std::array<float, kFftBins> realp{};
  std::array<float, kFftBins> imagp{};
  std::array<float, kFftBins> mag{};

  // --- input ring (2048, circular) ---
  std::array<float, kFftSize> ring{};
  int  ringPos = 0;          // next write index
  long sinceFft = 0;         // new samples since the last FFT
  bool primed = false;       // have we filled the ring once?

  // --- bin->band lookup (rebuilt when sampleRate changes) ---
  std::array<int, kFftBins> bandForBin{};
  float tableSampleRate = 0.0f;

  // --- per-band persistent state (TiXL AudioAnalysisContext) ---
  std::array<float, kBandCount> bands{};
  std::array<float, kBandCount> peaks{};
  std::array<float, kBandCount> attacks{};
  std::array<float, kBandCount> attackPeaks{};
  std::array<float, kBandCount> onsets{};
  std::array<float, kBandCount> prevAboveAvg{};
  std::array<float, kBandCount> averages{};
  std::array<float, kBandCount> histSum{};
  std::vector<std::array<float, kBandCount>> hist;  // sliding-window history (ring of rows)
  int  histLen = 333;        // TiXL FrequencyBandHistoryLength = 1/0.003
  int  histPos = 0;
  int  histCount = 0;

  // --- lock-free publish (audio writes back buffer, flips index) ---
  SpectrumSnapshot buf[2];
  std::atomic<int> live{0};
  std::uint64_t frameCounter = 0;

  Impl() {
    setup = vDSP_create_fftsetup(kLog2N, kFFTRadix2);
    vDSP_hann_window(hann.data(), kFftSize, 0 /*standard Hann*/);
    float s = 0.0f;
    for (float w : hann) s += w;
    windowSum = s > 0.0f ? s : 1.0f;
    hist.assign(histLen, std::array<float, kBandCount>{});
  }
  ~Impl() {
    if (setup) vDSP_destroy_fftsetup(setup);
  }

  void rebuildBandTable(float sampleRate) {
    const float maxOctave = std::log2(kHighestHz / kLowestHz);
    for (int i = 0; i < kFftBins; ++i) {
      int bandIndex = NoBand;
      const float freq = (float)i / kFftBins * (sampleRate * 0.5f);
      if (i == 0) {
        bandIndex = NoBand;
      } else if (i < 6) {
        bandIndex = i - 1;  // TiXL: first few bins map directly to the lowest bands
      } else {
        const float octave = std::log2(freq / kLowestHz);
        const float octaveNorm = octave / maxOctave;
        bandIndex = (int)(octaveNorm * kBandCount);
        if (bandIndex >= kBandCount || bandIndex < 0) bandIndex = NoBand;
      }
      bandForBin[i] = bandIndex;
    }
    tableSampleRate = sampleRate;
  }

  // Linearize the ring (oldest..newest) into `windowed` already multiplied by the window,
  // run the real FFT, write magnitudes into `mag` (bin 0 = DC, ignored downstream).
  void runFft() {
    for (int i = 0; i < kFftSize; ++i) {
      const float s = ring[(ringPos + i) % kFftSize];  // ringPos = oldest sample
      windowed[i] = s * hann[i];
    }
    DSPSplitComplex split{realp.data(), imagp.data()};
    vDSP_ctoz(reinterpret_cast<const DSPComplex*>(windowed.data()), 2, &split, 1, kFftBins);
    vDSP_fft_zrip(setup, &split, 1, kLog2N, kFFTDirection_Forward);
    vDSP_zvabs(&split, 1, mag.data(), 1, kFftBins);
    // zrip is 2x-scaled and ctoz packs N reals into N/2 complex; 1/windowSum recovers a
    // ~unit gain for a full-scale sine (verified by the selftest's 0 dB band landing ~1).
    const float scale = 1.0f / windowSum;
    vDSP_vsmul(mag.data(), 1, &scale, mag.data(), 1, kFftBins);
  }

  void processFftUpdate(float gainFactor, float decayFactor) {
    SpectrumSnapshot& out = buf[1 - live.load(std::memory_order_relaxed)];

    // 1) FFT bins -> normalized dB; max-pool into bands.
    int lastTarget = -1;
    bands.fill(0.0f);
    for (int bin = 0; bin < kFftBins; ++bin) {
      const float gain = mag[bin] * gainFactor;
      const float gainDb = gain <= 0.000001f ? -1000.0f : 20.0f * std::log10(gain);
      const float normalized = remapClamp(gainDb, -80.0f, 0.0f, 0.0f, 1.0f);
      out.fftGain[bin] = gain;
      out.fftNormalized[bin] = normalized;

      const int band = bandForBin[bin];
      if (band == NoBand) continue;
      if (band != lastTarget) { bands[band] = 0.0f; lastTarget = band; }
      bands[band] = std::max(bands[band], normalized);
    }

    // 2) sliding-window averages (for onset detection).
    {
      std::array<float, kBandCount>& row = hist[histPos];
      for (int i = 0; i < kBandCount; ++i) {
        histSum[i] += bands[i] - row[i];  // add new, subtract the value leaving the window
        row[i] = bands[i];
      }
      histPos = (histPos + 1) % histLen;
      if (histCount < histLen) ++histCount;
      const float inv = histCount > 0 ? 1.0f / histCount : 0.0f;
      for (int i = 0; i < kBandCount; ++i) averages[i] = histSum[i] * inv;
    }

    // 3) peaks / attacks / attack-peaks / onsets (TiXL ProcessFftUpdate).
    for (int b = 0; b < kBandCount; ++b) {
      const float lastPeak = peaks[b];
      const float decayed = lastPeak * decayFactor;
      const float cur = bands[b];
      const float newPeak = std::max(decayed, cur);
      peaks[b] = newPeak;

      const float newAttack = clampf((newPeak - lastPeak) * 4.0f, 0.0f, 10000.0f);
      attacks[b] = std::max(newAttack, attacks[b] * decayFactor);
      attackPeaks[b] = std::max(attackPeaks[b] * 0.995f, attacks[b]);

      const float aboveAvg = bands[b] - averages[b];
      const float delta = clampf((aboveAvg - prevAboveAvg[b]) * 2.0f, 0.0f, 1000.0f);
      prevAboveAvg[b] = aboveAvg;
      onsets[b] = delta;
    }

    // 4) publish (copy state arrays into the back buffer, flip).
    for (int i = 0; i < kBandCount; ++i) {
      out.bands[i] = bands[i];
      out.peaks[i] = peaks[i];
      out.attacks[i] = attacks[i];
      out.attackPeaks[i] = attackPeaks[i];
      out.onsets[i] = onsets[i];
    }
    out.frame = ++frameCounter;
    live.store(1 - live.load(std::memory_order_relaxed), std::memory_order_release);
  }
};

SpectrumAnalyzer::SpectrumAnalyzer() : impl_(new Impl) {}
SpectrumAnalyzer::~SpectrumAnalyzer() { delete impl_; }

void SpectrumAnalyzer::processBlock(const float* mono, int numSamples, float sampleRate) noexcept {
  if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0f) return;
  Impl* d = impl_;
  if (sampleRate != d->tableSampleRate) d->rebuildBandTable(sampleRate);

  for (int n = 0; n < numSamples; ++n) {
    d->ring[d->ringPos] = mono[n];
    d->ringPos = (d->ringPos + 1) % kFftSize;
    if (!d->primed && d->ringPos == 0) d->primed = true;
    if (++d->sinceFft >= kHop && (d->primed || d->ringPos == 0)) {
      d->sinceFft = 0;
      d->runFft();
      d->processFftUpdate(/*gainFactor=*/1.0f, /*decayFactor=*/0.9f);
    }
  }
}

SpectrumSnapshot SpectrumAnalyzer::snapshot() const noexcept {
  const int i = impl_->live.load(std::memory_order_acquire);
  return impl_->buf[i];  // value copy — caller owns it, audio thread keeps writing the other
}

// ---- Isolated proof -------------------------------------------------------------------
int runSpectrumSelfTest(bool injectBug) {
  SpectrumAnalyzer a;
  const float sr = 48000.0f;

  // Helper: feed `seconds` of a sine at `hz` (amp 1.0), return the latest snapshot.
  auto feedSine = [&](float hz, float seconds) {
    const int N = (int)(sr * seconds);
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = std::sin(2.0f * (float)M_PI * hz * i / sr);
    a.processBlock(buf.data(), N, sr);
    return a.snapshot();
  };

  // Expected band for 1000 Hz: octaveNorm = log2(1000/55)/log2(15000/55) -> band ~16.
  const float hz = 1000.0f;
  const float maxOct = std::log2(kHighestHz / kLowestHz);
  const int expectBand = (int)(std::log2(hz / kLowestHz) / maxOct * kBandCount);

  SpectrumSnapshot tone = feedSine(hz, 0.30f);

  // Find the loudest band; it should be the expected one (±1) and clearly above a far band.
  int peakBand = 0;
  for (int b = 1; b < kBandCount; ++b)
    if (tone.bands[b] > tone.bands[peakBand]) peakBand = b;
  const float farBand = tone.bands[(expectBand + 16) % kBandCount];
  const bool toneOk = std::abs(peakBand - expectBand) <= 1 && tone.bands[peakBand] > 0.5f &&
                      tone.bands[peakBand] > farBand * 4.0f + 0.05f;

  // Silence -> the energized band decays toward 0.
  SpectrumSnapshot quiet = feedSine(hz, 0.0f);  // no samples
  std::vector<float> zeros((int)(sr * 0.5f), 0.0f);
  a.processBlock(zeros.data(), (int)zeros.size(), sr);
  quiet = a.snapshot();
  const bool quietOk = quiet.bands[expectBand] < 0.1f;

  // Level step (silence -> loud) makes that band's attack fire.
  SpectrumAnalyzer a2;
  std::vector<float> z2((int)(sr * 0.2f), 0.0f);
  a2.processBlock(z2.data(), (int)z2.size(), sr);
  std::vector<float> loud((int)(sr * 0.2f));
  for (size_t i = 0; i < loud.size(); ++i) loud[i] = std::sin(2.0f * (float)M_PI * hz * i / sr);
  a2.processBlock(loud.data(), (int)loud.size(), sr);
  const float attackAtBand = a2.snapshot().attacks[expectBand];
  const bool attackOk = attackAtBand > 0.05f;

  bool ok = toneOk && quietOk && attackOk;
  if (injectBug) ok = !ok;
  std::printf("[selftest-spectrum] peakBand=%d expect=%d level=%.3f far=%.3f | quiet=%.3f | "
              "attack=%.3f -> %s\n",
              peakBand, expectBand, tone.bands[peakBand], farBand, quiet.bands[expectBand],
              attackAtBand, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
