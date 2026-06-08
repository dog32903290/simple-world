// runtime/audio_analyzer — block RMS/peak analyzer. Ported from my-world's proven
// AudioAnalyzerState (source/audio/AudioAnalyzerState): the audio thread calls
// processBlock() per incoming buffer (mono-mixes channels, computes RMS + peak +
// silence gate) and stores the result in atomics; any thread reads snapshot(). This
// is the samples->RMS half of World 1; attack_detector consumes the RMS half.
//
// runtime leaf: pure computation + lock-free atomics, no audio hardware, no UI. The
// platform capture layer owns one and drives it from the AVAudioEngine tap.
#pragma once
#include <atomic>
#include <cstdint>

namespace sw {

struct AudioSnapshot {
  float         rms   = 0.0f;
  float         peak  = 0.0f;
  float         gate  = 0.0f;   // 1 if peak above the noise floor, else 0
  bool          active = false; // gate > 0
  std::uint64_t sampleCounter = 0;
};

class AudioAnalyzer {
 public:
  // Called on the audio thread. inputChannels may be null / numChannels<=0 /
  // numSamples<=0 (e.g. a dropout) — that stores zeros and returns. analysisGain
  // scales the mono mix (input sensitivity). noexcept: never throw on the RT thread.
  void processBlock(const float* const* inputChannels, int numChannels, int numSamples,
                    float analysisGain) noexcept;
  // Read the latest analysis from any thread.
  AudioSnapshot snapshot() const noexcept;

 private:
  std::atomic<float>         rms_{0.0f};
  std::atomic<float>         peak_{0.0f};
  std::atomic<float>         gate_{0.0f};
  std::atomic<bool>          active_{false};
  std::atomic<std::uint64_t> sampleCounter_{0};
};

// Isolated proof (Rule 5): null->zeros; silence->rms 0 + inactive; constant 0.5 block
// ->rms 0.5/peak 0.5/active; 2-channel 0.4|0.6 mono-mixes to 0.5; sampleCounter
// accumulates only over non-empty blocks. injectBug flips the verdict.
int runAudioAnalyzerSelfTest(bool injectBug);

}  // namespace sw
