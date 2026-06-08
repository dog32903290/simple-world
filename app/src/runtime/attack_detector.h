// runtime/attack_detector — wide-band transient (kick/attack) onset detector.
// Ported from my-world's proven AttackDetector (source/audio/AnalyzerAttackDetector),
// which was fixture-tested against fixtures/analyzer/attack_detector_cases.json. Pure
// computation: consumes a per-frame RMS reading (computed upstream by the capture/
// analyzer layer) and reports a debounced onset event + a decaying attack envelope.
//
// Why this shape (the borrow): onset = a RISE in RMS (delta-based), gated by a silence
// floor, with TIME-based debounce + release (frame-rate independent), plus NaN/clip
// robustness. Pitch/spectral onsets (need an FFT) are out of scope (L3: 大鼓好打).
//
// runtime leaf: the capture layer calls processFrame() once per analyzed block. The
// envelope is what binds to a param (L5: `Speed <- audio.kick`).
#pragma once
#include <string>

namespace sw {

struct AttackParams {
  double floor         = 0.02;  // RMS at/below this is untrusted (silence gate)
  double riseThreshold = 0.08;  // onset fires when the gated RMS rise >= this
  double debounceMs    = 80.0;  // min real-time gap between onsets
  double releaseMs     = 120.0; // envelope decays to 0 over this (real time)
};

struct AttackFrameInput {
  bool   hasRms  = false;
  double rms     = 0.0;
  bool   hasPeak = false;
  double peak    = 0.0;
  double timeMs  = 0.0;  // monotonic frame time (drives debounce + release)
};

struct AttackFrameOutput {
  bool        detectorOk = true;  // false if input unusable (missing/NaN rms)
  bool        onset      = false; // a transient fired this frame
  double      attackValue = 0.0;  // onset strength (clamped gated rise); 0 when no onset
  double      envelope   = 0.0;   // decaying 0..1 envelope (what drives a param)
  double      confidence = 0.0;   // 1 trusted, 0.5 clipped, 0 untrusted
  double      rms        = 0.0;
  double      delta      = 0.0;
  std::string diagnostic;         // e.g. "missing_input:rms"
};

class AttackDetector {
 public:
  AttackDetector() = default;
  explicit AttackDetector(AttackParams p) : p_(p) {}
  AttackFrameOutput processFrame(const AttackFrameInput& in);
  double envelope() const { return env_; }  // latest envelope (no-arg poll)
 private:
  AttackParams p_;
  bool   hasPrev_   = false;
  double prevRms_   = 0.0;
  double lastFrameMs_ = 0.0;
  double lastOnsetMs_ = -1.0;
  double env_       = 0.0;
};

// Isolated proof (Rule 5). Ports my-world's 5 proven cases: quiet->0 onsets; single
// clean rise->exactly 1 onset at frame 2 with attackValue>=0.10; steady loud->0 (no
// re-trigger); micro wiggle below threshold->0; missing rms->detectorOk=false with
// diagnostic "missing_input:rms". injectBug drops the rise threshold to 0 (a real
// degenerate config) so the micro-wiggle/steady cases spuriously fire — teeth, not a
// synthetic flip.
int runAttackSelfTest(bool injectBug);

}  // namespace sw
