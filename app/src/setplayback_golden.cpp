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
// The teeth (-bug): no production bug-flag in the ops (faithful path only) → the bug is injected at the
// EXPECTATION level — each case flips its expected value to the WRONG-port answer (level instead of
// edge / a write without a gate / the un-snapped speed), so the real production output FAILs against
// it. Green = the production path does the RIGHT thing; -bug RED = the asserted wrong behavior bites.
//
// Transport apply notes (transport.h): scrub(bars) clamps position to >= 0 (no negative bars) — the
// golden uses non-negative target bars. setRate gate: NaN refused (keeps last), |r| clamped to ±16,
// |r| <= 0.001 advances nothing but IS stored. The 0.0001 near-stop speed is stored as-is (within gate).
#include <cstdio>
#include <cmath>

#include "app/cook_host_values.h"        // framecook::pullSetPlayback(Transport&)
#include "runtime/playback_provider.h"   // PlaybackProvider (resetForTest between cases)
#include "runtime/stateful_value_ops.h"  // cookStatefulValueOp / StatefulValueState
#include "runtime/transport.h"           // Transport (the apply target the golden inspects)

namespace sw {

// One op cook through the PRODUCTION step fn (keyed off the per-instance edge state `st`).
static void cookSetTime(StatefulValueState& st, float timeInBars, float mode, float enabled) {
  float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  cookStatefulValueOp("SetPlaybackTime",
                      {{"TimeInBars", timeInBars}, {"TriggerMode", mode}, {"Enabled", enabled}},
                      1.0f / 60.0f, 0.0f, st, out);
}
static float cookSetSpeed(StatefulValueState& st, float speedFactor, float trigger) {
  float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  cookStatefulValueOp("SetPlaybackSpeed", {{"SpeedFactor", speedFactor}, {"TriggerUpdate", trigger}},
                      1.0f / 60.0f, 0.0f, st, out);
  return out[0];  // golden probe = the snap-adjusted speed this cook would write
}

int runSetPlaybackSelfTest(bool injectBug) {
  const double eps = 1e-4;
  bool ok = true;
  auto& p = PlaybackProvider::instance();

  auto check = [&](const char* tag, double got, double want) {
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    std::printf("[selftest-setplayback] %s got=%.4f want=%.4f -> %s\n", tag, got, want,
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
    const double wantBars = injectBug ? 0.0 : 4.0;  // bug: claim the playhead never moved
    check("time-edge scrub", afterFirst, wantBars);
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
    const double want = injectBug ? 9.0 : 4.0;  // bug: claim a held-true LEVEL re-scrubbed to 9
    check("time-edge-not-level", t.position, want);
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
    const double wantF2 = injectBug ? 2.0 : 7.5;  // bug: claim Continuously only fired once
    check("time-continuous f2", t.position, wantF2);
  }

  // ===== CASE 4: SetPlaybackTime NaN/Inf → 0 (cs:34-37). Continuously, NaN newTime → scrub(0).
  {
    p.resetForTest();
    Transport t;
    t.scrub(5.0);  // seed a non-zero playhead so a successful NaN→0 scrub is observable
    StatefulValueState st;
    cookSetTime(st, std::nanf(""), 1.0f, 1.0f);  // Continuously, NaN → cs:36 newTime=0 → scrub(0)
    framecook::pullSetPlayback(t);
    const double want = injectBug ? 5.0 : 0.0;  // bug: claim NaN was rejected / playhead unchanged
    check("time-nan->0", t.position, want);
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
    const double want = injectBug ? 4.0 : 3.0;  // bug: claim a no-edge frame scrubbed to 4
    check("time-no-fire untouched", t.position, want);
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
    const double wantF2 = injectBug ? 2.0 : 3.0;  // bug: claim it was edge-gated (2nd frame silent)
    check("speed-level f2", t.rate, wantF2);
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
      const double want = injectBug ? (double)cases[i].in : cases[i].want;  // bug: raw un-snapped survived
      char tag[48];
      std::snprintf(tag, sizeof(tag), "speed-snap in=%.4f", cases[i].in);
      check(tag, t.rate, want);
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
    const double want = injectBug ? 2.0 : 4.0;  // bug: claim a no-trigger frame set the speed to 2
    check("speed-no-trigger untouched", t.rate, want);
  }

  // Leave the singleton clean for any later in-process selftest (no cross-test bleed).
  p.resetForTest();
  std::printf("[selftest-setplayback] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
