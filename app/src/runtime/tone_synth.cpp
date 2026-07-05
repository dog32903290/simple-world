// runtime/tone_synth — see tone_synth.h. VERBATIM port of TiXL AudioToneGenerator's
// ProceduralToneStream synthesis math (Operators/Lib/io/audio/AudioToneGenerator.cs).
#include "runtime/tone_synth.h"

#include <algorithm>
#include <cmath>

namespace sw {

namespace {
constexpr double kTwoPi = 2.0 * M_PI;
}  // namespace

bool& toneSynthInjectBug() {
  static bool bug = false;
  return bug;
}

float toneWaveformSample(double phaseRadians, ToneWaveform waveform) noexcept {
  // .cs:268 — wrap phase into [0, 2π). floor(phase/2π)*2π is the C# Math.Floor path (works for
  // negative phase too, unlike fmod).
  const double normalizedPhase =
      phaseRadians - std::floor(phaseRadians / kTwoPi) * kTwoPi;
  // .cs:269 — normalized position within the cycle, t in [0, 1).
  const double t = normalizedPhase / kTwoPi;

  // Teeth-only corruption (see toneSynthInjectBug in the header): scale the REAL output by a
  // wrong gain so every closed-form assertion diverges. Not applied to noise (returns 0 anyway).
  const float bugGain = toneSynthInjectBug() ? 0.5f : 1.0f;

  switch (waveform) {
    case ToneWaveform::Sine:  // .cs:273-274
      return static_cast<float>(std::sin(normalizedPhase)) * bugGain;
    case ToneWaveform::Square:  // .cs:275-276
      return (t < 0.5 ? 0.8f : -0.8f) * bugGain;
    case ToneWaveform::Sawtooth:  // .cs:277-278
      return static_cast<float>(2.0 * t - 1.0) * 0.8f * bugGain;
    case ToneWaveform::Triangle:  // .cs:279-280
      return static_cast<float>(4.0 * std::abs(t - 0.5) - 1.0) * 0.8f * bugGain;
    case ToneWaveform::WhiteNoise:  // .cs:281-282 — RNG state, not closed-form
    case ToneWaveform::PinkNoise:   // .cs:283-294 — RNG + IIR state, not closed-form
    default:
      return 0.0f;
  }
}

double toneClampFrequency(double frequencyHz) noexcept {
  // .cs:331 — Math.Max(20f, Math.Min(20000f, value)).
  return std::max(20.0, std::min(20000.0, frequencyHz));
}

double tonePhaseIncrement(double frequencyHz, int sampleRateHz) noexcept {
  if (sampleRateHz <= 0) return 0.0;
  // .cs:255 — 2π * freq / sampleRate. Frequency is clamped by the ProceduralToneStream.Frequency
  // setter (.cs:329-333) before it reaches the callback, so clamp here to match the pipeline.
  return kTwoPi * toneClampFrequency(frequencyHz) / static_cast<double>(sampleRateHz);
}

}  // namespace sw
