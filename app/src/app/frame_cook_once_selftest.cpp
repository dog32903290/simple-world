// app/frame_cook_once_selftest — Once PRODUCTION-PATH pin (--selftest-once). Drives the REAL
// production seam (framecook::cookStatefulValueNodes — the exact once-per-frame cook run() drives),
// NOT a mock, across multi-frame Trigger sequences carrying ONE StatefulValueState map (the
// production cross-frame contract). The WasTrigger harness pattern.
//
// Graph: 1 = Once(Trigger=<per-frame Constant override>). Stable child id so state["1"] survives the
// per-frame rebuild (the animvalue/wastrigger precedent for driving a per-frame-changing input).
//
// Frame-by-frame expectation (TiXL Once.cs:23-34, OutputTrigger = Trigger.DirtyFlag.IsDirty with the
// self-clearing latch, modeled as the named value-edge fork — see stateful_value_ops_flow.cpp):
//   Sequence A (Trigger 0,0,1,1,0):
//     f0 → 1  (initial-dirty: a fresh instance's input flag starts dirty → first cook fires)
//     f1 → 0  (no change → latch cleared, stays low)
//     f2 → 1  (0→1 change fires ONCE)
//     f3 → 0  (held 1 → no re-fire — THE once-ness; a level-triggered impl emits 1 here)
//     f4 → 1  (1→0 change ALSO fires — any set marks the TiXL flag dirty, direction-less)
//   Sequence B (Trigger 1,1): f0 → 1 (initial-dirty), f1 → 0 (held → cleared). Kills an impl that
//     models "fire while true" instead of the latch.
//
// TEETH (setOnceBug, sticky module switch flipped around the REAL cook; wants NEVER move):
//   bug 1 = DROP the state write → the latch never primes → stuck HIGH → A f1/f3 (want 0) bite.
//   bug 2 = FREEZE the edge low → A f0/f2/f4 (want 1) bite (the complementary tooth).
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/graph.h"                // findSpec (Once NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setOnceBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [once] FAIL %s\n", what); }
  else printf("  [once] ok   %s\n", what);
}

// Build the 1-node graph for one frame: Once(Trigger=triggerVal) as child id 1.
ResidentEvalGraph buildFrame(SymbolLibrary& lib, float triggerVal) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "Once";
  c.overrides["Trigger"] = triggerVal;
  root.children = {c}; root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

// Run an n-frame Trigger sequence through the REAL seam; OutputTrigger per frame into out[].
void runSequence(int bug, const float* triggers, int n, float* out) {
  setOnceBug(bug);
  SymbolLibrary lib;
  if (const NodeSpec* s = findSpec("Once")) lib.symbols["Once"] = atomicSymbolFromSpec(*s);
  ContextVarMap vars;
  std::map<std::string, StatefulValueState> state;
  for (int f = 0; f < n; ++f) {
    ResidentEvalGraph g = buildFrame(lib, triggers[f]);
    Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
    cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, (uint32_t)f, &lib, state, vars);
    const ResidentNode* node = g.node("1");
    out[f] = node ? node->extOut[0] : -999.0f;
  }
  setOnceBug(0);  // reset the sticky switch
}

bool near01(float got, float want) { return std::fabs(got - want) < 1e-5f; }

}  // namespace

int runOnceSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] once (Once trigger latch: fire exactly once per Trigger change, REAL cook seam)\n");

  const float seqA[5] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f};
  const float wantA[5] = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
  const float seqB[2] = {1.0f, 1.0f};
  const float wantB[2] = {1.0f, 0.0f};

  // ===== Production legs (bug 0) — must be green regardless of injectBug. =====================
  {
    float out[5];
    runSequence(0, seqA, 5, out);
    expect("A f0: fresh instance fires (initial dirty)", near01(out[0], wantA[0]));
    expect("A f1: unchanged 0 stays low (latch cleared)", near01(out[1], wantA[1]));
    expect("A f2: 0->1 fires once", near01(out[2], wantA[2]));
    expect("A f3: held 1 must NOT re-fire (once-ness)", near01(out[3], wantA[3]));
    expect("A f4: 1->0 change fires too (direction-less dirty)", near01(out[4], wantA[4]));
    float outB[2];
    runSequence(0, seqB, 2, outB);
    expect("B f0: starts-true fires once", near01(outB[0], wantB[0]));
    expect("B f1: held true stays low (not level-triggered)", near01(outB[1], wantB[1]));
  }

  // ===== TEETH legs (only under --selftest-once-bug): fixed wants must bite. ==================
  if (injectBug) {
    float b1[5];
    runSequence(/*bug=*/1, seqA, 5, b1);  // drop the state write → never primes → stuck HIGH
    expect("bug1: A f1 must stay low (state write load-bearing)", near01(b1[1], wantA[1]));
    expect("bug1: A f3 must stay low (latch primes)", near01(b1[3], wantA[3]));
    float b2[5];
    runSequence(/*bug=*/2, seqA, 5, b2);  // freeze the edge low → fire frames die
    expect("bug2: A f0 must fire (edge not frozen)", near01(b2[0], wantA[0]));
    expect("bug2: A f2 must fire on 0->1", near01(b2[2], wantA[2]));
  }

  printf("[selftest] once %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
