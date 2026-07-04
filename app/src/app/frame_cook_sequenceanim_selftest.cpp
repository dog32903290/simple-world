// app/frame_cook_sequenceanim_selftest — SequenceAnim PRODUCTION-PATH pin (--selftest-sequenceanim).
// R-2 IRON RULE: stateful ops live on the RESIDENT cook, so the golden drives the REAL production seam
// (cookStatefulValueNodes), NOT a mock. It builds one SequenceAnim node (Sequence string via the real
// String sub-seam: SymbolChild.strOverrides → strInputs), cooks it across a multi-frame time sweep
// carrying ONE StatefulValueState map (_lastStepIndex — the cross-frame WasStep edge channel), and
// asserts Result(extOut[0]) + WasStep(extOut[1]).
//
// TiXL authority: SequenceAnim.cs (PLAYBACK path). Sequence "5030" → 4 steps, strengths (c-'0')/9 =
//   {5/9, 0, 3/9, 0}. UpdateMode=Time, Rate=1, Phase=0, OverrideTime=0 → nbt = Fmod(time,1);
//   stepIndex = floor(nbt*4). OutputMode=NormalizedValue → Result = Lerp(Min,Max, stepStrength).
//   WasStep = stepIndex != _lastStepIndex && seq[stepIndex] > 0.  Min=2, Max=10 (so the OutputMode
//   Lerp is DISTINCT from the raw strength — bug 2 must move the value).
//
// Hand-computed (Min=2, Max=10; Result = 2 + 8*strength):
//   f0 t=0.10 nbt=0.10 step0 str=5/9 → Result=2+40/9=6.4444  WasStep=0 (step0==seed0)
//   f1 t=0.30 nbt=0.30 step1 str=0   → Result=2              WasStep=0 (seq[1]=0)
//   f2 t=0.60 nbt=0.60 step2 str=3/9 → Result=2+8/3=4.6667   WasStep=1 (2≠1 && seq[2]>0) ← cross-frame edge
//   f3 t=0.65 nbt=0.65 step2 str=3/9 → Result=4.6667         WasStep=0 (step2==step2)
//   f4 t=0.80 nbt=0.80 step3 str=0   → Result=2              WasStep=0 (seq[3]=0)
//
// injectBug (setSequenceAnimBug — REAL production-term corruptions, NOT flipped expectations):
//   bug 1 = DROP the state write (_lastStepIndex frozen at seed 0) → at f3 stepIndex=2≠0(frozen) &&
//           seq[2]>0 → WasStep wrongly TRUE (clean=FALSE). The cross-frame edge golden bites. Wants fixed.
//   bug 2 = DROP the OutputMode shaping (Result = raw stepStrength, ignoring Min/Max) → f0 6.4444→0.5556
//           etc. The OutputMode golden bites while WasStep stays correct. Wants fixed.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/transport.h"

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectClose(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-3f;
  if (!ok) { ++g_fail; printf("  [sequenceanim] FAIL %s got=%.5f want=%.5f\n", what, got, want); }
  else printf("  [sequenceanim] ok   %s = %.5f\n", what, got);
}

void ensureSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("SequenceAnim")) lib.symbols["SequenceAnim"] = atomicSymbolFromSpec(*s);
}

ResidentEvalGraph buildFrame(SymbolLibrary& lib) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "SequenceAnim";
  c.strOverrides["Sequence"] = "5030";  // real String sub-seam channel → strInputs["Sequence"]
  c.overrides = {{"SequenceIndex", 0.0f}, {"UpdateMode", 0.0f}, {"Rate", 1.0f}, {"Phase", 0.0f},
                 {"OutputMode", 1.0f}, {"MinValue", 2.0f}, {"MaxValue", 10.0f}, {"Bias", 0.5f},
                 {"OverrideTime", 0.0f}, {"Direction", 0.0f}, {"Interpolation", 0.0f}};
  root.children = {c}; root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

void cookAt(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
            std::map<std::string, StatefulValueState>& state, uint32_t frame, float timeSecs) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  cookStatefulValueNodes(g, 1.0f / 60.0f, timeSecs, 0.0, t, frame, &lib, state, vars, 0);
}
float resOut(const ResidentEvalGraph& g, int idx) {
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[idx] : -999.0f;
}

void runAll() {
  SymbolLibrary lib; ensureSymbol(lib);
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;
  const float times[5]      = {0.10f, 0.30f, 0.60f, 0.65f, 0.80f};
  const float wantR[5]      = {2.0f + 40.0f / 9.0f, 2.0f, 2.0f + 8.0f / 3.0f, 2.0f + 8.0f / 3.0f, 2.0f};
  const float wantStep[5]   = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
  for (uint32_t f = 0; f < 5; ++f) {
    ResidentEvalGraph g = buildFrame(lib);
    cookAt(lib, g, vars, state, f, times[f]);
    char buf[80];
    snprintf(buf, sizeof(buf), "f%u Result(t=%.2f)", f, times[f]);
    expectClose(buf, resOut(g, 0), wantR[f]);
    snprintf(buf, sizeof(buf), "f%u WasStep", f);
    expectClose(buf, resOut(g, 1), wantStep[f]);
  }
}

}  // namespace

int runSequenceAnimSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] sequenceanim (SequenceAnim step-sequencer playback on the PRODUCTION cook)\n");

  setSequenceAnimBug(0);
  runAll();
  const int cleanFail = g_fail;
  printf("[selftest] sequenceanim clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    setSequenceAnimBug(1);  // drop state write → WasStep edge breaks (f3 wrongly fires)
    int b1 = g_fail; runAll();
    printf("[selftest] sequenceanim bug1(drop-state-write) added %d failure(s)\n", g_fail - b1);
    setSequenceAnimBug(2);  // drop OutputMode shaping → Result becomes raw strength
    int b2 = g_fail; runAll();
    printf("[selftest] sequenceanim bug2(drop-outputmode) added %d failure(s)\n", g_fail - b2);
    setSequenceAnimBug(0);
  }

  printf("[selftest] sequenceanim %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
