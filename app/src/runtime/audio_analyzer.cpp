#include "runtime/audio_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sw {

void AudioAnalyzer::processBlock(const float* const* inputChannels, int numChannels,
                                 int numSamples, float analysisGain) noexcept {
  if (inputChannels == nullptr || numChannels <= 0 || numSamples <= 0) {
    rms_.store(0.0f, std::memory_order_relaxed);
    peak_.store(0.0f, std::memory_order_relaxed);
    gate_.store(0.0f, std::memory_order_relaxed);
    active_.store(false, std::memory_order_relaxed);
    return;  // dropout: zero the snapshot, don't touch the sample counter
  }

  double sumSquares = 0.0;
  float  blockPeak = 0.0f;
  for (int sample = 0; sample < numSamples; ++sample) {
    float mono = 0.0f;
    int   used = 0;
    for (int ch = 0; ch < numChannels; ++ch) {
      if (inputChannels[ch] != nullptr) {
        mono += inputChannels[ch][sample];
        ++used;
      }
    }
    if (used > 0) mono = (mono / (float)used) * analysisGain;
    sumSquares += (double)mono * (double)mono;
    blockPeak = std::max(blockPeak, std::abs(mono));
  }

  const float blockRms = (float)std::sqrt(sumSquares / (double)numSamples);
  const float blockGate = blockPeak > 0.0001f ? 1.0f : 0.0f;
  rms_.store(blockRms, std::memory_order_relaxed);
  peak_.store(blockPeak, std::memory_order_relaxed);
  gate_.store(blockGate, std::memory_order_relaxed);
  active_.store(blockGate > 0.0f, std::memory_order_relaxed);
  sampleCounter_.fetch_add((std::uint64_t)numSamples, std::memory_order_relaxed);
}

AudioSnapshot AudioAnalyzer::snapshot() const noexcept {
  AudioSnapshot s;
  s.rms = rms_.load(std::memory_order_relaxed);
  s.peak = peak_.load(std::memory_order_relaxed);
  s.gate = gate_.load(std::memory_order_relaxed);
  s.active = active_.load(std::memory_order_relaxed);
  s.sampleCounter = sampleCounter_.load(std::memory_order_relaxed);
  return s;
}

int runAudioAnalyzerSelfTest(bool injectBug) {
  AudioAnalyzer a;
  bool ok = true;
  const int N = 256;

  // 1. null input -> zeros, inactive, no counter advance.
  a.processBlock(nullptr, 0, 0, 1.0f);
  { auto s = a.snapshot(); ok = ok && (s.rms == 0.0f) && (!s.active) && (s.sampleCounter == 0); }

  // 2. silence block -> rms/peak 0, inactive.
  float zero[N]; for (int i = 0; i < N; ++i) zero[i] = 0.0f;
  const float* chZero[1] = {zero};
  a.processBlock(chZero, 1, N, 1.0f);
  { auto s = a.snapshot();
    ok = ok && (std::fabs(s.rms) < 1e-4f) && (std::fabs(s.peak) < 1e-4f) && (!s.active); }

  // 3. constant 0.5 block -> rms 0.5, peak 0.5, active.
  float half[N]; for (int i = 0; i < N; ++i) half[i] = 0.5f;
  const float* chHalf[1] = {half};
  a.processBlock(chHalf, 1, N, 1.0f);
  { auto s = a.snapshot();
    ok = ok && (std::fabs(s.rms - 0.5f) < 1e-4f) && (std::fabs(s.peak - 0.5f) < 1e-4f) && s.active; }

  // 4. 2 channels (0.4 | 0.6) mono-mix to 0.5.
  float a4[N], b6[N]; for (int i = 0; i < N; ++i) { a4[i] = 0.4f; b6[i] = 0.6f; }
  const float* ch2[2] = {a4, b6};
  a.processBlock(ch2, 2, N, 1.0f);
  { auto s = a.snapshot(); ok = ok && (std::fabs(s.rms - 0.5f) < 1e-4f); }

  // 5. sampleCounter accumulated only the 3 non-empty blocks (null block added none).
  { auto s = a.snapshot(); ok = ok && (s.sampleCounter == (std::uint64_t)(3 * N)); }

  if (injectBug) ok = !ok;
  printf("[selftest-analyzer] null/silence/const/2ch-mix/counter -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
