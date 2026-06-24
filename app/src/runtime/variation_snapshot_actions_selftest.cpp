// variation_snapshot_actions_selftest — --selftest-variation-snapshot-actions. Harness-first golden
// for Lane L1 (Variation/Snapshot): the SNAPSHOT-SLOT state machine (runtime/variation_snapshot_actions.h).
// Drives the pure action functions over a VariationPool + VariationCrossfader and pins every slot
// transition TiXL's SnapshotActions.cs makes — apply-if-filled / capture-if-empty / overwrite / remove
// / set-active. Pure CPU (no GPU, no UI, no MIDI), so the golden is a direct state comparison.
//
// EXPECTED STATES — hand-derived from external/tixl/.../SnapshotActions.cs (each transition cites the
// branch it proves):
//
//   T1  empty slot 1 + activate(overwrite=false)  → SnapshotActions.cs:25-28 CREATE branch:
//        pool now HAS a snapshot at index 1 carrying the CURRENT child values; result = Captured;
//        crossfader.activeSnapshotIndex() == 1, snapshotRight() == -1 (SetActiveSnapshot clears right).
//   T2  filled slot 1 + activate(overwrite=false)  → SnapshotActions.cs:17-22 APPLY branch:
//        pool UNCHANGED (count same, value unchanged — NOT re-captured); result = Applied;
//        crossfader set-active to 1 (left=1, activeIsLeft, right=-1). The value stays the FIRST capture.
//   T3  filled slot 1 + save(overwrite=true)  → SnapshotActions.cs:31-43 SAVE branch:
//        pool's snapshot at 1 is REPLACED with the new current (delete-then-add); result = Captured;
//        count unchanged (overwrite, not append); the stored value is now the NEW current.
//   T4  filled slot 1 + remove  → SnapshotActions.cs:54-57 DELETE branch:
//        pool no longer has a snapshot at index 1; result = Removed; a DIFFERENT slot is untouched.
//   T5  empty slot 9 + remove  → SnapshotActions.cs:58-61 empty branch:
//        nothing happens; result = NoOp; pool count unchanged.
//   T6  remove deletes the RIGHT slot — fill 1 AND 2, remove 1, assert 2 SURVIVES (and 1 gone).
//
// ★ RED tooth (injectBug, non-theatrical — bites real behavior, not a flipped constant):
//   the bug forces overwrite=TRUE on T2's activate (the bare-pad press becomes a Save). Then a bare
//   re-press of an ALREADY-FILLED slot would OVERWRITE it with the (different) current values instead
//   of APPLYING the stored snapshot. T2 captures the live value as DIFFERENT from slot 1's stored
//   value, so the genuine-overwrite changes the stored value → the "value unchanged after activate"
//   assertion FAILS, and result becomes Captured instead of Applied. That is the exact real bug
//   "activate overwrites a filled slot instead of applying it".
#include <cstdio>
#include <vector>

#include "runtime/selftest_registry.h"             // REGISTER_SELFTESTS
#include "runtime/variation_crossfader.h"          // VariationCrossfader
#include "runtime/variation_pool.h"                // VariationPool / SnapshotChildState / VariationValue
#include "runtime/variation_snapshot_actions.h"    // activateOrCreate / removeSlot / SnapshotActionResult

namespace sw {
namespace {

constexpr InputId kInputA = 100;

// Build a one-child (composition) live state carrying inputA = `value`. This is the "current graph
// state" the actions capture from. enabledForSnapshots=true so it survives the pool's filter.
std::vector<SnapshotChildState> liveWith(float value) {
  SnapshotChildState comp;
  comp.childId = kCompositionNode;
  comp.enabledForSnapshots = true;
  comp.values[kInputA] = VariationValue::makeFloat(value);
  return {comp};
}

// Read back the stored inputA at a slot (or NaN-ish sentinel if absent), for value assertions.
bool slotHasValue(const VariationPool& pool, int slot, float want) {
  const Variation* v = pool.tryGetSnapshot(slot);
  if (!v) return false;
  const VariationValue* val = v->find(kCompositionNode, kInputA);
  return val && val->equals(VariationValue::makeFloat(want));
}

bool runSnapshotActionsGolden(bool injectBug) {
  VariationPool pool;
  VariationCrossfader xf(pool);
  bool ok = true;

  // ── T1: empty slot 1 + activate(overwrite=false) → CAPTURE current (value 10) + set-active ──────
  const SnapshotActionResult r1 = activateOrCreate(pool, xf, /*slot=*/1, /*overwrite=*/false, liveWith(10.0f), "s1");
  const bool t1 = (r1 == SnapshotActionResult::Captured) &&
                  slotHasValue(pool, 1, 10.0f) &&
                  (xf.activeSnapshotIndex() == 1) && (xf.snapshotRight() == -1);
  ok = t1 && ok;
  std::printf("[selftest-variation-snapshot-actions] T1 empty+activate -> capture: result=%d val10=%s "
              "active=%d right=%d -> %s\n",
              (int)r1, slotHasValue(pool, 1, 10.0f) ? "y" : "n", xf.activeSnapshotIndex(),
              xf.snapshotRight(), t1 ? "PASS" : "FAIL");

  // ── T2: filled slot 1 + activate(overwrite=false) → APPLY (no re-capture); value stays 10 ───────
  // ★ injectBug forces overwrite=true → the bare press becomes a Save → it OVERWRITES slot 1 with the
  //   NEW current (20) instead of applying the stored 10. The value-unchanged + Applied checks bite.
  const size_t countBeforeT2 = pool.size();
  const bool bugOverwrite = injectBug ? true : false;
  const SnapshotActionResult r2 =
      activateOrCreate(pool, xf, /*slot=*/1, /*overwrite=*/bugOverwrite, liveWith(20.0f), "s1b");
  const bool t2 = (r2 == SnapshotActionResult::Applied) &&     // clean: Applied (bug: Captured)
                  (pool.size() == countBeforeT2) &&            // count unchanged either way
                  slotHasValue(pool, 1, 10.0f) &&              // clean: still 10 (bug: now 20 → fails)
                  (xf.activeSnapshotIndex() == 1);
  ok = t2 && ok;
  std::printf("[selftest-variation-snapshot-actions] T2 filled+activate -> apply: result=%d (want %d) "
              "stillVal10=%s count=%zu -> %s\n",
              (int)r2, (int)SnapshotActionResult::Applied, slotHasValue(pool, 1, 10.0f) ? "y" : "n",
              pool.size(), t2 ? "PASS" : "FAIL");

  // ── T3: filled slot 1 + save(overwrite=true) → OVERWRITE with new current (30); count unchanged ──
  // (run from a fresh pool so the bug in T2 cannot have already mutated slot 1's value.)
  VariationPool pool3;
  VariationCrossfader xf3(pool3);
  activateOrCreate(pool3, xf3, /*slot=*/1, /*overwrite=*/false, liveWith(10.0f), "s1");  // capture 10
  const size_t countBeforeT3 = pool3.size();
  const SnapshotActionResult r3 =
      activateOrCreate(pool3, xf3, /*slot=*/1, /*overwrite=*/true, liveWith(30.0f), "s1c");  // save 30
  const bool t3 = (r3 == SnapshotActionResult::Captured) &&
                  (pool3.size() == countBeforeT3) &&   // overwrite, NOT append
                  slotHasValue(pool3, 1, 30.0f) &&     // value replaced 10 → 30
                  !slotHasValue(pool3, 1, 10.0f);
  ok = t3 && ok;
  std::printf("[selftest-variation-snapshot-actions] T3 filled+save -> overwrite: result=%d "
              "newVal30=%s count=%zu->%zu -> %s\n",
              (int)r3, slotHasValue(pool3, 1, 30.0f) ? "y" : "n", countBeforeT3, pool3.size(),
              t3 ? "PASS" : "FAIL");

  // ── T4 + T6: remove deletes the RIGHT slot. Fill 1 (val 10) AND 2 (val 50); remove 1; assert
  //   slot 1 gone, slot 2 SURVIVES with its value, result=Removed. ★ a remove that deleted the wrong
  //   slot (or by position) would take out slot 2 — the t6survive check bites that. ─────────────────
  VariationPool pool4;
  VariationCrossfader xf4(pool4);
  activateOrCreate(pool4, xf4, /*slot=*/1, /*overwrite=*/false, liveWith(10.0f), "a");
  activateOrCreate(pool4, xf4, /*slot=*/2, /*overwrite=*/false, liveWith(50.0f), "b");
  const SnapshotActionResult r4 = removeSlot(pool4, /*slot=*/1);
  const bool t4 = (r4 == SnapshotActionResult::Removed) && (pool4.tryGetSnapshot(1) == nullptr);
  const bool t6survive = slotHasValue(pool4, 2, 50.0f);  // the OTHER slot must be untouched
  ok = t4 && t6survive && ok;
  std::printf("[selftest-variation-snapshot-actions] T4 filled+remove -> result=%d slot1gone=%s | "
              "T6 slot2-survives(val50)=%s -> %s\n",
              (int)r4, pool4.tryGetSnapshot(1) == nullptr ? "y" : "n", t6survive ? "y" : "n",
              (t4 && t6survive) ? "PASS" : "FAIL");

  // ── T5: empty slot 9 + remove → NoOp; pool count unchanged ───────────────────────────────────────
  const size_t countBeforeT5 = pool4.size();
  const SnapshotActionResult r5 = removeSlot(pool4, /*slot=*/9);
  const bool t5 = (r5 == SnapshotActionResult::NoOp) && (pool4.size() == countBeforeT5);
  ok = t5 && ok;
  std::printf("[selftest-variation-snapshot-actions] T5 empty+remove -> result=%d (want %d=NoOp) "
              "count=%zu->%zu -> %s\n",
              (int)r5, (int)SnapshotActionResult::NoOp, countBeforeT5, pool4.size(),
              t5 ? "PASS" : "FAIL");

  std::printf("[selftest-variation-snapshot-actions] %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runVariationSnapshotActionsSelfTest(bool injectBug) {
  return runSnapshotActionsGolden(injectBug) ? 0 : 1;
}

}  // namespace sw

namespace sw {
// Self-register into the --selftest router (independent leaf). orderBase 307 appends after
// variation-pool-json (306) deterministically — the registry sorts by order.
REGISTER_SELFTESTS(/*orderBase=*/307,
    {"variation-snapshot-actions", runVariationSnapshotActionsSelfTest});
}  // namespace sw
