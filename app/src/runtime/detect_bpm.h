// runtime/detect_bpm — the stateful cook for the DetectBpm NODE, ported faithfully from the
// OLDER TiXL operator external/tixl/Operators/Lib/io/audio/_/DetectBpm.cs (NOT the newer editor
// Interaction.Timing.BpmDetection class that bpm_detection.{h,cpp} ports — different algorithm,
// different constants, different border contract). Given a 1024-bin FFT frame + the node's params
// + a per-node state block, it accumulates one energy sample/frame and produces the DetectedBpm
// output. Kept out of the generic stateless value-node evaluate() because it needs (a) the whole
// spectrum (too big for the GPU-shared EvaluationContext) and (b) per-node memory across frames —
// the AudioReaction precedent (runtime/audio_reaction.h). main cooks each DetectBpm node once per
// frame and writes DetectedBpm onto the resident node's extOut[0]; evalResidentFloat reads it back.
//
// runtime leaf: pure computation, no hardware, no UI.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace sw {

struct ResidentEvalGraph;  // resident_eval_graph.h (cooked over by cookDetectBpmNodes)

// The DetectBpm operator's per-frame inputs (TiXL InputSlots), gathered from Node::params.
// DetectBpm.t3 defaults in the comments. LowerLimit/UpperLimit are INTEGER BIN INDICES into the
// FFT frame (NOT a normalized 0..1 range — that is the editor BpmDetection's contract, not this
// operator's): UpdateBuffer(fft, lowerBorder, upperBorder) sums fft[lower..upper).
struct DetectBpmParams {
  int   lowerLimit = 2;        // LowerLimit  (.t3 default 2)   — integer FFT bin border (inclusive)
  int   upperLimit = 199;      // UpperLimit  (.t3 default 199) — integer FFT bin border (exclusive)
  float bufferDurationSec = 15.0f;  // BufferDurationSec (.t3 15) — *60 frames, clamped [60, 60*60]
  int   lowestBpm = 120;       // LowestBpm   (.t3 default 120) — clamped [50, 200]
  int   highestBpm = 180;      // HighestBpm  (.t3 default 180) — clamped [min, 200]
  float lockInFactor = 0.0f;   // LockItFactor (.t3 default 0)
};

// Per-node memory across frames (TiXL DetectBpm private fields). One per DetectBpm instance,
// keyed by resident path (survives projection rebuilds + stays per-instance inside compounds).
// Ported VERBATIM from DetectBpm.cs — its OWN constants:
//   _searchOffsets = {-0.5,-0.1,0,0.1,0.5}   (refinement offsets, DetectBpm.cs:111)
//   _currentBpm seed = 122                   (DetectBpm.cs:110)
//   refinement hard-range 70..160            (DetectBpm.cs:81)
//   _smoothedBuffer initial new float[60*60] (DetectBpm.cs:161 — RESIZED to _buffer.Count by
//                                             SmoothBuffer, so its effective length == _bufferLength)
struct DetectBpm {
  // ---- DetectBpm.cs:Update / ComputeBpmRate ----
  // Push one FFT frame's band-sum into the sliding buffer (UpdateBuffer) with the integer borders,
  // then return the best BPM (the full bpmRange + searchOffsets argmin scan over the smoothed
  // autocorrelation). One call per frame = one DetectBpm.Update().
  float computeBpmRate(const float* fft, int n, const DetectBpmParams& p);

  // -bug discriminator (selftest only): when set, MeasureEnergyDifference detunes its autocorrelation
  // lag by a constant — the energy-difference argmin lands on a WRONG dt → wrong recovered BPM (RED).
  void setInjectBug(bool b) { injectBug_ = b; }

  // Test peek (golden intermediate anchor — re-derive MeasureEnergyDifference against the SAME
  // smoothed buffer the .cs autocorrelates over). Calls SmoothBuffer first (as ComputeBpmRate does).
  const std::vector<float>& smoothedBufferForTest();
  float measureEnergyDifferenceForTest(float bpm) const;

 private:
  void  updateBuffer(const float* fft, int n, int lowerBorder, int upperBorder);
  void  smoothBuffer();
  float measureEnergyDifference(float bpm) const;
  static float computeFocusFactor(float value, float targetValue, float range, float amplitude);

  // DetectBpm.cs private fields (its own seeds).
  int   bpmRangeMin_ = 65;
  int   bpmRangeMax_ = 150;
  int   bufferLength_ = 0;
  float currentBpm_ = 122.0f;                                        // DetectBpm.cs:110
  float lockInFactor_ = 0.0f;
  const float searchOffsets_[5] = {-0.5f, -0.1f, 0.0f, 0.1f, 0.5f};  // DetectBpm.cs:111
  std::vector<float> buffer_;          // _buffer (sliding, pre-sized to _bufferLength)
  std::vector<float> smoothedBuffer_;  // _smoothedBuffer (resized to _buffer.Count by SmoothBuffer)
  std::vector<float> bpmEnergies_;     // _bpmEnergies (sized to max-min)
  bool  injectBug_ = false;
};

// Cook every DetectBpm instance in `g` for this frame (TiXL parity): resolve its params through the
// resident drivers (the AudioReaction precedent), accumulate one energy sample from `fft` with the
// integer borders, write computeBpmRate() onto the resident node's extOut[0]. State keys off the
// resident PATH (the `state` map, threaded from frame_cook), so it survives projection rebuilds AND
// stays per-instance inside compounds — mirror of cookAudioReactionNodes. `fft`/`n` is the live FFT
// magnitude frame (audio_monitor::spectrum().fftGain — the RawFft bins DetectBpm sums over).
void cookDetectBpmNodes(ResidentEvalGraph& g, const float* fft, int n,
                        std::map<std::string, DetectBpm>& state);

// Isolated proof (Rule 5): drive a DetectBpm NODE through its cook path (a hand-built resident
// graph + integer borders) with a synthetic planted-BPM spike train, recover the planted BPM (±1)
// for 2 targets (rules out hard-coding) via extOut[0] AND evalResidentFloat; an intermediate anchor
// re-derives DetectBpm.cs MeasureEnergyDifference. injectBug flips the verdict so the test FAILs.
int runDetectBpmSelfTest(bool injectBug);

}  // namespace sw
