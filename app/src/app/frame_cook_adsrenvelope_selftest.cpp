// app/frame_cook_adsrenvelope_selftest — AdsrEnvelope PRODUCTION-PATH pin (--selftest-adsrenvelope).
// R-2 IRON RULE: stateful ops live on the RESIDENT cook, so the golden drives the REAL production seam
// (cookStatefulValueNodes), NOT a mock. It builds one AdsrEnvelope node, cooks it across a multi-frame
// gate trajectory carrying ONE StatefulValueState map (the envelope stage machine's cross-frame channel),
// and asserts Result(extOut[0]) + IsActive(extOut[1]) at each frame.
//
// TiXL authority: AdsrEnvelope.cs:56-74 (Result = Lerp(Min,Max, calc.Value); IsActive = calc.IsActive)
//   → Core/Audio/AdsrCalculator.cs:233-354 (the frame-based Update stage machine).
//
// The trajectory is hand-computed from AdsrCalculator.cs. Envelope=(A=0.2, D=0.2, S=0.5, R=0.2),
// Min=0, Max=10, Mode=Gate. dt = time-lastTime; the first cook seeds lastTime (fork-adsr-first-frame-
// lasttime) so dt=0 there. `time` is fed per frame via the seam clock (timeSecs).
//
//   f0 t=0.0 Gate=1 rising→Attack, dt=0(seed), stageTime=0.  Attack 0<0.2 → v=0/0.2=0    Result=0   IsActive=1
//   f1 t=0.1 Gate=1 dt=0.1, stageTime=0.1. Attack 0.1<0.2 → v=0.5                          Result=5   IsActive=1
//   f2 t=0.3 Gate=1 dt=0.2, stageTime=0.3. Attack 0.3≥0.2 → v=1, →Decay(stageTime=0)       Result=10  IsActive=1
//   f3 t=0.4 Gate=1 dt=0.1, stageTime=0.1. Decay p=0.5 → v=1-0.5*(1-0.5)=0.75               Result=7.5 IsActive=1
//   f4 t=0.6 Gate=1 dt=0.2, stageTime=0.3. Decay 0.3≥0.2 → v=sustain=0.5, →Sustain          Result=5   IsActive=1
//   f5 t=0.7 Gate=0 falling→Release(releaseStart=0.5), dt=0.1, stageTime=0.1. Release p=0.5 →
//            v=0.5*(1-0.5)=0.25                                                             Result=2.5 IsActive=1
//
// injectBug (setAdsrEnvelopeBug — REAL production-term corruptions, NOT flipped expectations):
//   bug 1 = DROP the state write → the stage machine freezes at frame-1 seed → f2..f5 diverge (the
//           Attack→Decay→Sustain→Release progression never advances). Result golden bites. Wants fixed.
//   bug 2 = DROP the Min/Max Lerp → Result becomes the raw 0..1 envelope value → the range golden bites
//           at every non-zero frame (f1 5→0.5, f2 10→1, ...) while IsActive stays correct. Wants fixed.
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
  if (!ok) { ++g_fail; printf("  [adsrenvelope] FAIL %s got=%.5f want=%.5f\n", what, got, want); }
  else printf("  [adsrenvelope] ok   %s = %.5f\n", what, got);
}

void ensureSymbol(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("AdsrEnvelope")) lib.symbols["AdsrEnvelope"] = atomicSymbolFromSpec(*s);
}

// One AdsrEnvelope child with A=0.2/D=0.2/S=0.5/R=0.2, Min=0, Max=10, Mode=Gate, given Gate value.
ResidentEvalGraph buildFrame(SymbolLibrary& lib, float gate) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "AdsrEnvelope";
  c.overrides = {{"Gate", gate}, {"Mode", 0.0f}, {"Duration", 1.0f},
                 {"Envelope.x", 0.2f}, {"Envelope.y", 0.2f}, {"Envelope.z", 0.5f}, {"Envelope.w", 0.2f},
                 {"Min", 0.0f}, {"Max", 10.0f}};
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
  const float times[6] = {0.0f, 0.1f, 0.3f, 0.4f, 0.6f, 0.7f};
  const float gates[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
  const float wantR[6] = {0.0f, 5.0f, 10.0f, 7.5f, 5.0f, 2.5f};
  const float wantA[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};  // stage never Idle across this run
  for (uint32_t f = 0; f < 6; ++f) {
    ResidentEvalGraph g = buildFrame(lib, gates[f]);
    cookAt(lib, g, vars, state, f, times[f]);
    char buf[80];
    snprintf(buf, sizeof(buf), "f%u Result(t=%.1f)", f, times[f]);
    expectClose(buf, resOut(g, 0), wantR[f]);
    snprintf(buf, sizeof(buf), "f%u IsActive", f);
    expectClose(buf, resOut(g, 1), wantA[f]);
  }
}

}  // namespace

int runAdsrEnvelopeSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] adsrenvelope (AdsrEnvelope gate ADSR machine on the PRODUCTION cook)\n");

  setAdsrEnvelopeBug(0);
  runAll();
  const int cleanFail = g_fail;
  printf("[selftest] adsrenvelope clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    setAdsrEnvelopeBug(1);  // drop state write → stage machine frozen → progression bites
    int b1 = g_fail; runAll();
    printf("[selftest] adsrenvelope bug1(drop-state-write) added %d failure(s)\n", g_fail - b1);
    setAdsrEnvelopeBug(2);  // drop Min/Max Lerp → range bites
    int b2 = g_fail; runAll();
    printf("[selftest] adsrenvelope bug2(drop-lerp-range) added %d failure(s)\n", g_fail - b2);
    setAdsrEnvelopeBug(0);
  }

  printf("[selftest] adsrenvelope %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
