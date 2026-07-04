// app/frame_cook_delayboolean_selftest — DelayBoolean PRODUCTION-PATH pin (--selftest-delayboolean).
// Drives the REAL production seam (framecook::cookStatefulValueNodes — the exact cook run() drives every
// frame), NOT a mock, across a multi-frame Trigger sequence carrying ONE StatefulValueState map (the
// production contract). The cross-frame Queue<bool> (StatefulValueState::boolQueue) is the whole point,
// so the SAME node (stable child id) is cooked frame after frame with state[path] persisting.
//
// Graph (stable child id so state[path] survives the per-frame rebuild):
//   1 = DelayBoolean(FrameCount=2), Trigger changes each frame.
//
// TiXL authority (DelayBoolean.cs): output at frame N = Trigger from frame N-(FrameCount+1); before the
// queue fills, result is the initial false. HAND-TRACE FrameCount=2, Trigger seq [1,0,0,1,0] (see the
// leaf header stateful_value_ops_delayboolean.cpp for the queue-state walk):
//   f0 Trigger=1 → DelayedTrigger=0 (queue filling: q=[1])
//   f1 Trigger=0 → DelayedTrigger=0 (q=[1,0])
//   f2 Trigger=0 → DelayedTrigger=0 (q=[1,0,0])
//   f3 Trigger=1 → DelayedTrigger=1 (dequeue f0's 1; q=[0,0,1])   ★ = Trigger@f0, delay of 3 frames
//   f4 Trigger=0 → DelayedTrigger=0 (dequeue f1's 0; q=[0,1,0])   ★ = Trigger@f1
//
//   The f3 result=1 is the LOAD-BEARING assertion: DelayedTrigger at f3 equals Trigger at f0 ONLY if the
//   queue PERSISTED across all 4 frames. probe坐發散中段 — f3 is where the delayed 1 first emerges (not an
//   identity/all-zero frame), and it differs from the immediate Trigger (f3 Trigger=1 but that 1 is NOT
//   why the output is 1 — the output 1 is f0's, proven by f4 where Trigger=0 yet the mechanism still
//   carries f1's 0). A broken delay (no persistence / wrong depth) diverges at f3.
//
// TEETH (setDelayBooleanBug, sticky module switch flipped around the REAL cook, expectations stay FIXED):
//   bug 1 = DROP the queue persistence (enqueue/dequeue on a FRESH empty queue each frame) → the delay
//           never materializes → f3 emits the initial/immediate value, NOT f0's delayed 1 → f3 expected 1
//           but the broken cook emits 0 → FAIL (the cross-frame queue is load-bearing). This is a REAL
//           cook-path corruption (the fork severs st.boolQueue), NOT a flipped want.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/graph.h"                // findSpec (DelayBoolean NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / setDelayBooleanBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [delayboolean] FAIL %s\n", what); }
  else printf("  [delayboolean] ok   %s\n", what);
}

void ensureSymbols(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("DelayBoolean")) lib.symbols["DelayBoolean"] = atomicSymbolFromSpec(*s);
}

// Build the 1-node graph for one frame: DelayBoolean(FrameCount=2, Trigger=triggerVal). Stable child id
// (1=DelayBoolean) so state[path="1"] carries the boolQueue across the per-frame rebuild.
ResidentEvalGraph buildFrame(SymbolLibrary& lib, float triggerVal) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild db; db.id = 1; db.symbolId = "DelayBoolean";
  db.overrides["Trigger"] = triggerVal;
  db.overrides["FrameCount"] = 2.0f;  // delay of FrameCount+1 = 3 frames
  root.children = {db}; root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

float extOut0(const ResidentEvalGraph& g, const char* path) {
  const ResidentNode* n = g.node(path);
  return n ? n->extOut[0] : -999.0f;
}

// Cook one frame through the REAL seam, carrying state across frames.
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g,
               std::map<std::string, StatefulValueState>& state, uint32_t frame) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  ContextVarMap vars;  // DelayBoolean touches no context vars; a fresh empty map is fine.
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars, /*ctxVarBug=*/0);
}

// Run the 5-frame sequence; returns DelayedTrigger for each frame into out[5]. bug = the teeth mode.
void runSequence(int bug, float out[5]) {
  setDelayBooleanBug(bug);
  const float trig[5] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  std::map<std::string, StatefulValueState> state;
  SymbolLibrary lib; ensureSymbols(lib);
  for (uint32_t f = 0; f < 5; ++f) {
    ResidentEvalGraph g = buildFrame(lib, trig[f]);
    cookFrame(lib, g, state, f);
    out[f] = extOut0(g, "1");
  }
  setDelayBooleanBug(0);  // reset the sticky switch
}

}  // namespace

int runDelayBooleanSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] delayboolean (DelayBoolean N-frame trigger delay via cross-frame queue, REAL cook seam)\n");

  // ===== Production sequence: the delayed trigger train (bug 0). FrameCount=2 → delay 3 frames. =====
  {
    float out[5];
    runSequence(/*bug=*/0, out);
    expect("f0: Trigger=1 → DelayedTrigger=0 (queue filling)",              std::fabs(out[0] - 0.0f) < 1e-5f);
    expect("f1: Trigger=0 → DelayedTrigger=0 (queue filling)",              std::fabs(out[1] - 0.0f) < 1e-5f);
    expect("f2: Trigger=0 → DelayedTrigger=0 (queue filling)",              std::fabs(out[2] - 0.0f) < 1e-5f);
    expect("f3: Trigger=1 → DelayedTrigger=1 (= Trigger@f0, delay of 3)",   std::fabs(out[3] - 1.0f) < 1e-5f);
    expect("f4: Trigger=0 → DelayedTrigger=0 (= Trigger@f1)",               std::fabs(out[4] - 0.0f) < 1e-5f);
  }

  // ===== TEETH leg (only when injectBug): bug 1 (drop queue persistence). ====
  // Corrupts the REAL cross-frame queue; the FIXED expectation above bites. Run inside injectBug so the
  // --bite harness flips it; the production leg always runs (a normal --selftest run stays GREEN).
  if (injectBug) {
    // bug 1: the queue never persists → the delayed 1 never emerges at f3 (expected 1, gets 0).
    float b1[5];
    runSequence(/*bug=*/1, b1);
    expect("bug1: f3 delayed trigger must fire (cross-frame queue load-bearing)", std::fabs(b1[3] - 1.0f) < 1e-5f);
  }

  printf("[selftest] delayboolean %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
