// app/frame_cook_randomchoice_selftest — RandomChoiceIndex PRODUCTION-PATH pin
// (--selftest-randomchoiceindex). R-2 IRON RULE: stateful ops live on the RESIDENT cook, so the
// golden drives the REAL production seam (framecook::cookStatefulValueNodes — the exact
// once-per-frame cook run() calls), carrying ONE StatefulValueState map across frames (the
// cross-frame channel), and reads Result (extOut[0]) by resident path.
//
// Expected values: hand-derived from TiXL RandomChoiceIndex.cs (Update cs:26-58, FillBuffer
// cs:65-94) + MathUtils.XxHash (MathUtils.cs:113-129) + MathUtils.Mod (MathUtils.cs:273-284) via an
// INDEPENDENT Python transcription of those exact lines (scratch rci_oracle.py — transcribed from
// the .cs, NOT from the sw implementation; the algorithm's own no-consecutive-repeat design property
// held over n=0..299, validating the transcription). Wants are FIXED numbers below.
//
// What it proves:
//   A early paths (cs:30-40): Mod<2 → 0 (the .t3 fresh-drop eval, Mod=1); Mod==2 → n.Mod(2)
//     incl. the negative wrap ((-3).Mod(2)=1, MathUtils.cs:273-284).
//   B the XxHash slice-buffer chain (cs:42-57), ONE instance walked across frames at Mod=5:
//     n=0→4, 1→0, 2→1, 3→4, 17→0, 50→3, 76→2, 99→4. The n=76 frame crosses the internal window
//     boundary ([start,start+100) = [-25,75)) → in-place refill of the SAME block (cs:44). Every
//     probe sits mid-band of the mod-5 value space (a chain/rounding/hash error shifts them).
//   C modulo-change refill (cs:45): same instance, n=17: Mod=5 → 0, then Mod=7 → 4 (the fresh
//     Mod=7 table at the same n — state hysteresis does not leak across a modulo switch).
//   D the cross-frame STATE tooth (the hysteresis quirk, cs:42-44): one instance cooks n=10 (window
//     start=-25) then n=-10 — NO refill triggers (-10 >= -25 && -10 < 75), so it reads slot
//     (-10).Mod(100)=90 of the OLD start=-25 buffer → 1; a FRESH instance at n=-10 refills at
//     start=-125 → 0. History(1) vs fresh(0) DIVERGE — provable only if the production state map
//     actually carried (_lastBufferIndex,_modulo) across frames.
//
// injectBug (a REAL production-term corruption via runtime setRandomChoiceIndexBug, NOT a flipped
// expectation): bug 1 = DROP the "+1" anti-repetition term in the chain offset (cs:75
// `Mod(modulo-1)+1` → `Mod(modulo-1)`) — every chained buffer value from index 1 on shifts → the
// B/C/D mod>=3 probes FAIL while the A early paths stay green (proving the bite is in the chain).
// The wants are INDEPENDENT of the flag (no co-conditioning). did-not-trip → the run adds 0
// failures → exit 0 → the --bite NO-BITE list catches a dead tooth.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/graph.h"                // findSpec (RandomChoiceIndex NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setRandomChoiceIndexBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectEq(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-4f;
  if (!ok) { ++g_fail; printf("  [randomchoiceindex] FAIL %s got=%.4f want=%.4f\n", what, got, want); }
  else printf("  [randomchoiceindex] ok   %s = %.0f\n", what, got);
}

// Register the RandomChoiceIndex atomic symbol into the lib (regenerate from its NodeSpec).
void ensureSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("RandomChoiceIndex")) lib.symbols["RandomChoiceIndex"] = atomicSymbolFromSpec(*s);
}

// Build ONE root holding a single RandomChoiceIndex child (id 1) with the given Value/Mod overrides,
// projected to a resident graph (Constant overrides — the float rail the inspector writes).
ResidentEvalGraph build(SymbolLibrary& lib, float value, float mod) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "RandomChoiceIndex";
  c.overrides = {{"Value", value}, {"Mod", mod}};
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

// Cook one production frame through the REAL seam; return Result (extOut[0]).
// `state` is the cross-frame channel — the caller owns it per logical instance.
float cookOnce(SymbolLibrary& lib, std::map<std::string, StatefulValueState>& state, float value,
               float mod, uint32_t frame) {
  ResidentEvalGraph g = build(lib, value, mod);
  ContextVarMap vars;
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars);
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[0] : -999.0f;
}

// The full golden body (clean or under the bug mode). Accumulates into g_fail.
void runAllGoldens() {
  SymbolLibrary lib; ensureSymbol(lib);

  // --- A: early paths (cs:30-40). Mod<2 → 0 (incl. the .t3 default Mod=1); Mod==2 → n.Mod(2). ---
  {
    std::map<std::string, StatefulValueState> st;
    expectEq("A Mod=1 n=5 (t3 default path)", cookOnce(lib, st, 5.0f, 1.0f, 0), 0.0f);
    expectEq("A Mod=0 n=7", cookOnce(lib, st, 7.0f, 0.0f, 1), 0.0f);
    expectEq("A Mod=2 n=5", cookOnce(lib, st, 5.0f, 2.0f, 2), 1.0f);
    expectEq("A Mod=2 n=-3 (negative wrap)", cookOnce(lib, st, -3.0f, 2.0f, 3), 1.0f);
    expectEq("A Mod=2 n=4", cookOnce(lib, st, 4.0f, 2.0f, 4), 0.0f);
  }
  // --- B: the XxHash chain at Mod=5, ONE instance across frames (oracle wants, cs:42-57). ---
  {
    std::map<std::string, StatefulValueState> st;
    const float ns[8]    = {0, 1, 2, 3, 17, 50, 76, 99};
    const float want[8]  = {4, 0, 1, 4, 0, 3, 2, 4};
    for (int i = 0; i < 8; ++i) {
      char buf[64];
      snprintf(buf, sizeof(buf), "B Mod=5 n=%d", (int)ns[i]);
      expectEq(buf, cookOnce(lib, st, ns[i], 5.0f, (uint32_t)i), want[i]);
    }
  }
  // --- C: modulo-change refill (cs:45): n=17 Mod=5 → 0, then Mod=7 → 4 on the SAME instance. ---
  {
    std::map<std::string, StatefulValueState> st;
    expectEq("C n=17 Mod=5", cookOnce(lib, st, 17.0f, 5.0f, 0), 0.0f);
    expectEq("C n=17 Mod=7 (refill on switch)", cookOnce(lib, st, 17.0f, 7.0f, 1), 4.0f);
  }
  // --- D: the cross-frame STATE tooth (hysteresis quirk): history(1) vs fresh(0) diverge. ---
  {
    std::map<std::string, StatefulValueState> hist;
    expectEq("D n=10 Mod=5 (window start=-25)", cookOnce(lib, hist, 10.0f, 5.0f, 0), 3.0f);
    expectEq("D n=-10 Mod=5 HISTORY (old buffer slot 90)", cookOnce(lib, hist, -10.0f, 5.0f, 1), 1.0f);
    std::map<std::string, StatefulValueState> fresh;
    expectEq("D n=-10 Mod=5 FRESH (refill start=-125)", cookOnce(lib, fresh, -10.0f, 5.0f, 0), 0.0f);
  }
}

}  // namespace

int runRandomChoiceIndexSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] randomchoiceindex (deterministic no-repeat choice on the PRODUCTION cook)\n");

  // Always run the full golden CLEAN first (production behavior must pass regardless of injectBug).
  setRandomChoiceIndexBug(0);
  runAllGoldens();
  const int cleanFail = g_fail;
  printf("[selftest] randomchoiceindex clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    // Same fixed-want goldens under the real-term corruption — the chain probes must FAIL (teeth).
    setRandomChoiceIndexBug(1);
    const int before = g_fail;
    runAllGoldens();
    printf("[selftest] randomchoiceindex bug1(drop-anti-repeat-+1) added %d failure(s)\n",
           g_fail - before);
    setRandomChoiceIndexBug(0);  // restore production
  }

  printf("[selftest] randomchoiceindex %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
