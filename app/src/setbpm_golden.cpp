// setbpm_golden — --selftest-setbpm. The FAITHFUL transport-BPM golden for the [SetBpm] VJ op:
// the triggered-pull chain SetBpm(edge) → BpmProvider(armed) → pullSetBpmRate → composition.bpm.
//
// = TiXL Operators/Lib/numbers/anim/vj/SetBpm.cs + Core/IO/BpmProvider.cs:22-33 + Editor/.../
// PlaybackUtils.cs:74-78 (the per-frame consumer). Drives the PRODUCTION op step fn (cookStatefulValueOp
// "SetBpm") and the PRODUCTION consumer (pullSetBpmRate) headlessly, frame by frame, so the make-or-break
// triggered-pull semantics (NOT a per-frame overwrite) are machine-verified with no 柏為 in the loop.
//
// The teeth (-bug): REAL injection via setSetBpmBug (stateful_value_ops.h) — each mode corrupts one
// production term inside stepSetBpm (stateful_value_ops_setbpm.cpp) while every expected value below
// stays FIXED at the production-correct answer (GOLDEN_STANDARD 特徵 3: corrupt the cook, never flip
// the want). Mode → tooth:
//   1 GATE IGNORED   → CASE 2 (a no-trigger frame writes; comp.bpm clobbered) + CASE 3 (held re-fires)
//   2 CLAMP DROPPED  → CASE 4 (the raw 300/10 survives instead of 240/54)
//   3 WRITE SEVERED  → CASE 1 (the edge never arms; comp.bpm never becomes 128)
// Under -bug every mode must ADD failures against the SAME fixed asserts; a mode that adds none is a
// dead tooth → return 0 so --bite's NO-BITE list surfaces it.
#include <cstdio>
#include <cmath>

#include "app/cook_host_values.h"          // framecook::pullSetBpmRate
#include "runtime/bpm_provider.h"           // BpmProvider (probe + resetForTest)
#include "runtime/compound_graph.h"         // CompositionSettings
#include "runtime/stateful_value_ops.h"     // cookStatefulValueOp / StatefulValueState / setSetBpmBug

namespace sw {
namespace {

// One SetBpm op cook (the production step fn), keyed off the per-instance edge state `st`.
void cookSetBpm(StatefulValueState& st, float bpmRate, float trigger) {
  float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  cookStatefulValueOp("SetBpm", {{"BpmRate", bpmRate}, {"TriggerUpdate", trigger}}, 1.0f / 60.0f,
                      0.0f, st, out);
}

// The full 4-case battery with FIXED production-correct wants (independent of any bug mode).
// Returns the number of failed assertions. `tag` labels the run (clean / bug N) in the printout.
int runCases(const char* tag) {
  const float eps = 1e-4f;
  int fails = 0;
  auto& provider = BpmProvider::instance();
  auto note = [&](bool pass) { if (!pass) ++fails; return pass ? "PASS" : "FAIL"; };

  // ===== CASE 1: rising edge BpmRate=128 → provider holds 128 ONCE, then false; transport→128 =====
  // SetBpm.cs:22,25,38-39: a false→true edge with bpm>1 arms BpmProvider.NewBpmRate=clampedRate. The
  // provider's tryGetNewBpmRate returns true→128 ONCE (clear-on-read, BpmProvider.cs:30-32) then false.
  // After pullSetBpmRate (PlaybackUtils.cs:77) comp.bpm == 128.
  {
    provider.resetForTest();
    CompositionSettings comp;  // bpm defaults 120
    StatefulValueState st;
    cookSetBpm(st, 0.0f, 0.0f);     // frame 0: trigger low (no edge, no arm)
    cookSetBpm(st, 128.0f, 1.0f);   // frame 1: false→true RISING edge with 128 → arms provider

    float pulled = -1.0f;
    const bool gotFirst = provider.tryGetNewBpmRate(pulled);  // armed → true, 128, CLEARS
    float pulled2 = -1.0f;
    const bool gotSecond = provider.tryGetNewBpmRate(pulled2);  // cleared → false (the triggered-pull)
    // Re-arm for the real consumer call (resetForTest+re-fire to mirror the actual frame path).
    provider.resetForTest();
    StatefulValueState st2;
    cookSetBpm(st2, 0.0f, 0.0f);
    cookSetBpm(st2, 128.0f, 1.0f);
    const bool wrote = framecook::pullSetBpmRate(comp);

    const bool pass = gotFirst && !gotSecond && std::fabs(pulled - 128.0f) < eps && wrote &&
                      std::fabs(comp.bpm - 128.0) < eps;
    std::printf("[selftest-setbpm][%s] edge128 firstPull=%d(want 1) secondPull=%d(want 0) pulled=%.1f "
                "comp.bpm=%.1f(want 128.0) -> %s\n",
                tag, gotFirst, gotSecond, pulled, comp.bpm, note(pass));
  }

  // ===== CASE 2: NO trigger / no edge → false; transport UNCHANGED (the make-or-break) =====
  // BpmProvider.cs:24 `if(!SetBpmTriggered) return false` → comp.bpm must NOT move. A per-frame
  // overwrite (the WRONG port) would clobber it — this is the case that catches it.
  {
    provider.resetForTest();
    CompositionSettings comp;
    comp.bpm = 137.0;  // a non-default sentinel: if anything overwrites every-frame, this dies
    StatefulValueState st;
    cookSetBpm(st, 128.0f, 0.0f);  // trigger never rises → never arms
    cookSetBpm(st, 128.0f, 0.0f);
    const bool wrote = framecook::pullSetBpmRate(comp);  // not armed → false, comp untouched
    const bool pass = !wrote && std::fabs(comp.bpm - 137.0) < 1e-4;
    std::printf("[selftest-setbpm][%s] no-trigger wrote=%d(want 0) comp.bpm=%.1f(want 137.0) -> %s\n",
                tag, wrote, comp.bpm, note(pass));
  }

  // ===== CASE 3: EDGE not LEVEL — holding TriggerUpdate=true a 2nd frame does NOT re-fire =====
  // SetBpm.cs:22 MathUtils.WasTriggered = a RISING edge (false→true once). A held-true 2nd frame must
  // NOT re-arm: after consuming the first edge, a 2nd cook at still-true 144 leaves the provider unarmed.
  {
    provider.resetForTest();
    StatefulValueState st;
    cookSetBpm(st, 128.0f, 0.0f);  // low
    cookSetBpm(st, 128.0f, 1.0f);  // RISING edge → arms 128
    float p1 = -1.0f;
    const bool e1 = provider.tryGetNewBpmRate(p1);   // consume the edge (true, 128)
    cookSetBpm(st, 144.0f, 1.0f);  // STILL true (held) → must NOT re-arm (edge, not level)
    float p2 = -1.0f;
    const bool e2 = provider.tryGetNewBpmRate(p2);   // no new edge → false
    const bool pass = e1 && std::fabs(p1 - 128.0f) < eps && !e2;
    std::printf("[selftest-setbpm][%s] edge-not-level firstEdge=%d(128 once) heldReFire=%d(want 0) -> %s\n",
                tag, e1, e2, note(pass));
  }

  // ===== CASE 4: CLAMP 54..240 (SetBpm.cs:24 .Clamp(54,240)) =====
  // 300 → 240 (upper), 10 → 54 (lower). Each fired on its own rising edge.
  {
    struct { float in, want; } cases[2] = {{300.0f, 240.0f}, {10.0f, 54.0f}};
    for (int i = 0; i < 2; ++i) {
      provider.resetForTest();
      StatefulValueState st;
      cookSetBpm(st, cases[i].in, 0.0f);
      cookSetBpm(st, cases[i].in, 1.0f);  // edge → arms clamp(in)
      float p = -1.0f;
      provider.tryGetNewBpmRate(p);
      const bool pass = std::fabs(p - cases[i].want) < eps;
      std::printf("[selftest-setbpm][%s] clamp in=%.0f -> %.0f(want %.0f) -> %s\n", tag, cases[i].in,
                  p, cases[i].want, note(pass));
    }
  }

  provider.resetForTest();  // leave the singleton clean (no cross-case / cross-test bleed)
  return fails;
}

}  // namespace

int runSetBpmSelfTest(bool injectBug) {
  setSetBpmBug(0);
  const int cleanFail = runCases("clean");

  if (!injectBug) {
    std::printf("[selftest-setbpm] %s\n", cleanFail == 0 ? "PASS" : "FAIL");
    return cleanFail == 0 ? 0 : 1;
  }

  // -bug: rerun the SAME fixed-want battery under each real production-term corruption. Every mode
  // must ADD failures (its tooth bites); a mode adding none = dead tooth → return 0 (NO-BITE list).
  bool allTripped = true;
  const struct { int mode; const char* name; } bugs[3] = {
      {1, "bug1-gate-ignored"}, {2, "bug2-clamp-dropped"}, {3, "bug3-write-severed"}};
  for (const auto& b : bugs) {
    setSetBpmBug(b.mode);
    const int f = runCases(b.name);
    std::printf("[selftest-setbpm] %s added %d failure(s)\n", b.name, f);
    if (f == 0) allTripped = false;
  }
  setSetBpmBug(0);  // restore production

  if (cleanFail != 0) {  // broken clean is a real red regardless of bite bookkeeping
    std::printf("[selftest-setbpm] FAIL (clean run broken under -bug harness)\n");
    return 1;
  }
  if (!allTripped) {
    std::printf("[selftest-setbpm] injectBug did not trip (a bug mode added no failures)\n");
    return 0;  // dead tooth → NO-BITE list catches it
  }
  std::printf("[selftest-setbpm] BITE (all bug modes tripped against fixed wants)\n");
  return 1;
}

}  // namespace sw
