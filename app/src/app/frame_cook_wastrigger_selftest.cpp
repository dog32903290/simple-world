// app/frame_cook_wastrigger_selftest — WasTrigger PRODUCTION-PATH pin (--selftest-wastrigger).
// Drives the REAL production seam (framecook::cookStatefulValueNodes — the exact 3-phase
// clear→writers→readers cook run() drives every frame), NOT a mock, across a multi-frame trigger
// sequence carrying ONE ContextVarMap + ONE StatefulValueState map (the production contract).
//
// Graph (stable child ids so state[path] survives the per-frame rebuild):
//   1 = SetFloatVar("__TriggerA")  — the WRITER (runs in pass 1), FloatValue changes each frame.
//   2 = WasTrigger(Trigger=TriggerA) — the READER (runs in pass 2), reads floatVars["__TriggerA"].
// The value crosses the SAME shared floatVars channel the contextvar seam proves (anti-orphan): the
// writer's FloatValue → ContextVarMap.floatVars["__TriggerA"] → WasTrigger reads it → rising-edge gate.
//
// Frame-by-frame expectation (TiXL WasTrigger.cs: increased = value > _lastValue;
//   triggered = WasTriggered(increased, ref _wasHit) = increased && !_wasHit; then _wasHit = increased):
//   f0 __TriggerA=0.0  value=0   lastValue=0(init) increased=F            → WasTriggered=0
//   f1 __TriggerA=0.5  value=0.5 lastValue=0       increased=T _wasHit:F  → WasTriggered=1 (rising edge)
//   f2 __TriggerA=0.8  value=0.8 lastValue=0.5     increased=T _wasHit:T  → WasTriggered=0 (held rise, no re-pulse)
//   f3 __TriggerA=0.3  value=0.3 lastValue=0.8     increased=F            → WasTriggered=0 (decrease → re-arm)
//   f4 __TriggerA=0.9  value=0.9 lastValue=0.3     increased=T _wasHit:F  → WasTriggered=1 (re-pulse after arm)
//
// TEETH (setWasTriggerBug, sticky module switch flipped around the REAL cook):
//   bug 1 = DROP the _wasHit state write → the held-rise frame f2 RE-PULSES (gate stuck low) →
//           f2 expected 0 but the broken cook emits 1 → FAIL (the cross-frame edge gate is load-bearing).
//   bug 2 = DROP the var read → value forced 0 → the rising-edge frame f1 NEVER fires →
//           f1 expected 1 but the broken cook emits 0 → FAIL (the floatVars channel is load-bearing).
// Expectations stay CORRECT in both legs (RED reflects a real defect, not a flipped want).
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"                // findSpec (WasTrigger / SetFloatVar NodeSpecs)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // ContextVarMap / StatefulValueState / setWasTriggerBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [wastrigger] FAIL %s\n", what); }
  else printf("  [wastrigger] ok   %s\n", what);
}

void ensureSymbols(SymbolLibrary& lib) {
  for (const char* t : {"SetFloatVar", "WasTrigger"})
    if (const NodeSpec* s = findSpec(t)) lib.symbols[t] = atomicSymbolFromSpec(*s);
}

// Build the 2-node graph for one frame: SetFloatVar("__TriggerA")=triggerVal, WasTrigger(TriggerA).
// Stable child ids (1=Set, 2=WasTrigger) so state[path] carries across the per-frame rebuild.
ResidentEvalGraph buildFrame(SymbolLibrary& lib, float triggerVal) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild setA; setA.id = 1; setA.symbolId = "SetFloatVar";
  setA.strOverrides["VariableName"] = "__TriggerA";        // real string sub-seam channel
  setA.overrides["FloatValue"] = triggerVal;
  SymbolChild wt; wt.id = 2; wt.symbolId = "WasTrigger";
  wt.overrides["Trigger"] = 1.0f;                          // 1 = TriggerA (reads "__TriggerA")
  root.children = {setA, wt}; root.nextChildId = 3;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

float extOut0(const ResidentEvalGraph& g, const char* path) {
  const ResidentNode* n = g.node(path);
  return n ? n->extOut[0] : -999.0f;
}

// Cook one frame through the REAL seam, carrying vars+state across frames.
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
               std::map<std::string, StatefulValueState>& state, uint32_t frame) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars, /*ctxVarBug=*/0);
}

// Run the 5-frame sequence; returns WasTriggered for each frame into out[5]. bug = the teeth mode.
void runSequence(int bug, float out[5]) {
  setWasTriggerBug(bug);
  const float vals[5] = {0.0f, 0.5f, 0.8f, 0.3f, 0.9f};
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;
  SymbolLibrary lib; ensureSymbols(lib);
  for (uint32_t f = 0; f < 5; ++f) {
    ResidentEvalGraph g = buildFrame(lib, vals[f]);
    cookFrame(lib, g, vars, state, f);
    out[f] = extOut0(g, "2");
  }
  setWasTriggerBug(0);  // reset the sticky switch
}

}  // namespace

int runWasTriggerSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] wastrigger (WasTrigger rising-edge pulse on a named trigger var, REAL cook seam)\n");

  // ===== Production sequence: the rising-edge pulse train (bug 0). ==========================
  {
    float out[5];
    runSequence(/*bug=*/0, out);
    expect("f0: __TriggerA=0.0 → WasTriggered=0 (no rise from init 0)",  std::fabs(out[0] - 0.0f) < 1e-5f);
    expect("f1: __TriggerA 0→0.5 → WasTriggered=1 (rising edge fires)",  std::fabs(out[1] - 1.0f) < 1e-5f);
    expect("f2: __TriggerA 0.5→0.8 → WasTriggered=0 (held rise, no re-pulse)", std::fabs(out[2] - 0.0f) < 1e-5f);
    expect("f3: __TriggerA 0.8→0.3 → WasTriggered=0 (decrease re-arms gate)", std::fabs(out[3] - 0.0f) < 1e-5f);
    expect("f4: __TriggerA 0.3→0.9 → WasTriggered=1 (re-pulse after arm)", std::fabs(out[4] - 1.0f) < 1e-5f);
  }

  // ===== TEETH leg (only when injectBug): bug 1 (drop state write) + bug 2 (drop var read). ====
  // These corrupt a REAL production term; the FIXED expectations above bite. Run inside injectBug so
  // the --bite harness flips them; the production leg always runs (a normal --selftest run stays GREEN).
  if (injectBug) {
    // bug 1: the _wasHit gate never advances → the HELD-rise frame f2 re-pulses (expected 0, gets 1).
    float b1[5];
    runSequence(/*bug=*/1, b1);
    expect("bug1: f2 held-rise must NOT re-pulse (gate load-bearing)", std::fabs(b1[2] - 0.0f) < 1e-5f);
    // bug 2: value forced 0 → the rising-edge frame f1 never fires (expected 1, gets 0).
    float b2[5];
    runSequence(/*bug=*/2, b2);
    expect("bug2: f1 rising edge must fire (floatVars channel load-bearing)", std::fabs(b2[1] - 1.0f) < 1e-5f);
  }

  printf("[selftest] wastrigger %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
