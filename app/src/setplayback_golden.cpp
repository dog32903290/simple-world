// setplayback_golden — --selftest-setplayback. The FAITHFUL transport-WRITE golden for the
// [SetPlaybackTime] / [SetPlaybackSpeed] anim ops: the op-arms-provider → frame_cook-pulls-and-applies
// chain (SetPlaybackTime.cs:54 / SetPlaybackSpeed.cs:48 → PlaybackProvider → pullSetPlayback → Transport).
//
// = TiXL Operators/Lib/numbers/anim/time/SetPlaybackTime.cs + SetPlaybackSpeed.cs +
//   playback_provider.h (the sw-side write mailbox the ops route through, NAMED FORK
//   fork-playbackwrite-via-provider — TiXL writes Playback.Current inline). Drives the PRODUCTION op
//   step fns (cookStatefulValueOp "SetPlaybackTime"/"SetPlaybackSpeed") and the PRODUCTION consumer
//   (pullSetPlayback) against a LOCAL Transport, frame by frame, so the make-or-break per-frame-mailbox
//   semantics (an un-armed frame leaves the transport UNTOUCHED) are machine-verified, no 柏為 in loop.
//
// The teeth (-bug): REAL injection via setSetPlaybackBug (stateful_value_ops.h) — each mode corrupts
// one production term inside the step fns (stateful_value_ops_setplayback.cpp) while every expected
// value below stays FIXED at the production-correct answer (GOLDEN_STANDARD 特徵 3: corrupt the cook,
// never flip the want). Mode → tooth:
//   1 GATE IGNORED          → CASE 2 (held re-scrubs to 9), CASE 5 (no-edge frame scrubs), CASE 8
//                             (no-trigger frame sets the speed) — the per-frame-overwrite wrong port
//   2 EDGE-ONLY             → CASE 3 (Continuously stops re-firing), CASE 6 (speed LEVEL becomes edge)
//   3 WRITE SEVERED         → CASE 1 (the edge scrub never lands), CASE 6/7 (rate never set)
//   4 CONDITIONING DROPPED  → CASE 4 (NaN passes through instead of →0), CASE 7 (raw un-snapped speed)
// Under -bug every mode must ADD failures against the SAME fixed asserts; a mode that adds none is a
// dead tooth → return 0 so --bite's NO-BITE list surfaces it.
//
// Transport apply notes (transport.h): scrub(bars) clamps position to >= 0 (no negative bars) — the
// golden uses non-negative target bars. setRate gate: NaN refused (keeps last), |r| clamped to ±16,
// |r| <= 0.001 advances nothing but IS stored. The 0.0001 near-stop speed is stored as-is (within gate).
#include <cstdio>
#include <cmath>

#include "app/cook_host_values.h"        // framecook::pullSetPlayback(Transport&)
#include "runtime/playback_provider.h"   // PlaybackProvider (resetForTest between cases)
#include "runtime/stateful_value_ops.h"  // cookStatefulValueOp / StatefulValueState / setSetPlaybackBug
#include "runtime/transport.h"           // Transport (the apply target the golden inspects)

namespace sw {
namespace {

// One op cook through the PRODUCTION step fn (keyed off the per-instance edge state `st`).
void cookSetTime(StatefulValueState& st, float timeInBars, float mode, float enabled) {
  float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  cookStatefulValueOp("SetPlaybackTime",
                      {{"TimeInBars", timeInBars}, {"TriggerMode", mode}, {"Enabled", enabled}},
                      1.0f / 60.0f, 0.0f, st, out);
}
float cookSetSpeed(StatefulValueState& st, float speedFactor, float trigger) {
  float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  cookStatefulValueOp("SetPlaybackSpeed", {{"SpeedFactor", speedFactor}, {"TriggerUpdate", trigger}},
                      1.0f / 60.0f, 0.0f, st, out);
  return out[0];  // golden probe = the snap-adjusted speed this cook would write
}

// The full 8-case battery with FIXED production-correct wants (independent of any bug mode).
// Returns the number of failed assertions. `tag` labels the run (clean / bug N) in the printout.
int runCases(const char* tag) {
  const double eps = 1e-4;
  int fails = 0;
  auto& p = PlaybackProvider::instance();

  auto check = [&](const char* what, double got, double want) {
    const bool pass = std::fabs(got - want) < eps;
    if (!pass) ++fails;
    std::printf("[selftest-setplayback][%s] %s got=%.4f want=%.4f -> %s\n", tag, what, got, want,
                pass ? "PASS" : "FAIL");
  };

  // ===== CASE 1: SetPlaybackTime EDGE (OnceEnabledGetsTrue): rising Enabled scrubs the playhead ONCE.
  // SetPlaybackTime.cs:32,39: WasTriggered(enabled) — a false→true edge with mode 0 arms TimeInBars;
  // pull → Transport.scrub(4). A SECOND pull (same un-re-armed frame) does NOT move the playhead.
  {
    p.resetForTest();
    Transport t;  // position 0
    StatefulValueState st;
    cookSetTime(st, 4.0f, 0.0f, 0.0f);  // frame 0: Enabled low (no edge, no arm)
    cookSetTime(st, 4.0f, 0.0f, 1.0f);  // frame 1: false→true RISING edge, mode 0 → arms time=4
    framecook::pullSetPlayback(t);      // scrub(4)
    const double afterFirst = t.position;
    framecook::pullSetPlayback(t);      // un-re-armed → NO move
    check("time-edge scrub", afterFirst, 4.0);
    check("time-edge no-double", t.position, afterFirst);  // 2nd pull left it where the 1st put it
  }

  // ===== CASE 2: SetPlaybackTime EDGE-not-LEVEL: a held-true Enabled 2nd frame does NOT re-scrub.
  // mode 0 = OnceEnabledGetsTrue → only the rising edge fires; holding Enabled=true (no edge) is silent.
  {
    p.resetForTest();
    Transport t;
    StatefulValueState st;
    cookSetTime(st, 4.0f, 0.0f, 0.0f);  // low
    cookSetTime(st, 4.0f, 0.0f, 1.0f);  // RISING edge → arms 4
    framecook::pullSetPlayback(t);      // scrub(4)
    cookSetTime(st, 9.0f, 0.0f, 1.0f);  // STILL true (held), mode 0 → must NOT re-arm (edge, not level)
    framecook::pullSetPlayback(t);      // un-armed → playhead stays at 4 (NOT 9)
    check("time-edge-not-level", t.position, 4.0);
  }

  // ===== CASE 3: SetPlaybackTime CONTINUOUSLY (mode 1): scrubs EVERY frame while Enabled, no edge.
  // SetPlaybackTime.cs:39 `enabled && mode==Continuously` → fires every enabled frame, tracking newTime.
  {
    p.resetForTest();
    Transport t;
    StatefulValueState st;
    cookSetTime(st, 2.0f, 1.0f, 1.0f);  // enabled + Continuously → arms 2
    framecook::pullSetPlayback(t);      // scrub(2)
    const double f1 = t.position;
    cookSetTime(st, 7.5f, 1.0f, 1.0f);  // STILL enabled, no edge — Continuously re-arms 7.5
    framecook::pullSetPlayback(t);      // scrub(7.5)
    check("time-continuous f1", f1, 2.0);
    check("time-continuous f2", t.position, 7.5);
  }

  // ===== CASE 4: SetPlaybackTime NaN/Inf → 0 (cs:34-37). Continuously, NaN newTime → scrub(0).
  {
    p.resetForTest();
    Transport t;
    t.scrub(5.0);  // seed a non-zero playhead so a successful NaN→0 scrub is observable
    StatefulValueState st;
    cookSetTime(st, std::nanf(""), 1.0f, 1.0f);  // Continuously, NaN → cs:36 newTime=0 → scrub(0)
    framecook::pullSetPlayback(t);
    check("time-nan->0", t.position, 0.0);
  }

  // ===== CASE 5: SetPlaybackTime no-fire: mode 0, never an edge → playhead UNTOUCHED (make-or-break).
  {
    p.resetForTest();
    Transport t;
    t.scrub(3.0);  // a playhead the operator set elsewhere
    StatefulValueState st;
    cookSetTime(st, 4.0f, 0.0f, 0.0f);  // Enabled never rises → never arms
    cookSetTime(st, 4.0f, 0.0f, 0.0f);
    framecook::pullSetPlayback(t);
    check("time-no-fire untouched", t.position, 3.0);
  }

  // ===== CASE 6: SetPlaybackSpeed LEVEL: setRate every frame TriggerUpdate is true (WasTriggered off).
  // SetPlaybackSpeed.cs:24 WasTriggered COMMENTED OUT → cs:31 `if(triggered)` is LEVEL. A held-true
  // trigger keeps setting the rate each frame (unlike the edge-gated SetPlaybackTime/SetBpm).
  {
    p.resetForTest();
    Transport t;  // rate 1.0
    StatefulValueState st;
    const float probe1 = cookSetSpeed(st, 2.0f, 1.0f);  // triggered → arms 2 (no snap: 2 ∉ [0.95,1.05])
    framecook::pullSetPlayback(t);                      // setRate(2)
    const double f1 = t.rate;
    const float probe2 = cookSetSpeed(st, 3.0f, 1.0f);  // STILL true → LEVEL re-arms 3
    framecook::pullSetPlayback(t);                      // setRate(3)
    check("speed-level probe1", probe1, 2.0);
    check("speed-level f1", f1, 2.0);
    check("speed-level probe2", probe2, 3.0);
    check("speed-level f2", t.rate, 3.0);
  }

  // ===== CASE 7: SetPlaybackSpeed SNAP: near-1 → 1 (cs:39-42), small-positive → 0.0001 (cs:43-46).
  {
    struct { float in; double want; } cases[3] = {{0.98f, 1.0}, {1.04f, 1.0}, {0.01f, 0.0001}};
    for (int i = 0; i < 3; ++i) {
      p.resetForTest();
      Transport t;
      StatefulValueState st;
      const float probe = cookSetSpeed(st, cases[i].in, 1.0f);  // triggered → snap then arm
      framecook::pullSetPlayback(t);                            // setRate(snapped)
      char what[48];
      std::snprintf(what, sizeof(what), "speed-snap in=%.4f", cases[i].in);
      check(what, t.rate, cases[i].want);
      check("speed-snap probe", probe, cases[i].want);
    }
  }

  // ===== CASE 8: SetPlaybackSpeed no-trigger → rate UNTOUCHED (make-or-break).
  {
    p.resetForTest();
    Transport t;
    t.setRate(4.0);  // a rate the operator set on the toolbar
    StatefulValueState st;
    cookSetSpeed(st, 2.0f, 0.0f);  // trigger false → never arms
    cookSetSpeed(st, 2.0f, 0.0f);
    framecook::pullSetPlayback(t);
    check("speed-no-trigger untouched", t.rate, 4.0);
  }

  p.resetForTest();  // leave the singleton clean (no cross-case / cross-test bleed)
  return fails;
}

}  // namespace

int runSetPlaybackSelfTest(bool injectBug) {
  setSetPlaybackBug(0);
  const int cleanFail = runCases("clean");

  if (!injectBug) {
    std::printf("[selftest-setplayback] %s\n", cleanFail == 0 ? "PASS" : "FAIL");
    return cleanFail == 0 ? 0 : 1;
  }

  // -bug: rerun the SAME fixed-want battery under each real production-term corruption. Every mode
  // must ADD failures (its tooth bites); a mode adding none = dead tooth → return 0 (NO-BITE list).
  bool allTripped = true;
  const struct { int mode; const char* name; } bugs[4] = {{1, "bug1-gate-ignored"},
                                                          {2, "bug2-edge-only"},
                                                          {3, "bug3-write-severed"},
                                                          {4, "bug4-conditioning-dropped"}};
  for (const auto& b : bugs) {
    setSetPlaybackBug(b.mode);
    const int f = runCases(b.name);
    std::printf("[selftest-setplayback] %s added %d failure(s)\n", b.name, f);
    if (f == 0) allTripped = false;
  }
  setSetPlaybackBug(0);  // restore production

  if (cleanFail != 0) {  // broken clean is a real red regardless of bite bookkeeping
    std::printf("[selftest-setplayback] FAIL (clean run broken under -bug harness)\n");
    return 1;
  }
  if (!allTripped) {
    std::printf("[selftest-setplayback] injectBug did not trip (a bug mode added no failures)\n");
    return 0;  // dead tooth → NO-BITE list catches it
  }
  std::printf("[selftest-setplayback] BITE (all bug modes tripped against fixed wants)\n");
  return 1;
}

}  // namespace sw
