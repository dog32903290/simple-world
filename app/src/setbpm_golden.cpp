// setbpm_golden — --selftest-setbpm. The FAITHFUL transport-BPM golden for the [SetBpm] VJ op:
// the triggered-pull chain SetBpm(edge) → BpmProvider(armed) → pullSetBpmRate → composition.bpm.
//
// = TiXL Operators/Lib/numbers/anim/vj/SetBpm.cs + Core/IO/BpmProvider.cs:22-33 + Editor/.../
// PlaybackUtils.cs:74-78 (the per-frame consumer). Drives the PRODUCTION op step fn (cookStatefulValueOp
// "SetBpm") and the PRODUCTION consumer (pullSetBpmRate) headlessly, frame by frame, so the make-or-break
// triggered-pull semantics (NOT a per-frame overwrite) are machine-verified with no 柏為 in the loop.
//
// The teeth (-bug): there is no production bug-flag hook in the op (faithful path only), so the bug is
// injected at the EXPECTATION level — each case flips its expected value to the WRONG-port answer
// (level instead of edge / a write without a trigger / the unclamped rate), so the real production
// output FAILs against it. A green here = the production path does the RIGHT thing; -bug RED = the
// asserted wrong behavior really is wrong (the tooth bites).
#include <cstdio>
#include <cmath>

#include "app/cook_host_values.h"          // framecook::pullSetBpmRate
#include "runtime/bpm_provider.h"           // BpmProvider (probe + resetForTest)
#include "runtime/compound_graph.h"         // CompositionSettings
#include "runtime/stateful_value_ops.h"     // cookStatefulValueOp / StatefulValueState

namespace sw {

// One SetBpm op cook (the production step fn), keyed off the per-instance edge state `st`.
static void cookSetBpm(StatefulValueState& st, float bpmRate, float trigger) {
  float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  cookStatefulValueOp("SetBpm", {{"BpmRate", bpmRate}, {"TriggerUpdate", trigger}}, 1.0f / 60.0f,
                      0.0f, st, out);
}

int runSetBpmSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;
  auto& provider = BpmProvider::instance();

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

    const bool wantFirst = !injectBug;        // bug: claim no edge fired (writes-without-trigger fork)
    const float wantBpm = injectBug ? 120.0f : 128.0f;  // bug: claim transport stayed at default
    const bool pass = (gotFirst == wantFirst) && !gotSecond && std::fabs(pulled - 128.0f) < eps &&
                      wrote && std::fabs(comp.bpm - wantBpm) < eps;
    ok = ok && pass;
    std::printf("[selftest-setbpm] edge128 firstPull=%d(want %d) secondPull=%d(want 0) pulled=%.1f "
                "comp.bpm=%.1f(want %.1f) -> %s\n",
                gotFirst, wantFirst, gotSecond, pulled, comp.bpm, wantBpm, pass ? "PASS" : "FAIL");
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
    const bool wantWrote = injectBug;                    // bug: claim a no-trigger frame DID write
    const double wantBpm = injectBug ? 128.0 : 137.0;    // bug: claim it overwrote to 128
    const bool pass = (wrote == wantWrote) && std::fabs(comp.bpm - wantBpm) < 1e-4;
    ok = ok && pass;
    std::printf("[selftest-setbpm] no-trigger wrote=%d(want %d) comp.bpm=%.1f(want %.1f) -> %s\n",
                wrote, wantWrote, comp.bpm, wantBpm, pass ? "PASS" : "FAIL");
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
    const bool wantE2 = injectBug;  // bug: claim a held-true LEVEL re-fired (the level-instead-of-edge bug)
    const bool pass = e1 && std::fabs(p1 - 128.0f) < eps && (e2 == wantE2);
    ok = ok && pass;
    std::printf("[selftest-setbpm] edge-not-level firstEdge=%d(128 once) heldReFire=%d(want %d) -> %s\n",
                e1, e2, wantE2, pass ? "PASS" : "FAIL");
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
      // bug: claim the RAW unclamped rate survived (the clamp-dropped fork)
      const float want = injectBug ? cases[i].in : cases[i].want;
      const bool pass = std::fabs(p - want) < eps;
      ok = ok && pass;
      std::printf("[selftest-setbpm] clamp in=%.0f -> %.0f(want %.0f) -> %s\n", cases[i].in, p, want,
                  pass ? "PASS" : "FAIL");
    }
  }

  // Leave the singleton clean for any later in-process selftest (no cross-test bleed).
  provider.resetForTest();
  std::printf("[selftest-setbpm] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
