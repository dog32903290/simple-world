// app/frame_cook_triggeranim_selftest — TriggerAnim PRODUCTION-PATH pin (--selftest-triggeranim).
// R-2 IRON RULE: stateful ops live on the RESIDENT cook, so the golden drives the REAL production seam
// (cookStatefulValueNodes), NOT a mock. It builds one TriggerAnim node, cooks it across a multi-frame
// trigger sweep carrying ONE StatefulValueState map (LastFraction/direction/triggerTime — the cross-frame
// channel), and asserts Result(extOut[0]) + HasCompleted(extOut[1]).
//
// TiXL authority: TriggerAnim.cs — a Trigger edge sweeps LastFraction over Duration (after Delay);
//   Result = Base + Amplitude * SchlickBias(MapShapes[Shape](LastFraction), Bias); HasCompleted fires on
//   the Forward sweep reaching 1.
//
// A/B config: AnimMode=OnlyOnTrue, Shape=Linear, Bias=0.5 (SchlickBias identity), Duration=1, Delay=0,
//   Base=0, Amplitude=10, TimeMode=LocalFxTime → Result = 10 * clamp01(LastFraction). Hand-computed:
//   f0 t=0.0 Trig=0            no edge, dir=None → LastFraction=0                       Result=0   Done=0
//   f1 t=0.1 Trig=1 (rising)   edge→Forward, triggerTime=0.1, LastFraction=(0.1-0.1)/1=0 Result=0   Done=0
//   f2 t=0.4 Trig=1            Forward (0.4-0.1)/1=0.3                                    Result=3   Done=0
//   f3 t=0.7 Trig=1            Forward 0.6                                               Result=6   Done=0
//   f4 t=1.2 Trig=1            Forward 1.1≥1 → LastFraction=1, HasCompleted, dir=None     Result=10  Done=1
//   f5 t=1.5 Trig=1            dir=None → LastFraction stays 1 (HasCompleted only on the completing frame) Result=10 Done=0
//
// C config: SAME sweep but Bias=0.25 → the shape engine is REAL. At the f3 fraction 0.6:
//   SchlickBias(0.6, 0.25) = 0.6 / ((1/0.25 - 2)(1-0.6) + 1) = 0.6/(2*0.4+1) = 0.6/1.8 = 1/3 → Result=10/3.
//
// injectBug (setTriggerAnimBug — REAL production-term corruptions, NOT flipped expectations):
//   bug 1 = DROP the state write → LastFraction/dir/triggerTime frozen at frame-1 seed → the multi-frame
//           sweep never progresses (f2..f5 diverge). Result golden bites. Wants fixed.
//   bug 2 = DROP the SchlickBias+shape shaping (Result = Base + Amplitude*rawFraction) → the C bias
//           golden bites (10/3 → 6) while the A/B linear/bias-0.5 frames stay correct. Wants fixed.
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
  if (!ok) { ++g_fail; printf("  [triggeranim] FAIL %s got=%.5f want=%.5f\n", what, got, want); }
  else printf("  [triggeranim] ok   %s = %.5f\n", what, got);
}

void ensureSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("TriggerAnim")) lib.symbols["TriggerAnim"] = atomicSymbolFromSpec(*s);
}

ResidentEvalGraph buildFrame(SymbolLibrary& lib, float trigger, float bias) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "TriggerAnim";
  c.overrides = {{"Trigger", trigger}, {"Shape", 0.0f}, {"AnimMode", 0.0f}, {"Duration", 1.0f},
                 {"Base", 0.0f}, {"Amplitude", 10.0f}, {"Delay", 0.0f}, {"Bias", bias},
                 {"TimeMode", 0.0f}};
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

// Drive the 6-frame sweep for a given Bias; assert Result + HasCompleted against the fixed wants.
void runSweep(const char* label, float bias, const float wantR[6], const float wantDone[6]) {
  SymbolLibrary lib; ensureSymbol(lib);
  ContextVarMap vars; std::map<std::string, StatefulValueState> state;
  const float times[6]   = {0.0f, 0.1f, 0.4f, 0.7f, 1.2f, 1.5f};
  const float trigger[6] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  for (uint32_t f = 0; f < 6; ++f) {
    ResidentEvalGraph g = buildFrame(lib, trigger[f], bias);
    cookAt(lib, g, vars, state, f, times[f]);
    char buf[80];
    snprintf(buf, sizeof(buf), "%s f%u Result(t=%.1f)", label, f, times[f]);
    expectClose(buf, resOut(g, 0), wantR[f]);
    snprintf(buf, sizeof(buf), "%s f%u HasCompleted", label, f);
    expectClose(buf, resOut(g, 1), wantDone[f]);
  }
}

void runAll() {
  // A/B: Linear shape, Bias=0.5 (identity) → Result = 10*clamp01(fraction).
  {
    const float wantR[6]    = {0.0f, 0.0f, 3.0f, 6.0f, 10.0f, 10.0f};
    const float wantDone[6] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    runSweep("A/B linear", 0.5f, wantR, wantDone);
  }
  // C: Bias=0.25 → the shape engine is real. Only the mid frames differ (0/1 are SchlickBias fixed points).
  //   f2 frac 0.3 → SchlickBias(0.3,0.25)=0.3/((2)(0.7)+1)=0.3/2.4=0.125 → Result=1.25
  //   f3 frac 0.6 → 0.6/1.8=1/3 → Result=10/3
  {
    const float wantR[6]    = {0.0f, 0.0f, 1.25f, 10.0f / 3.0f, 10.0f, 10.0f};
    const float wantDone[6] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    runSweep("C bias0.25", 0.25f, wantR, wantDone);
  }
}

}  // namespace

int runTriggerAnimSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] triggeranim (TriggerAnim one-shot sweep + shape on the PRODUCTION cook)\n");

  setTriggerAnimBug(0);
  runAll();
  const int cleanFail = g_fail;
  printf("[selftest] triggeranim clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    setTriggerAnimBug(1);  // drop state write → sweep frozen → bites
    int b1 = g_fail; runAll();
    printf("[selftest] triggeranim bug1(drop-state-write) added %d failure(s)\n", g_fail - b1);
    setTriggerAnimBug(2);  // drop shaping → bias-0.25 frames bite
    int b2 = g_fail; runAll();
    printf("[selftest] triggeranim bug2(drop-shape-bias) added %d failure(s)\n", g_fail - b2);
    setTriggerAnimBug(0);
  }

  printf("[selftest] triggeranim %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
