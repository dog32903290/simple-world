// runtime/spectrum_analyzer — per-band spectral analysis, ported faithfully from TiXL
// Core/Audio/AudioAnalysisContext.cs (the data that feeds Operators/.../AudioReaction.cs).
//
// Pipeline: mono PCM -> 2048-pt real FFT (1024 magnitude bins, via Apple Accelerate/vDSP)
// -> dB-normalized -> log-octave map (55 Hz..15 kHz) into 32 frequency bands (max-pool) ->
// per-band peaks (decay), attacks (rate-of-increase ×4), attack-peaks, onsets (above a
// sliding average). TiXL gets the raw FFT from BASS; we compute it ourselves with vDSP.
//
// runtime leaf: pure computation + vDSP, no audio hardware, no UI. The platform capture
// layer owns one and drives processBlock() from the AUHAL tap (audio thread); any thread
// reads snapshot(). Mirrors audio_analyzer's contract, one level richer (bands, not 1 RMS).
#pragma once
#include <array>
#include <cstdint>

namespace sw {

inline constexpr int kFftSize  = 2048;            // FFT window (TiXL DataFlags.FFT2048)
inline constexpr int kFftBins  = kFftSize / 2;    // 1024 magnitude bins
inline constexpr int kBandCount = 32;             // TiXL AudioConfig.FrequencyBandCount

// One frame of analysis. Arrays are 0..1 normalized (bands/peaks) or rate values
// (attacks/onsets, clamped). Read via SpectrumAnalyzer::snapshot().
struct SpectrumSnapshot {
  std::array<float, kBandCount> bands{};         // current per-band level (FrequencyBands)
  std::array<float, kBandCount> peaks{};         // peak-hold w/ decay (FrequencyBandPeaks)
  std::array<float, kBandCount> attacks{};       // rate of increase (FrequencyBandAttacks)
  std::array<float, kBandCount> attackPeaks{};   // slow-decay attack peaks
  std::array<float, kBandCount> onsets{};        // above sliding-average (FrequencyBandOnSets)
  std::array<float, kFftBins>   fftGain{};       // raw FFT magnitude     (RawFft mode)
  std::array<float, kFftBins>   fftNormalized{}; // dB-normalized 0..1     (NormalizedFft mode)
  std::uint64_t frame = 0;                       // increments each FFT update
};

class SpectrumAnalyzer {
 public:
  SpectrumAnalyzer();
  ~SpectrumAnalyzer();
  SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
  SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;

  // Audio thread. Accumulate mono samples; each time a full 2048-window fills, run the FFT
  // + band/peak/attack/onset update and publish a new snapshot. sampleRate builds the
  // bin->band table (rebuilt only when it changes). null / numSamples<=0 is a safe no-op.
  // noexcept: never throws on the RT thread.
  void processBlock(const float* mono, int numSamples, float sampleRate) noexcept;

  // Any thread: the latest published snapshot (lock-free double-buffer copy).
  SpectrumSnapshot snapshot() const noexcept;

 private:
  struct Impl;
  Impl* impl_;
};

// Isolated proof (Rule 5): a pure sine lands in its log-octave band (neighbours far lower);
// silence -> all bands ~0; a level step -> that band's attack fires. injectBug flips the
// verdict so the test must FAIL.
int runSpectrumSelfTest(bool injectBug);

}  // namespace sw
