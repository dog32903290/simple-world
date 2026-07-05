// runtime/tone_synth — procedural waveform synthesis, the closed-form CORE of TiXL
// AudioToneGenerator (Operators/Lib/io/audio/AudioToneGenerator.cs). Ported VERBATIM from
// its ProceduralToneStream.GenerateSample (.cs:266-308) + the phase loop
// (StreamCallback .cs:245-263): given a running phase and a waveform selector, produce one
// mono sample in [-1, 1].
//
// WHY a primitive, not a node: AudioToneGenerator's THREE operator outputs (Command / IsPlaying
// bool / GetLevel float) are ALL driven by the BASS operator-mixer (AudioMixerManager +
// per-operator ProceduralToneStream + ChannelGetLevel readback) — hardware the Mac chain does
// not have yet (deferred-hw-verify). What IS portable + closed-form is the synthesis math, and
// that is the only part with an impl-independent oracle (hand-derivable from the .cs). So the
// waveform generator lives here as a pure, golden-able runtime leaf; the future node (when the
// AVAudioEngine operator-mixer subsystem exists) drives THIS to fill its audio callback.
//
// runtime leaf: pure computation, no audio hardware, no UI, no mixer. Deterministic for the
// deterministic waveforms (Sine/Square/Sawtooth/Triangle); the noise waveforms take an explicit
// RNG/state seam so callers own their randomness (the golden pins only the closed-form four).
#pragma once
#include <cstdint>

namespace sw {

// = AudioToneGenerator private enum WaveformTypes (.cs:288-296). Integer values are the .t3
// selector wire values (WaveformType InputSlot<int>, MappedType=WaveformTypes).
enum class ToneWaveform : int {
  Sine      = 0,
  Square    = 1,
  Sawtooth  = 2,
  Triangle  = 3,
  WhiteNoise = 4,
  PinkNoise  = 5,
};

// One deterministic waveform sample from a phase in RADIANS. Verbatim TiXL
// GenerateSample (.cs:266-286) for the four closed-form waveforms:
//   normalizedPhase = phase - floor(phase / 2π) * 2π          (.cs:268)
//   t               = normalizedPhase / 2π                    (.cs:269)
//   Sine     -> sin(normalizedPhase)                          (.cs:273-274)
//   Square   -> t < 0.5 ?  0.8 : -0.8                         (.cs:275-276)
//   Sawtooth -> (2t - 1) * 0.8                                (.cs:277-278)
//   Triangle -> (4*|t - 0.5| - 1) * 0.8                       (.cs:279-280)
// WhiteNoise/PinkNoise are NOT closed-form (RNG state) — passing them here returns 0 (use
// the stateful noise members on a caller-owned generator instead; not needed for the golden).
float toneWaveformSample(double phaseRadians, ToneWaveform waveform) noexcept;

// The per-sample phase advance for a given frequency at a sample rate (.cs:255):
//   phaseIncrement = 2π * freq / sampleRate
// Frequency is clamped to [20, 20000] Hz exactly as the ProceduralToneStream.Frequency setter
// (.cs:329-333) — the operator's own guard, not an invented bound.
double tonePhaseIncrement(double frequencyHz, int sampleRateHz) noexcept;

// Frequency clamp mirror (.cs:331: Max(20, Min(20000, value))). Exposed so the golden can pin
// the guard independently of the increment.
double toneClampFrequency(double frequencyHz) noexcept;

// Teeth-only injection seam (GOLDEN_STANDARD 特徵 3): toneWaveformSample has no cook flag or
// shared-state seam to sever (it is a pure math leaf), so the golden corrupts the REAL synthesis
// output through this flag — when set, toneWaveformSample scales its result by 0.5 (a WRONG gain
// no TiXL waveform emits), diverging every closed-form assertion. Production never sets it; the
// --bite -bug pass sets it, runs the SAME hand-derived wants, and the teeth bite. This is a REAL
// corruption of the function callers use (mirror of gradient_golden's corrupt()), not a want-flip.
bool& toneSynthInjectBug();

}  // namespace sw
