// app/frame_cook_animboolean_selftest — AnimBoolean PRODUCTION-PATH pin (--selftest-animboolean). The
// INVERSE Anim* op: no Result, its ONLY output TriggerOutput IS the cross-frame integer tooth. Same
// R-2 IRON RULE as the AnimValue/AnimInt goldens: stateful ops live on the RESIDENT cook, so this
// golden drives the REAL production seam (framecook::cookStatefulValueNodes — the exact once-per-frame
// cook run() calls), NOT a mock. It builds an AnimBoolean node, cooks it across multiple frames carrying
// the SAME StatefulValueState map (the cross-frame channel), and reads TriggerOutput(extOut[0]).
//
// ★ KEY STRUCTURAL DIFFERENCE from the AnimValue/AnimInt goldens: AnimBoolean has NO OverrideTime
// input — TiXL reads ONLY context.LocalFxTime. So the time source is the SEAM clock, and we drive it
// per frame via the `timeSecs` argument to cookStatefulValueNodes (NOT via an OverrideTime override).
// This exercises the seam's own clock path end-to-end.
//
// What it proves (hand-computed from AnimBoolean.cs: nt = time*rateFactor*rate+phase, here
// Rate=1/Phase=0/rateFactor=1 so nt = timeSecs; TriggerOutput = (int)priorNt != (int)nt):
//   The CROSS-FRAME integer tooth. timeSecs={0.6,1.2,1.8,2.3} → (int)nt={0,1,1,2}; TriggerOutput=
//   {0,1,0,1} (int 0→0, 0→1 tooth, 1→1, 1→2 tooth). The 0,1,0,1 tooth can ONLY be right if the
//   production state map carried _normalizedTime across frames (no state → 0,1,1,1).
//
// injectBug (a REAL production-term corruption via runtime setAnimBooleanBug, NOT a flipped expectation):
//   bug 1 = DROP the state write in stepAnimBoolean → _normalizedTime never advances → originalTime
//           frozen at 0 → the cross-frame tooth breaks (f2 TriggerOutput fires when it shouldn't:
//           0 vs (int)1.8=1 → 1, but the want is 0). This op has ONLY the state output, so dropping
//           the state write is the natural defect — it proves the cross-frame state write is
//           load-bearing. Expected values unchanged (no co-conditioning).
//   bug 2 = FREEZE the edge to 0 (TriggerOutput forced low, ignoring the comparison) → bites the
//           frames where the tooth SHOULD fire (f1, f3 want 1) while leaving the no-fire frames (f0,
//           f2 want 0) correct. An INDEPENDENT defect from bug 1, so the want=1 frames bite. Expected
//           unchanged.
// The selftest runs the FULL golden once clean (must pass) and, when injectBug, once under each bug
// mode (the harness asserts the same FIXED wants → they FAIL → the binary exits 1).
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"                // findSpec (AnimBoolean NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setAnimBooleanBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectClose(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-4f;
  if (!ok) { ++g_fail; printf("  [animboolean] FAIL %s got=%.6f want=%.6f\n", what, got, want); }
  else printf("  [animboolean] ok   %s = %.6f\n", what, got);
}

// Register the AnimBoolean atomic symbol into the lib (regenerate from its NodeSpec, like the bridge).
void ensureAnimBooleanSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("AnimBoolean")) lib.symbols["AnimBoolean"] = atomicSymbolFromSpec(*s);
}

// Build ONE root holding a single AnimBoolean child (id 1) with the given input overrides, then
// project it to a resident graph. AnimBoolean has NO time input — time is the seam clock.
ResidentEvalGraph buildAnim(SymbolLibrary& lib, const std::map<std::string, float>& overrides) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "AnimBoolean"; c.overrides = overrides;
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

// Cook one production frame through the REAL seam. ★ timeSecs is the SEAM clock that AnimBoolean reads
// (it has no OverrideTime) — we vary it per frame to drive nt. State + var map carried across frames.
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
               std::map<std::string, StatefulValueState>& state, uint32_t frame, float timeSecs) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  cookStatefulValueNodes(g, 1.0f / 60.0f, timeSecs, 0.0, t, frame, &lib, state, vars);
}

float resOut(const ResidentEvalGraph& g, int idx) {
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[idx] : -999.0f;
}

// The full golden body (clean or under a bug mode). Returns nothing; accumulates into g_fail.
void runAllGoldens() {
  SymbolLibrary lib; ensureAnimBooleanSymbol(lib);
  // Rate=1, Phase=0, AllowSpeedFactor=None(0) so rateFactor=1 (no var needed) → nt = timeSecs.
  const float times[4] = {0.6f, 1.2f, 1.8f, 2.3f};   // (int)nt = {0,1,1,2}
  const float wTrig[4] = {0.0f, 1.0f, 0.0f, 1.0f};   // int 0→0, 0→1(tooth), 1→1, 1→2(tooth)
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;
  for (int f = 0; f < 4; ++f) {
    ResidentEvalGraph g = buildAnim(lib, {{"Rate", 1.0f}, {"Phase", 0.0f}, {"AllowSpeedFactor", 0.0f}});
    cookFrame(lib, g, vars, state, (uint32_t)f, times[f]);
    char buf[96];
    snprintf(buf, sizeof(buf), "f%d TriggerOutput(t=%.2f)", f, times[f]);
    expectClose(buf, resOut(g, 0), wTrig[f]);
  }
}

}  // namespace

int runAnimBooleanSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] animboolean (Anim* edge-only: AnimBoolean TriggerOutput on the PRODUCTION cook)\n");

  // Always run the full golden CLEAN first (production behavior must pass regardless of injectBug).
  setAnimBooleanBug(0);
  runAllGoldens();
  const int cleanFail = g_fail;
  printf("[selftest] animboolean clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    // Run the SAME fixed-want golden under each real-term corruption — they must FAIL (the teeth).
    // bug 1 (drop state write) breaks the cross-frame tooth (f2 fires when it shouldn't).
    setAnimBooleanBug(1);
    int beforeBug1 = g_fail;
    runAllGoldens();
    printf("[selftest] animboolean bug1(drop-state-write) added %d failure(s)\n", g_fail - beforeBug1);
    // bug 2 (freeze edge to 0) bites the fire-frames (f1, f3 want 1).
    setAnimBooleanBug(2);
    int beforeBug2 = g_fail;
    runAllGoldens();
    printf("[selftest] animboolean bug2(freeze-edge-low) added %d failure(s)\n", g_fail - beforeBug2);
    setAnimBooleanBug(0);  // restore production
  }

  printf("[selftest] animboolean %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
