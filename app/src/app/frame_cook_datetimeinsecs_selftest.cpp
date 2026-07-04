// app/frame_cook_datetimeinsecs_selftest — DateTimeInSecs PRODUCTION-PATH pin (--selftest-datetimeinsecs).
// R-2 IRON RULE: stateful ops live on the RESIDENT cook, so the golden drives the REAL production seam
// (cookStatefulValueNodes), NOT a mock. DateTimeInSecs is a WALL-CLOCK op, so its ABSOLUTE value is not
// a parity oracle; the STATEFUL invariant worth a golden is the Freeze LATCH. We inject a deterministic
// clock (setDateTimeInSecsClockOverride) so the latch invariant is a FIXED, bug-independent oracle.
//
// TiXL authority: DateTimeInSecs.cs:14-24 — if(!Freeze) _lastValue = (int)Now.Unix; Result = _lastValue.
//
// What it proves (all values FIXED, computed from the injected clock — independent of any bug mode):
//   A Unfrozen tracks the clock: clock=1000 → Result=1000; clock advances to 2000 (Freeze=false) → 2000.
//   B Freeze LATCHES: cook at clock=1000 (Freeze=false) → 1000; then set clock=5000 but Freeze=true →
//     Result STAYS 1000 (the latch held the prior sample across the frame). Unfreeze at clock=5000 → 5000.
//     This is the cross-frame state invariant — only right if _lastValue persisted through the frozen cook.
//
// injectBug (setDateTimeInSecsBug — a REAL production-gate corruption, NOT a flipped expectation):
//   bug 1 = IGNORE the Freeze gate → the frozen cook re-samples the (injected) clock → Result becomes
//           5000 where the FIXED want is 1000 → golden B bites. The dead-gate defect. Wants unchanged.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/transport.h"

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectEq(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 0.5f;  // int-valued; 0.5 tolerance
  if (!ok) { ++g_fail; printf("  [datetimeinsecs] FAIL %s got=%.1f want=%.1f\n", what, got, want); }
  else printf("  [datetimeinsecs] ok   %s = %.1f\n", what, got);
}

void ensureSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("DateTimeInSecs")) lib.symbols["DateTimeInSecs"] = atomicSymbolFromSpec(*s);
}

ResidentEvalGraph buildFrame(SymbolLibrary& lib, bool freeze) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "DateTimeInSecs"; c.overrides["Freeze"] = freeze ? 1.0f : 0.0f;
  root.children = {c}; root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

float cookAt(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
             std::map<std::string, StatefulValueState>& state, uint32_t frame) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars, 0);
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[0] : -999.0f;
}

// The full golden body. state carries across the 4 cooks (the latch channel).
void runAll() {
  SymbolLibrary lib; ensureSymbol(lib);
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;

  // A: unfrozen tracks the injected clock.
  setDateTimeInSecsClockOverride(1000);
  { ResidentEvalGraph g = buildFrame(lib, false); expectEq("A f0 unfrozen@1000", cookAt(lib, g, vars, state, 0), 1000.0f); }
  setDateTimeInSecsClockOverride(2000);
  { ResidentEvalGraph g = buildFrame(lib, false); expectEq("A f1 unfrozen@2000", cookAt(lib, g, vars, state, 1), 2000.0f); }

  // B: Freeze LATCHES the prior sample across the frame. Re-seed to a known value first.
  setDateTimeInSecsClockOverride(1000);
  { ResidentEvalGraph g = buildFrame(lib, false); expectEq("B f2 sample@1000", cookAt(lib, g, vars, state, 2), 1000.0f); }
  setDateTimeInSecsClockOverride(5000);  // clock jumps, but...
  { ResidentEvalGraph g = buildFrame(lib, true); expectEq("B f3 FROZEN holds 1000", cookAt(lib, g, vars, state, 3), 1000.0f); }
  { ResidentEvalGraph g = buildFrame(lib, false); expectEq("B f4 unfreeze@5000", cookAt(lib, g, vars, state, 4), 5000.0f); }
}

}  // namespace

int runDateTimeInSecsSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] datetimeinsecs (DateTimeInSecs Freeze latch on the PRODUCTION cook)\n");

  setDateTimeInSecsBug(0);
  runAll();
  const int cleanFail = g_fail;
  printf("[selftest] datetimeinsecs clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    setDateTimeInSecsBug(1);  // ignore Freeze gate → the frozen frame re-samples → golden B bites
    int before = g_fail;
    runAll();
    printf("[selftest] datetimeinsecs bug1(ignore-freeze-gate) added %d failure(s)\n", g_fail - before);
    setDateTimeInSecsBug(0);
  }

  setDateTimeInSecsClockOverride(-1);  // restore the real OS clock (production)
  printf("[selftest] datetimeinsecs %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
