// app/frame_cook_animint_selftest — AnimInt PRODUCTION-PATH pin (--selftest-animint). The integer
// sibling of the AnimValue golden, on the same R-2 IRON RULE: stateful ops live on the RESIDENT cook,
// so this golden drives the REAL production seam (framecook::cookStatefulValueNodes — the exact
// once-per-frame cook run() calls), NOT a mock. It builds an AnimInt node, cooks it across multiple
// frames carrying the SAME StatefulValueState map (the cross-frame channel), and reads Result(extOut[0])
// + WasHit(extOut[1]) by resident path.
//
// What it proves (hand-computed from AnimInt.cs: nt = OverrideTime*rateFactor*rate+phase, here
// Rate=1/Phase=0/rateFactor=1 so nt = OverrideTime; Result = modulo!=0 ? (int)nt .Mod modulo : (int)nt;
// WasHit = (int)priorNt != (int)nt):
//   A/B  Modulo=0: Result = (int)nt; WasHit = the CROSS-FRAME integer tooth. ot={0.6,1.2,1.8,2.3} →
//        (int)nt={0,1,1,2}; WasHit={0,1,0,1} (int 0→0, 0→1 tooth, 1→1, 1→2 tooth). The 0,1,0,1 tooth
//        can ONLY be right if the production state map carried _normalizedTime across frames (no state
//        → 0,1,1,1). Result here is the bare integer truncation.
//   C    POSITIVE modulo at NEGATIVE time — proves .Mod (MathUtils.cs:273) wraps into [0,modulo),
//        NOT C `%` (which keeps the dividend's sign). Modulo=3, ot={-0.5,-1.5,-3.5,-0.5} →
//        (int)nt={0,-1,-3,0}; posMod→Result={0,2,0,0} (C `%` would give {0,-1,0,0}). This proves the
//        positive-modulo path is actually invoked.
//   D    POSITIVE modulo wraps a positive ramp: Modulo=3, ot={0.5,3.5,6.5,9.5} → (int)nt={0,3,6,9};
//        posMod(_,3)→Result={0,0,0,0} (raw would be {0,3,6,9}). Proves modulo!=0 actually wraps.
//
// injectBug (a REAL production-term corruption via runtime setAnimIntBug, NOT a flipped expectation):
//   bug 1 = DROP the state write in stepAnimInt → _normalizedTime never advances → originalTime frozen
//           at 0 → the cross-frame WasHit tooth breaks (A/B f2 WasHit fires when it shouldn't). Result
//           is UNAFFECTED (Result is pure) — so bug 1 bites ONLY the state output, proving the state
//           write is load-bearing. Expected values unchanged (no co-conditioning).
//   bug 2 = DROP the positive-modulo wrap → Result becomes the raw (int)nt → the C/D modulo goldens
//           bite (C f1: 2→-1; D: {0,0,0,0}→{0,3,6,9}) while WasHit stays correct. Expected unchanged.
// The selftest runs the FULL golden once clean (must pass) and, when injectBug, once under each bug
// mode (the harness asserts the same FIXED wants → they FAIL → the binary exits 1).
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"                // findSpec (AnimInt NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setAnimIntBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectClose(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-4f;
  if (!ok) { ++g_fail; printf("  [animint] FAIL %s got=%.6f want=%.6f\n", what, got, want); }
  else printf("  [animint] ok   %s = %.6f\n", what, got);
}

// Register the AnimInt atomic symbol into the lib (regenerate from its NodeSpec, like the bridge).
void ensureAnimIntSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("AnimInt")) lib.symbols["AnimInt"] = atomicSymbolFromSpec(*s);
}

// Build ONE root holding a single AnimInt child (id 1) with the given input overrides, then project
// it to a resident graph. Inputs are Constant overrides (the float rail). OverrideTime is set per
// frame to deterministically drive the time source (the named single-clock fork).
ResidentEvalGraph buildAnim(SymbolLibrary& lib, const std::map<std::string, float>& overrides) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "AnimInt"; c.overrides = overrides;
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

// Cook one production frame through the REAL seam (state + var map carried across frames).
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
               std::map<std::string, StatefulValueState>& state, uint32_t frame) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  // timeSecs=0: AnimInt's time comes from its nonzero OverrideTime override (named single-clock
  // fork), so we control time per node without varying the seam's global clock.
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars);
}

float resOut(const ResidentEvalGraph& g, int idx) {
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[idx] : -999.0f;
}

// Drive the 4-frame Result+WasHit trajectory for a given AnimInt config and assert the FIXED wants.
// `wantResult`/`wantWasHit` are hand-computed from the TiXL formula and are INDEPENDENT of any bug
// mode (the bug breaks the live cook; these wants never move).
void runTrajectory(const char* label, float modulo, const float overrideTimes[4],
                   const float wantResult[4], const float wantWasHit[4]) {
  SymbolLibrary lib; ensureAnimIntSymbol(lib);
  // Rate=1, Phase=0, AllowSpeedFactor=None(0) so rateFactor=1 (no var needed) → nt = OverrideTime.
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;
  for (int f = 0; f < 4; ++f) {
    // Rebuild the child each frame with the new OverrideTime (Constant override), but carry state.
    ResidentEvalGraph g = buildAnim(lib, {{"Modulo", modulo}, {"Rate", 1.0f}, {"Phase", 0.0f},
                                          {"AllowSpeedFactor", 0.0f},
                                          {"OverrideTime", overrideTimes[f]}});
    cookFrame(lib, g, vars, state, (uint32_t)f);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s f%d Result(t=%.2f)", label, f, overrideTimes[f]);
    expectClose(buf, resOut(g, 0), wantResult[f]);
    snprintf(buf, sizeof(buf), "%s f%d WasHit", label, f);
    expectClose(buf, resOut(g, 1), wantWasHit[f]);
  }
}

// The full golden body (clean or under a bug mode). Returns nothing; accumulates into g_fail.
void runAllGoldens() {
  // --- A/B: Modulo=0, the bare integer ramp + the cross-frame WasHit tooth. ---
  {
    const float ot[4] = {0.6f, 1.2f, 1.8f, 2.3f};
    const float wR[4] = {0.0f, 1.0f, 1.0f, 2.0f};   // (int)nt
    const float wH[4] = {0.0f, 1.0f, 0.0f, 1.0f};   // int 0→0, 0→1(tooth), 1→1, 1→2(tooth)
    runTrajectory("A/B mod0", 0.0f, ot, wR, wH);
  }
  // --- C: POSITIVE modulo at NEGATIVE time (proves .Mod wraps, not C `%`). Modulo=3. ---
  {
    const float ot[4] = {-0.5f, -1.5f, -3.5f, -0.5f};   // (int)nt = {0,-1,-3,0}
    const float wR[4] = {0.0f, 2.0f, 0.0f, 0.0f};       // posMod: 0,(-1→2),(-3→0),0  (C `%`: 0,-1,0,0)
    const float wH[4] = {0.0f, 1.0f, 1.0f, 1.0f};       // int 0→0, 0→-1, -1→-3, -3→0 (all cross)
    runTrajectory("C posmod-neg", 3.0f, ot, wR, wH);
  }
  // --- D: POSITIVE modulo wraps a positive ramp. Modulo=3, ot=multiples of ~3 → all wrap to 0. ---
  {
    const float ot[4] = {0.5f, 3.5f, 6.5f, 9.5f};       // (int)nt = {0,3,6,9}
    const float wR[4] = {0.0f, 0.0f, 0.0f, 0.0f};       // posMod(_,3) = 0  (raw would be 0,3,6,9)
    const float wH[4] = {0.0f, 1.0f, 1.0f, 1.0f};       // int 0→0, 0→3, 3→6, 6→9 (all cross)
    runTrajectory("D modwrap-pos", 3.0f, ot, wR, wH);
  }
}

}  // namespace

int runAnimIntSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] animint (Anim* integer sibling: AnimInt Result+WasHit on the PRODUCTION cook)\n");

  // Always run the full golden CLEAN first (production behavior must pass regardless of injectBug).
  setAnimIntBug(0);
  runAllGoldens();
  const int cleanFail = g_fail;
  printf("[selftest] animint clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    // Run the SAME fixed-want goldens under each real-term corruption — they must FAIL (the teeth).
    // bug 1 (drop state write) bites the cross-frame WasHit tooth (A/B f2 WasHit), Result untouched.
    setAnimIntBug(1);
    int beforeBug1 = g_fail;
    runAllGoldens();
    printf("[selftest] animint bug1(drop-state-write) added %d failure(s)\n", g_fail - beforeBug1);
    // bug 2 (drop modulo wrap) bites the C/D modulo Result goldens, WasHit untouched.
    setAnimIntBug(2);
    int beforeBug2 = g_fail;
    runAllGoldens();
    printf("[selftest] animint bug2(drop-modulo-wrap) added %d failure(s)\n", g_fail - beforeBug2);
    setAnimIntBug(0);  // restore production
  }

  printf("[selftest] animint %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
