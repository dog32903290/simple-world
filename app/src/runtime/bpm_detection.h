// runtime/bpm_detection — headless BPM auto-detection, ported LINE-FOR-LINE from TiXL
// external/tixl/Editor/Gui/Interaction/Timing/BpmDetection.cs:5-182 (pure deterministic float DSP).
//
// Algorithm: per-frame band-0..NormalizedFrequencyRangeMax energy is appended to a sliding
// sample buffer (SampleDurationInSec * 60 frames). On demand, SmoothBuffer subtracts a boxcar
// moving-average (cancels volume drift), MeasureEnergyDifference does a coarse autocorrelation
// over candidate BPM periods, ComputeFocusFactor biases toward the current lock, and an argmin
// over [BpmRangeMin, BpmRangeMax) + a refinement pass over searchOffsets picks the best BPM.
//
// runtime leaf: pure C++ arithmetic. No GPU/device/UI/frame-loop/randomness/threads. The input
// is a normalized FFT magnitude buffer (sw SpectrumSnapshot.fftNormalized[1024]); TiXL reads
// BASS's 512-bin buffer. See fork-bpm-fft-resolution below.
#pragma once
#include <vector>

namespace sw {

// fork-bpm-fft-resolution: sw kFftBins=1024 (spectrum_analyzer.h:19) vs TiXL FftResolution=512.
// The band borders are NormalizedFrequencyRange * resolution (BpmDetection.cs:92-93) — a RATIO,
// so we scale the resolution to sw's bin count. For the default 0..0.2 range the band sum just
// covers a wider slice of the SAME normalized spectrum, so the per-frame energy series — and
// therefore the recovered BPM — is identical in shape. Faithful, named.
inline constexpr int kBpmFftResolution = 1024;  // TiXL FftResolution=512; ratio-rescaled.

// Line-for-line port of T3.Editor.Gui.Interaction.Timing.BpmDetection.
class BpmDetection {
 public:
  // Tunables (BpmDetection.cs:7-12). Public, settable; defaults are TiXL's verbatim.
  float sampleDurationInSec = 25.0f;   // SampleDurationInSec
  int   bpmRangeMin = 80;              // BpmRangeMin
  int   bpmRangeMax = 180;             // BpmRangeMax
  float normalizedFrequencyRangeMin = 0.0f;   // NormalizedFrequencyRangeMin
  float normalizedFrequencyRangeMax = 0.2f;   // NormalizedFrequencyRangeMax
  float lockInFactor = 0.001f;         // LockInFactor

  // HasSufficientSampleData (BpmDetection.cs:13): true once the sliding buffer has filled.
  bool hasSufficientSampleData() const { return addedSampleCount_ >= sampleBufferSize(); }

  // AddFftSample (BpmDetection.cs:18-24): call once a frame with a fresh FFT magnitude buffer.
  // `n` is the bin count (sw passes 1024); borders are derived against kBpmFftResolution.
  void addFftSample(const float* fftBuffer, int n);

  // ComputeBpmRate (BpmDetection.cs:31-70): expensive; do NOT call every frame. Returns best BPM.
  float computeBpmRate();

  // -bug discriminator wiring (selftest only): when set, MeasureEnergyDifference detunes its
  // autocorrelation lag by a fixed +7 frames, moving the energy-difference minimum to the wrong dt
  // so the argmin recovers a WRONG bpm (golden RED). A constant lag-detune breaks the period match
  // even on a clean planted beat (unlike a symmetric index flip). Production leaves this false.
  void setInjectBug(bool on) { injectBug_ = on; }

  // Test access to the intermediate buffers (refuter anchor — re-derive from the .cs math).
  const std::vector<float>& smoothedSampleBuffer() const { return smoothedSampleBuffer_; }
  void smoothBufferForTest() { smoothBuffer(); }
  float measureEnergyDifferenceForTest(float bpm) { return measureEnergyDifference(bpm); }

 private:
  void  updateSampleBuffer(const float* fftBuffer, int n);  // BpmDetection.cs:86-112
  void  smoothBuffer();                                     // BpmDetection.cs:120-138 (boxcar subtract)
  float measureEnergyDifference(float bpm);                 // BpmDetection.cs:146-168 (autocorrelation)
  float computeFocusFactor(float value, float targetValue, float range, float amplitude);  // :77-82

  int sampleBufferSize() const;  // BpmDetection.cs:177: (int)Clamp(dur,1,60) * FramesPerSecond

  static constexpr int kFramesPerSecond = 60;  // BpmDetection.cs:172

  int   addedSampleCount_ = 0;                 // _addedSampleCount
  float currentBpm_ = 66.0f;                   // _currentBpm = 66
  // _searchOffsets = { -0.3, -0.1, 0, 0.1, 0.3 } (BpmDetection.cs:175)
  std::vector<float> searchOffsets_{-0.3f, -0.1f, 0.0f, 0.1f, 0.3f};
  std::vector<float> sampleBuffer_;            // _sampleBuffer
  std::vector<float> smoothedSampleBuffer_;    // _smoothedSampleBuffer
  std::vector<float> bpmEnergies_;             // _bpmEnergies
  bool  injectBug_ = false;                    // selftest -bug switch (production: false)
};

// Isolated proof (Rule 5): synthesize an FFT stream with a planted band-energy beat at a known
// BPM, recover it within ±1 (120 and 90, proving it's not hard-coded); re-derive SmoothBuffer +
// MeasureEnergyDifference from the .cs and match to 5 decimals; hasSufficientSampleData guards
// before the buffer fills; injectBug flips the verdict so the golden goes RED.
int runBpmDetectionSelfTest(bool injectBug);

}  // namespace sw
