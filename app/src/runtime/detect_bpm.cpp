// runtime/detect_bpm — implementation + isolated node-level golden. Ported VERBATIM from the OLDER
// TiXL operator external/tixl/Operators/Lib/io/audio/_/DetectBpm.cs:22-183 (its OWN constants —
// distinct from the editor BpmDetection bpm_detection.cpp ports). See header for the algorithm.
#include "runtime/detect_bpm.h"

#include <cmath>
#include <cstdio>

#include "runtime/anim_math.h"            // sw::anim_math::clampf (anim_math.h:89) — reused, not reimplemented
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / resolveResidentFloatInputs

namespace sw {

// DetectBpm.cs:113-137 — UpdateBuffer(fftBuffer, lowerBorder, upperBorder). lower/upper are INTEGER
// FFT bin borders (the operator's contract; NOT a normalized range). _buffer is pre-sized to
// _bufferLength then slid by Add+RemoveAt(0).
void DetectBpm::updateBuffer(const float* fft, int n, int lowerBorder, int upperBorder) {
  if ((int)buffer_.size() != bufferLength_)
    buffer_.assign(bufferLength_, 0.0f);

  if (upperBorder <= lowerBorder)
    return;
  if (lowerBorder < 0)
    lowerBorder = 0;
  if (upperBorder > n)
    upperBorder = n;

  float sum = 0.0f;
  for (int index = lowerBorder; index < upperBorder; index++)
    sum += fft[index];

  buffer_.push_back(sum);
  if ((int)buffer_.size() > bufferLength_)
    buffer_.erase(buffer_.begin());
}

// DetectBpm.cs:139-159 — SmoothBuffer(ref output, buffer): subtract a boxcar moving average. NOTE
// output is RESIZED to buffer.Count (so _smoothedBuffer's effective length == _bufferLength, not the
// 60*60 it was initialised at). NOTE the .cs asymmetry: j runs [-5,4] (10 terms) but the divisor is
// smoothSteps*2+1 = 11. Ported VERBATIM (DetectBpm.cs:148-157).
void DetectBpm::smoothBuffer() {
  const int count = (int)buffer_.size();
  if ((int)smoothedBuffer_.size() != count)
    smoothedBuffer_.assign(count, 0.0f);

  const int smoothSteps = 5;
  if (count < smoothSteps * 2 + 1)
    return;
  for (int i = smoothSteps; i < count - smoothSteps; i++) {
    float sum = 0.0f;
    for (int j = -smoothSteps; j < smoothSteps; j++)
      sum += buffer_[i + j];
    smoothedBuffer_[i] = std::fmax(0.0f, buffer_[i] - sum / (smoothSteps * 2 + 1));
  }
}

// DetectBpm.cs:163-183 — MeasureEnergyDifference(bpm): coarse autocorrelation over _smoothedBuffer.
float DetectBpm::measureEnergyDifference(float bpm) const {
  const float dt = (240.0f / bpm * 60.0f / 4.0f);
  float sum = 0.1f;

  const int slideScans = 4;
  const int clipStart = (int)(240.0f / 80.0f * 60.0f / 4.0f) * slideScans + 1;  // = 181

  const int len = (int)smoothedBuffer_.size();
  for (int j = 1; j < slideScans; j++) {
    // -bug: detune the autocorrelation lag by a fixed +7 frames. The lag at which a beat aligns with
    // its time-shifted copy encodes the period (= BPM); a constant lag shift moves the energy-diff
    // minimum to a DIFFERENT dt, so the argmin recovers a WRONG bpm (golden RED). Production: verbatim
    // (int)(dt*j). (Same discriminator idiom as bpm_detection.cpp's port.)
    const int offset = injectBug_ ? (int)(dt * j) + 7 : (int)(dt * j);
    int startIndex = clipStart > offset ? clipStart : offset;  // Math.Max(clipStart, (int)(dt*j))
    if (startIndex >= len)
      continue;
    for (int i = startIndex; i < len; i++)
      sum += std::fabs(smoothedBuffer_[i] - smoothedBuffer_[i - offset]);
  }
  return sum;
}

// DetectBpm.cs:96-101 — ComputeFocusFactor: fall-off curve biasing toward the current lock.
float DetectBpm::computeFocusFactor(float value, float targetValue, float range, float amplitude) {
  const float deviance = std::fabs(value - targetValue);
  const float bump =
      std::fmax(0.0f, 1.0f - (1.0f / (range * range) * deviance * deviance)) * amplitude + 1.0f;
  return std::fmax(bump, 1.0f);
}

// DetectBpm.cs:22-94 — one Update(): clamp the ranges, slide the buffer, smooth, then scan the full
// bpmRange + the searchOffsets refinement for the energy-difference argmin. Returns DetectedBpm.
float DetectBpm::computeBpmRate(const float* fft, int n, const DetectBpmParams& p) {
  // _bpmRangeMin = LowestBpm.Clamp(50,200) (DetectBpm.cs:30-34).
  bpmRangeMin_ = (int)anim_math::clampf((float)p.lowestBpm, 50.0f, 200.0f);
  // _bpmRangeMax: if < min -> min+1; else clamp upper 200 (DetectBpm.cs:36-40).
  bpmRangeMax_ = p.highestBpm;
  if (bpmRangeMax_ < bpmRangeMin_)
    bpmRangeMax_ = bpmRangeMin_ + 1;
  else if (bpmRangeMax_ > 200)
    bpmRangeMax_ = 200;

  const int bpmStepCount = bpmRangeMax_ - bpmRangeMin_;
  if ((int)bpmEnergies_.size() != bpmStepCount)
    bpmEnergies_.assign(bpmStepCount, 0.0f);

  // bufferDuration = (int)BufferDurationSec * 60, clamped [60, 60*60] (DetectBpm.cs:45-56).
  int bufferDuration = (int)p.bufferDurationSec * 60;
  if (bufferDuration < 60)
    bufferDuration = 60;
  else if (bufferDuration > 60 * 60)
    bufferDuration = 60 * 60;
  bufferLength_ = bufferDuration;
  lockInFactor_ = p.lockInFactor;

  updateBuffer(fft, n, p.lowerLimit, p.upperLimit);  // INTEGER borders (DetectBpm.cs:59)
  smoothBuffer();                                    // DetectBpm.cs:60

  float bestBpm = 0.0f;
  float bestMeasurement = INFINITY;  // float.PositiveInfinity

  for (int bpm = bpmRangeMin_; bpm < bpmRangeMax_; bpm++) {
    const float m =
        measureEnergyDifference((float)bpm) / computeFocusFactor((float)bpm, currentBpm_, 4.0f, lockInFactor_);
    if (m < bestMeasurement) {
      bestMeasurement = m;
      bestBpm = (float)bpm;
    }
    bpmEnergies_[bpm - bpmRangeMin_] = m;
  }

  // searchOffsets refinement (DetectBpm.cs:78-89). NOTE the hard 70..160 range here is NOT the
  // configurable bpmRangeMin/Max — it's the operator's own fixed clamp (engine uses 70..170; this
  // OLDER operator uses 70..160). Ported verbatim.
  for (float offset : searchOffsets_) {
    const float bpm = currentBpm_ + offset;
    if (bpm < 70.0f || bpm > 160.0f)
      continue;
    const float m = measureEnergyDifference(bpm) / computeFocusFactor(bpm, currentBpm_, 2.0f, 0.01f);
    if (!(m < bestMeasurement))
      continue;
    bestMeasurement = m;
    bestBpm = bpm;
  }

  currentBpm_ = bestBpm;  // DetectBpm.cs:92 — _currentBpm = bestBpm
  return bestBpm;
}

const std::vector<float>& DetectBpm::smoothedBufferForTest() {
  smoothBuffer();
  return smoothedBuffer_;
}
float DetectBpm::measureEnergyDifferenceForTest(float bpm) const { return measureEnergyDifference(bpm); }

// Cook every DetectBpm instance (TiXL parity) — the DetectBpm sibling of cookAudioReactionNodes.
// Resolve each node's Float inputs through the resident drivers (so a wired LowerLimit walks its
// source), accumulate one energy sample from the live FFT with the INTEGER borders, write the
// recovered BPM onto extOut[0]. State keys off the resident PATH (rides rebuilds, per-instance).
void cookDetectBpmNodes(ResidentEvalGraph& g, const float* fft, int n,
                        std::map<std::string, DetectBpm>& state) {
  ResidentEvalCtx rctx;  // DetectBpm params are constants/wires, no clock domain (no time-reading input)
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "DetectBpm") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    DetectBpmParams p;
    p.lowerLimit = (int)(P["LowerLimit"] + 0.5f);
    p.upperLimit = (int)(P["UpperLimit"] + 0.5f);
    p.bufferDurationSec = P["BufferDurationSec"];
    p.lowestBpm = (int)(P["LowestBpm"] + 0.5f);
    p.highestBpm = (int)(P["HighestBpm"] + 0.5f);
    p.lockInFactor = P["LockItFactor"];
    rn.extOut[0] = state[rn.path].computeBpmRate(fft, n, p);
  }
}

// ============================ Isolated node-level golden (Rule 5) ============================
//
// Drive a DetectBpm NODE through ITS cook path: a hand-built resident graph holding one DetectBpm
// node, fed a synthetic planted-BPM spike train (energy 1.0 packed into the integer-border band
// every `period` frames) over N frames; recover the planted BPM (±1) via extOut[0] AND
// evalResidentFloat. Two targets (120, 90) rule out hard-coding. Intermediate anchor re-derives
// DetectBpm.cs MeasureEnergyDifference against the SAME smoothed buffer to ~5 decimals. -bug detunes
// MeasureEnergyDifference's lag -> recovered BPM wrong -> ±1 assertion FAILS (RED).
namespace {

// Period (in frames) of a beat at `bpm`, matching dt = 240/bpm*60/4 — the lag the autocorrelation
// scans. Planting the spike every `dt` frames makes offset=dt see aligned copies.
int beatPeriodFrames(float bpm) { return (int)std::lround(240.0f / bpm * 60.0f / 4.0f); }

// Build an FFT frame: energy `height` packed into the integer border band [lower, upper), ~0 else.
void makeFftFrame(std::vector<float>& fft, int bins, int lower, int upper, float height) {
  fft.assign(bins, 0.0f);
  const int span = upper - lower;
  if (span <= 0) return;
  const float per = height / (float)span;  // band-sum over [lower,upper) == height
  for (int i = lower; i < upper; i++) fft[i] = per;
}

// A one-node resident graph carrying a DetectBpm op with the given integer borders + scan range as
// Constant drivers (the production cook resolves these via resolveResidentFloatInputs). lowestBpm/
// highestBpm are the operator's LowestBpm/HighestBpm INPUTS — the golden drives them to bracket the
// planted targets (exactly as the engine golden set bpmRangeMin/Max), since the .t3 default
// LowestBpm=120 floors the scan above a 90 target. This is a TEST-INPUT choice, not a fork: the
// operator faithfully reads LowestBpm/HighestBpm as params.
ResidentEvalGraph makeDetectBpmGraph(int lower, int upper, int lowestBpm, int highestBpm) {
  ResidentEvalGraph g;
  ResidentNode rn;
  rn.path = "1";
  rn.opType = "DetectBpm";
  auto addConst = [&](const char* slot, float v) {
    ResidentInput ri;
    ri.slotId = slot;
    ri.driver = ResidentInput::Driver::Constant;
    ri.constant = v;
    rn.inputs.push_back(ri);
  };
  addConst("LowerLimit", (float)lower);
  addConst("UpperLimit", (float)upper);
  addConst("BufferDurationSec", 15.0f);  // .t3 default
  addConst("LowestBpm", (float)lowestBpm);
  addConst("HighestBpm", (float)highestBpm);
  addConst("LockItFactor", 0.0f);  // .t3 default
  g.nodes.push_back(rn);
  g.byPath[rn.path] = 0;  // the node()/evalResidentFloat lookup index (flatten populates this in prod)
  return g;
}

// Drive the NODE through cookDetectBpmNodes for `frames`, return extOut[0] (the recovered BPM).
// Also cross-checks evalResidentFloat returns the same extOut[0] for the DetectedBpm output.
float recoverBpmViaNode(float bpmTarget, int lower, int upper, int lowestBpm, int highestBpm,
                        int frames, bool injectBug, bool& evalMatches) {
  ResidentEvalGraph g = makeDetectBpmGraph(lower, upper, lowestBpm, highestBpm);
  std::map<std::string, DetectBpm> state;
  const int bins = 1024;
  std::vector<float> spike, quiet;
  makeFftFrame(spike, bins, lower, upper, 1.0f);
  makeFftFrame(quiet, bins, lower, upper, 0.0f);
  const int period = beatPeriodFrames(bpmTarget);
  for (int f = 0; f < frames; f++) {
    state["1"].setInjectBug(injectBug);  // re-arm (cook reads the per-path state's bug flag)
    const std::vector<float>& frame = ((f % period) == 0) ? spike : quiet;
    cookDetectBpmNodes(g, frame.data(), bins, state);
  }
  const float viaExtOut = g.nodes[0].extOut[0];
  ResidentEvalCtx ctx;
  const float viaEval = evalResidentFloat(g, "1", "DetectedBpm", ctx);
  evalMatches = std::fabs(viaEval - viaExtOut) < 1e-4f;
  return viaExtOut;
}

}  // namespace

int runDetectBpmSelfTest(bool injectBug) {
  bool ok = true;
  const int lower = 2, upper = 199;  // DetectBpm.t3 defaults (integer bin borders)

  // ---- (1) Node-level recovery via the cook path: 90 and 120, ±1, not hard-coded ----
  // Scan range [80,180) brackets both targets (the .t3 LowestBpm=120 default floors the scan above a
  // 90 target, so the golden drives LowestBpm=80/HighestBpm=180 as TEST INPUTS — the engine golden did
  // the same; not a fork, the operator reads them as params). Both have an INTEGER beat period
  // (3600/90=40, 3600/120=30) so the planted spike lands exactly on the dt the autocorrelation scans,
  // and both are away from the currentBpm=122 seed so convergence is genuine. NOTE: DetectBpm.cs
  // truncates each candidate's dt to int (DetectBpm.cs:173 (int)(dt*j)), so a target and its lower
  // neighbour share the same integer lags → the argmin (first-of-equal, strict <) biases to the lower
  // neighbour; ±1 absorbs that faithful quantization. 15s buffer default -> 900 frames fully fill the
  // slide window.
  const int frames = 900;
  const int loBpm = 80, hiBpm = 180;
  bool evalA = false, evalB = false;
  const float r90 = recoverBpmViaNode(90.0f, lower, upper, loBpm, hiBpm, frames, injectBug, evalA);
  const float r120 = recoverBpmViaNode(120.0f, lower, upper, loBpm, hiBpm, frames, injectBug, evalB);
  std::printf("[detectbpm] node-recovered (via extOut[0]): target 90 -> %.3f, target 120 -> %.3f%s\n",
              r90, r120, injectBug ? "  (injectBug)" : "");
  if (std::fabs(r90 - 90.0f) > 1.0f) {
    std::printf("[detectbpm] FAIL: 90 target recovered %.3f (>1 off)\n", r90);
    ok = false;
  }
  if (std::fabs(r120 - 120.0f) > 1.0f) {
    std::printf("[detectbpm] FAIL: 120 target recovered %.3f (>1 off)\n", r120);
    ok = false;
  }
  // evalResidentFloat must agree with extOut[0] (the no-evaluate readback path). Always clean.
  if (!evalA || !evalB) {
    std::printf("[detectbpm] FAIL: evalResidentFloat(DetectedBpm) != extOut[0] (evalA=%d evalB=%d)\n",
                evalA, evalB);
    ok = false;
  }

  // ---- (2) Intermediate-buffer equivalence: hand-derive SmoothBuffer + MeasureEnergyDifference
  //          FROM DetectBpm.cs against the SAME smoothed buffer, match to ~5 decimals. The refuter
  //          anchor — source of truth is the .cs, not self-consistency. Always clean (the bug flows
  //          only through (1)'s recovery via injectBug). Use a 5s buffer (300 frames) so the
  //          autocorrelation inner loop actually fires (300 > clipStart 181). ----
  {
    DetectBpm e;
    DetectBpmParams p;
    p.lowerLimit = lower;
    p.upperLimit = upper;
    p.bufferDurationSec = 5.0f;  // bufferLength = (int)5*60 = 300
    const int bins = 1024;
    std::vector<float> fft;
    // band-sum per frame seq[f] = f % 7 (exactly representable; structure for the autocorrelation).
    for (int f = 0; f < 300; f++) {
      makeFftFrame(fft, bins, lower, upper, (float)(f % 7));
      e.computeBpmRate(fft.data(), bins, p);  // accumulate one frame (also runs the scan; we re-probe below)
    }
    const std::vector<float>& sm = e.smoothedBufferForTest();

    // Hand-derive SmoothBuffer (DetectBpm.cs:148-157): cleaned[i] = max(0, s[i] - (sum_{j=-5}^{4}
    // s[i+j])/11) with s[k] = k%7. The buffer is pre-sized 300 then slid 300 times: after 300 Adds
    // + 300 RemoveAt(0), buffer[k] holds the band-sum from frame (k) of the LAST 300 frames — i.e.
    // buffer[k] == k%7 for the 0..299 fill (the initial zeros all slid out). So s[k] = k%7.
    auto handSmooth = [](int i) {
      float sum = 0.0f;
      for (int j = -5; j < 5; j++) sum += (float)((i + j) % 7);
      const float v = (float)(i % 7) - sum / 11.0f;
      return v > 0.0f ? v : 0.0f;
    };
    for (int i : {10, 137, 280}) {
      const float hand = handSmooth(i);
      if (std::fabs(sm[i] - hand) > 1e-4f) {
        std::printf("[detectbpm] FAIL: SmoothBuffer[%d] sw=%.6f hand=%.6f\n", i, sm[i], hand);
        ok = false;
      }
    }

    // Hand-derive MeasureEnergyDifference (DetectBpm.cs:165-182) over the SAME smoothed buffer:
    //   dt = 240/bpm*60/4 ; clipStart = (int)(240/80*60/4)*4+1 = 181 ; sum starts 0.1 ;
    //   for j in [1,3]: offset=(int)(dt*j); start=max(181,offset); accumulate abs(sm[i]-sm[i-offset]).
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
      if (std::fabs(swv - hand) > 1e-3f * std::fabs(hand) + 1e-5f) {
        std::printf("[detectbpm] FAIL: MeasureEnergyDifference(%.0f) sw=%.6f hand=%.6f\n", bpm, swv, hand);
        ok = false;
      } else if (!injectBug) {
        std::printf("[detectbpm] intermediate match: MeasureEnergyDifference(%.0f) sw=%.6f hand=%.6f\n",
                    bpm, swv, hand);
      }
    }
  }

  if (injectBug) ok = !ok;  // -bug: (1)'s detune must have broken recovery, flipping the verdict
  if (ok) std::printf("[detectbpm] PASS%s\n", injectBug ? " (injectBug expected-FAIL inverted)" : "");
  return ok ? 0 : 1;
}

}  // namespace sw
