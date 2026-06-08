#include "runtime/attack_detector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace sw {
namespace {
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
}  // namespace

AttackFrameOutput AttackDetector::processFrame(const AttackFrameInput& in) {
  AttackFrameOutput out;

  if (!in.hasRms || !std::isfinite(in.rms)) {
    out.detectorOk = false;
    out.diagnostic = "missing_input:rms";
    out.envelope = env_;  // hold the last envelope; don't lie about a value
    return out;
  }

  // Time-based release: decay the envelope by real elapsed time, not per-block, so the
  // boom-then-fade is frame-rate independent.
  const double elapsedMs = hasPrev_ ? std::max(0.0, in.timeMs - lastFrameMs_) : 0.0;
  if (p_.releaseMs > 0.0 && elapsedMs > 0.0)
    env_ *= std::max(0.0, 1.0 - (elapsedMs / p_.releaseMs));

  const double currentRms = std::max(0.0, in.rms);
  const double baselineRms = hasPrev_ ? prevRms_ : currentRms;  // first frame: no rise
  const double delta = currentRms - baselineRms;
  const bool   trusted = currentRms >= p_.floor;                // silence gate
  const double gatedDelta = trusted ? delta : 0.0;
  const bool   debounceActive =
      lastOnsetMs_ >= 0.0 && (in.timeMs - lastOnsetMs_) < p_.debounceMs;
  const bool   onset = trusted && gatedDelta >= p_.riseThreshold && !debounceActive;
  const bool   clipped = in.hasPeak && in.peak > 1.0;

  out.rms = currentRms;
  out.delta = delta;
  out.confidence = trusted ? (clipped ? 0.5 : 1.0) : 0.0;

  if (onset) {
    out.onset = true;
    out.attackValue = clamp01(gatedDelta);
    env_ = out.attackValue;  // snap envelope up to the transient strength
    lastOnsetMs_ = in.timeMs;
  }

  out.envelope = env_;
  prevRms_ = currentRms;
  lastFrameMs_ = in.timeMs;
  hasPrev_ = true;
  return out;
}

int runAttackSelfTest(bool injectBug) {
  AttackParams base;                         // floor 0.02, rise 0.08, debounce 80, release 120
  if (injectBug) base.riseThreshold = 0.0;   // degenerate: any rise fires -> wiggle/steady spurious

  struct CaseResult { int onsets; double maxAttack; int firstOnset; };
  auto run = [](AttackParams p, const std::vector<double>& rmsSeq, double dtMs) -> CaseResult {
    AttackDetector d(p);
    CaseResult r{0, 0.0, -1};
    double t = 0.0;
    for (size_t i = 0; i < rmsSeq.size(); ++i) {
      AttackFrameInput in;
      in.hasRms = true;
      in.rms = rmsSeq[i];
      in.timeMs = t;
      auto o = d.processFrame(in);
      if (o.onset) {
        if (r.firstOnset < 0) r.firstOnset = (int)i;
        ++r.onsets;
        r.maxAttack = std::max(r.maxAttack, o.attackValue);
      }
      t += dtMs;
    }
    return r;
  };

  bool ok = true;

  // 1. quiet (all below floor) -> no onset.
  { CaseResult r = run(base, {0.0, 0.0, 0.005, 0.0, 0.005, 0.0}, 10.0); ok = ok && (r.onsets == 0); }

  // 2. single clean entry -> exactly one onset at frame 2, attackValue >= 0.10.
  { CaseResult r = run(base, {0.0, 0.0, 0.2, 0.2, 0.2, 0.2}, 10.0);
    ok = ok && (r.onsets == 1) && (r.firstOnset == 2) && (r.maxAttack >= 0.10); }

  // 3. steady loud (starts loud, stays) -> no onset (no rise to detect).
  { CaseResult r = run(base, {0.3, 0.3, 0.3, 0.3, 0.3}, 10.0); ok = ok && (r.onsets == 0); }

  // 4. micro wiggle, every delta below the rise threshold -> no onset.
  { CaseResult r = run(base, {0.10, 0.13, 0.10, 0.14, 0.10, 0.15, 0.11, 0.14}, 10.0);
    ok = ok && (r.onsets == 0); }

  // 5. missing rms -> detectorOk=false + diagnostic.
  { AttackDetector d(base);
    AttackFrameInput in; in.hasRms = false; in.timeMs = 0.0;
    auto o = d.processFrame(in);
    ok = ok && (!o.detectorOk) && (o.diagnostic == "missing_input:rms"); }

  printf("[selftest-attack] quiet/clean/steady/wiggle/missing (rise=%.2f) -> %s\n",
         base.riseThreshold, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
