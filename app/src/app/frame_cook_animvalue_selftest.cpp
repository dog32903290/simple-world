// app/frame_cook_animvalue_selftest — AnimValue PRODUCTION-PATH pin (--selftest-animvalue). The
// canonical proof-of-pattern for the stateful-value Anim* family. R-2 IRON RULE: stateful ops live on
// the RESIDENT cook, so the golden drives the REAL production seam (framecook::cookStatefulValueNodes —
// the exact once-per-frame cook run() calls), NOT a mock. It builds an AnimValue node, cooks it across
// multiple frames carrying the SAME StatefulValueState map (the cross-frame channel), and reads
// Result(extOut[0]) + WasHit(extOut[1]) by resident path.
//
// What it proves:
//   A Result is the PURE AnimMath value. Shape=Ramps(1), Rate/Ratio/Amplitude=1, Phase/Offset=0,
//     Bias=0.5 → Result = Fmod(nt,1) (SchlickBias is the identity at bias 0.5). Driven via OverrideTime
//     per frame (the seam can't vary timeSecs per node, so we use the OverrideTime input — the named
//     single-clock fork makes nonzero OverrideTime the time source). Hand-computed from AnimValue.cs.
//   B WasHit is the CROSS-FRAME integer tooth: WasHit = (int)priorNormalizedTime != (int)nt. Frame at
//     t=0.6 → WasHit=0 (int 0→0); next frame at t=1.2 → WasHit=1 (int 0→1 crosses the boundary); t=1.8
//     → 0 (1→1); t=2.3 → 1 (1→2). This is THE state-dependent output, and it can only be right if the
//     production state map carried _normalizedTime across frames.
//   C Bias is real: bias=0.25 bends Result(t=0.6) from 0.6 to SchlickBias(0.6,0.25)=1/3 (proves the
//     AnimMath shape engine is actually invoked, not a passthrough).
//   D Amplitude+Offset: amplitude=2, offset=10 on the bias-0.5 ramp t=0.6 → 0.6*2+10 = 11.2.
//
// injectBug (a REAL production-term corruption via runtime setAnimValueBug, NOT a flipped expectation):
//   bug 1 = DROP the state write in stepAnimValue → _normalizedTime never advances → the t=1.2 frame's
//           originalTime stays 0... but so does every later frame, so the cross-frame WasHit tooth at
//           t=1.8/t=2.3 breaks (originalTime frozen at 0 → WasHit fires when it shouldn't / vice versa).
//           The Result golden is UNAFFECTED (Result is pure) — so bug 1 bites ONLY the state output,
//           proving the state write is load-bearing. Expected values are unchanged (no co-conditioning).
//   bug 2 = DROP the AnimMath call → Result becomes the raw normalizedTime → the Result/Bias/Amp goldens
//           bite while WasHit stays correct. Expected values are unchanged.
// The selftest runs the FULL golden once clean (must pass) and, when injectBug, once under each bug
// mode (the harness asserts the same FIXED wants → they FAIL → the binary exits 1).
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"                // findSpec (AnimValue NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setAnimValueBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectClose(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-4f;
  if (!ok) { ++g_fail; printf("  [animvalue] FAIL %s got=%.6f want=%.6f\n", what, got, want); }
  else printf("  [animvalue] ok   %s = %.6f\n", what, got);
}

// Register the AnimValue atomic symbol into the lib (regenerate from its NodeSpec, like the bridge).
void ensureAnimValueSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("AnimValue")) lib.symbols["AnimValue"] = atomicSymbolFromSpec(*s);
}

// Build ONE root holding a single AnimValue child (id 1) with the given input overrides, then
// project it to a resident graph. Inputs are Constant overrides (the float rail) — the same channel
// the inspector writes. OverrideTime is set per frame to deterministically drive the time source.
ResidentEvalGraph buildAnim(SymbolLibrary& lib, const std::map<std::string, float>& overrides) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "AnimValue"; c.overrides = overrides;
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
  // timeSecs=0: AnimValue's time comes from its nonzero OverrideTime override (named single-clock
  // fork), so we control time per node without varying the seam's global clock. runTimeSecs unused.
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars);
}

float resOut(const ResidentEvalGraph& g, int idx) {
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[idx] : -999.0f;
}

// Drive the 4-frame Result+WasHit trajectory for a given AnimValue config and assert the FIXED wants.
// `wantResult`/`wantWasHit` are computed from the TiXL formula (hand-computed in the python check), and
// are INDEPENDENT of any bug mode (the bug breaks the live cook; these wants never move).
void runTrajectory(const char* label, float bias, float amplitude, float offset,
                   const float overrideTimes[4], const float wantResult[4], const float wantWasHit[4]) {
  SymbolLibrary lib; ensureAnimValueSymbol(lib);
  // Shape=Ramps(1), Rate=Ratio=1, Phase=0, AllowSpeedFactor=None(0) so rateFactor=1 (no var needed).
  ResidentEvalGraph g = buildAnim(lib, {{"Shape", 1.0f}, {"Rate", 1.0f}, {"Ratio", 1.0f},
                                        {"Phase", 0.0f}, {"Amplitude", amplitude}, {"Offset", offset},
                                        {"Bias", bias}, {"AllowSpeedFactor", 0.0f},
                                        {"OverrideTime", overrideTimes[0]}});
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;
  for (int f = 0; f < 4; ++f) {
    // Rebuild the child each frame with the new OverrideTime (Constant override), but carry state.
    g = buildAnim(lib, {{"Shape", 1.0f}, {"Rate", 1.0f}, {"Ratio", 1.0f}, {"Phase", 0.0f},
                        {"Amplitude", amplitude}, {"Offset", offset}, {"Bias", bias},
                        {"AllowSpeedFactor", 0.0f}, {"OverrideTime", overrideTimes[f]}});
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
  // --- A+B: the canonical Ramps trajectory + the cross-frame WasHit tooth. ---
  // nt = OverrideTime (Rate/Ratio=1, Phase=0, rateFactor=1). Bias=0.5 → SchlickBias identity →
  // Result = Fmod(nt,1). WasHit = (int)priorNt != (int)nt.
  {
    const float ot[4]   = {0.6f, 1.2f, 1.8f, 2.3f};
    const float wR[4]   = {0.6f, 0.2f, 0.8f, 0.3f};   // Fmod(nt,1)
    const float wH[4]   = {0.0f, 1.0f, 0.0f, 1.0f};   // int 0→0, 0→1(tooth), 1→1, 1→2(tooth)
    runTrajectory("A/B ramps", 0.5f, 1.0f, 0.0f, ot, wR, wH);
  }
  // --- C: Bias is real (the AnimMath shape engine is actually invoked). bias=0.25 bends 0.6 → 1/3. ---
  {
    const float ot[4] = {0.6f, 0.6f, 0.6f, 0.6f};       // hold t=0.6 (WasHit=0 after frame 1)
    const float r = 1.0f / 3.0f;                         // SchlickBias(0.6, 0.25)
    const float wR[4] = {r, r, r, r};
    const float wH[4] = {0.0f, 0.0f, 0.0f, 0.0f};        // (int)0.6 never crosses → no tooth
    runTrajectory("C bias0.25", 0.25f, 1.0f, 0.0f, ot, wR, wH);
  }
  // --- D: Amplitude + Offset. amp=2, off=10 on the bias-0.5 ramp t=0.6 → 0.6*2+10 = 11.2. ---
  {
    const float ot[4] = {0.6f, 0.6f, 0.6f, 0.6f};
    const float wR[4] = {11.2f, 11.2f, 11.2f, 11.2f};
    const float wH[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    runTrajectory("D amp2off10", 0.5f, 2.0f, 10.0f, ot, wR, wH);
  }
}

}  // namespace

int runAnimValueSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] animvalue (Anim* foundation: AnimValue Result+WasHit on the PRODUCTION cook)\n");

  // Always run the full golden CLEAN first (production behavior must pass regardless of injectBug).
  setAnimValueBug(0);
  runAllGoldens();
  const int cleanFail = g_fail;
  printf("[selftest] animvalue clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    // Run the SAME fixed-want goldens under each real-term corruption — they must FAIL (the teeth).
    // bug 1 (drop state write) bites the cross-frame WasHit tooth (A/B f2/f3 WasHit), Result untouched.
    setAnimValueBug(1);
    int beforeBug1 = g_fail;
    runAllGoldens();
    printf("[selftest] animvalue bug1(drop-state-write) added %d failure(s)\n", g_fail - beforeBug1);
    // bug 2 (drop AnimMath call) bites every Result/Bias/Amp golden, WasHit untouched.
    setAnimValueBug(2);
    int beforeBug2 = g_fail;
    runAllGoldens();
    printf("[selftest] animvalue bug2(drop-animmath-call) added %d failure(s)\n", g_fail - beforeBug2);
    setAnimValueBug(0);  // restore production
  }

  printf("[selftest] animvalue %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
