// floatlist_animfloatlist_golden — --selftest-animfloatlist. BRIDGE-grade golden for the AnimFloatList
// FloatList PRODUCER (numbers/anim/animators) — a pure animator that emits a List<float> of OffsetNumber
// AnimMath shape samples on the bars clock (LocalFxTime), one per index with the per-index time offset
// `i*OffsetCycle + OffsetNumber`. (See floatlist_ops_animfloatlist.cpp for the full TiXL transcription.)
//
// WHY a chain-through golden (LIST_SEAM_BLUEPRINT §4): AnimFloatList's FloatList output is proven to FLOW
// node→node and be CONSUMED by a downstream host-scalar op (PickFloatFromList for an indexed ELEMENT,
// FloatListLength for the COUNT) — not merely transported. A transport-only readback passes even if the
// bridge is broken; the consume path catches a real wire break.
//
// ★TWO LEGS — flat AND production resident (mirror of list_routing_golden + value_op_animvec3's dual leg):
//   • FLAT: PointGraph::cook (cookFlatFloatList, fc.ctx=&ctx already wired) → evalFloat the downstream.
//   • RESIDENT (★ the production-in-app path): libFromGraph → buildEvalGraph → cookHostScalarNodes →
//     evalResidentFloat. cookHostScalarNodes's PickFloatFromList consumer internally gathers AnimFloatList
//     via cookResidentFloatList — the EXACT site this lane wired fc.ctx (LocalFxTime) + fc.params (the
//     resolved shape/rate/...). If that wiring regressed (ctx→nullptr / params→nullptr), the resident leg
//     would read time 0 / default params → WRONG value → RED. This leg is what makes AnimFloatList faithful
//     in the running editor, not flat-only.
//
// ★Expected values HAND-COMPUTED from AnimFloatList.cs + AnimMath.cs (NOT self-referential to the leaf),
//   reproduced by an INDEPENDENT float32 reference (/tmp/animfloatlist_ref.py in the build dossier):
//   per index i:  adjustedTime = (float)(LocalFxTime + Phase + i*OffsetCycle + OffsetNumber);
//                 nt = adjustedTime * 1 * Rate;  v = CalcValueForNormalizedTime(Shape, nt, 0, Bias, Ratio)*Amp+Off.
//
//   CASE A (Shape=Sin(7), LocalFxTime=2.0, OffsetNumber=3, OffsetCycle=0.13, Phase=0.05, Rate=0.7,
//           Ratio=1, Bias=0.5, Amplitude=2, Offset=10, AllowSpeedFactor=FactorA → rfc=1):
//     i=0: adjustedTime=2.0+0.05+0+3=5.05 ; nt=5.05*0.7=3.535 ; Sin(fmod=0.535)→(−0.97592)*2+10 = 8.048166
//     i=1: adjustedTime=5.18 ; nt=3.626 ; Sin(fmod=0.626) → 8.594698
//     i=2: adjustedTime=5.31 ; nt=3.717 ; Sin(fmod=0.717) → 9.588272
//     (Sin shape := schlickBiasWithNegative(sin((fmod(nt,1)+0.25)*2π), 0.5); independent ref reproduces.)
//     LocalFxTime is LIVE (2.0): if the resident leg lost LocalFxTime (read 0) → nt=(0+0.05+i*.13+3)*0.7 →
//     DIFFERENT fmod → wrong Sin → the resident assert flips RED (the non-integer Rate ensures time=0 does
//     NOT alias the live point — the divergence is real, the LocalFxTime-wiring tooth is load-bearing).
//
//   CASE B (Shape=Saws(2), LocalFxTime=1.0, OffsetNumber=4, OffsetCycle=0.25, Phase=0, Rate=1, Ratio=1,
//           Bias=0.5, Amplitude=1, Offset=0): nt(i)=1+0+i*0.25+4 = 5, 5.25, 5.5, 5.75 ; fmod = 0, .25, .5, .75
//     Saws = 1 - fmod (schlickBias(.,0.5) identity, mapShape(2,ff)=1-fmod(ff,1), ff∈[0,1]): 1.0, 0.75, 0.5, 0.25.
//     This case is the PER-INDEX-TIME tooth: the values are a clean descending ramp over i, so an off-by-one
//     in the `i*OffsetCycle + OffsetNumber` term ROTATES the list (1,.75,.5,.25 → .75,.5,.25,1) and every
//     indexed assert shifts → bites (the harness RED leg via floatListInjectBug drops the last element).
//
//   CASE C: OffsetNumber=0 → EMPTY-LIST GUARD. FloatListLength = 0 (the .cs `if (offsetNumber<=0) return
//           empty` path). Proves the guard fires (a fresh AnimFloatList with no count set yields no elements).
//
// injectBug routes through floatListInjectBug() (drops AnimFloatList's last output element in the REAL
// cook) → a downstream PickFloatFromList at the dropped tail / FloatListLength reads wrong → RED on the
// actual cook path, NOT by flipping the expected value. --bite runs --selftest-animfloatlist-bug → exit !=0.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext (+ localFxTime, the bars clock)
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/point_graph.h"             // PointGraph::cook
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Set every AnimFloatList Float param explicitly on node `id` (so the golden is NOT default-dependent).
void setAnimParams(Graph& g, int id, float shape, float offsetNumber, float offsetCycle, float rate,
                   float amplitude, float phase, float offset, float bias, float ratio,
                   float allowSpeed) {
  Node* n = g.node(id);
  n->params["Shape"] = shape;
  n->params["OffsetNumber"] = offsetNumber;
  n->params["OffsetCycle"] = offsetCycle;
  n->params["Rate"] = rate;
  n->params["Amplitude"] = amplitude;
  n->params["Phase"] = phase;
  n->params["Offset"] = offset;
  n->params["Bias"] = bias;
  n->params["Ratio"] = ratio;
  n->params["AllowSpeedFactor"] = allowSpeed;
}

// AnimFloatList(id=2, params) → PickFloatFromList(id=4, Index) reading AnimFloatList.out (port 0).
// PickFloatFromList ports: [0]=Selected(out), [1]=Input(FloatList), [2]=Index(param).
int chainAnimPick(Graph& g, int& connId, float index, float shape, float offsetNumber, float offsetCycle,
                  float rate, float amplitude, float phase, float offset, float bias, float ratio,
                  float allowSpeed) {
  int afl = 2, pk = 4;
  Node a; a.id = afl; a.type = "AnimFloatList"; g.nodes.push_back(a);
  setAnimParams(g, afl, shape, offsetNumber, offsetCycle, rate, amplitude, phase, offset, bias, ratio, allowSpeed);
  Node p; p.id = pk; p.type = "PickFloatFromList"; p.params["Index"] = index; g.nodes.push_back(p);
  g.connections.push_back({connId++, pinId(afl, /*out*/ 0), pinId(pk, /*Input*/ 1)});
  return pk;
}

// AnimFloatList(id=2, params) → FloatListLength(id=4) reading AnimFloatList.out (port 0).
// FloatListLength ports: [0]=Length(out), [1]=Input(FloatList).
int chainAnimLen(Graph& g, int& connId, float shape, float offsetNumber, float offsetCycle, float rate,
                 float amplitude, float phase, float offset, float bias, float ratio, float allowSpeed) {
  int afl = 2, len = 4;
  Node a; a.id = afl; a.type = "AnimFloatList"; g.nodes.push_back(a);
  setAnimParams(g, afl, shape, offsetNumber, offsetCycle, rate, amplitude, phase, offset, bias, ratio, allowSpeed);
  Node l; l.id = len; l.type = "FloatListLength"; g.nodes.push_back(l);
  g.connections.push_back({connId++, pinId(afl, /*out*/ 0), pinId(len, /*Input*/ 1)});
  return len;
}

// FLAT leg: PointGraph::cook with an explicit LocalFxTime, then evalFloat the downstream out (port 0).
float cookEvalFlat(PointGraph& pg, Graph& g, int terminalId, float localFxTime) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  ctx.localFxTime = localFxTime;  // BARS — AnimFloatList's clock (flat fc.ctx=&ctx carries it through)
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/terminalId);
  return evalFloat(g, pinId(terminalId, /*out port 0*/ 0), ctx);
}

// RESIDENT leg (★ production path): mirror the SAME flat Graph into a SymbolLibrary, build the resident
// graph, run the production cookHostScalarNodes pass with an explicit LocalFxTime, then evalResidentFloat
// the downstream node's out slot. cookHostScalarNodes's consumer gathers AnimFloatList through
// cookResidentFloatList — the wired-ctx/params site. `path` = downstream node id string, `slot` its out slot.
float cookEvalResident(Graph& g, const std::string& path, const std::string& slot, float localFxTime) {
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = localFxTime; rc.frameIndex = 0; rc.lib = &lib;
  cookHostScalarNodes(rg, rc);
  return evalResidentFloat(rg, path, slot, rc);
}

}  // namespace

int runAnimFloatListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  bool ok = true;

  // FLAT runner: build a fresh graph via `build`, cook+eval the downstream terminal, assert nearf(want).
  auto runFlat = [&](const char* tag, int terminal, float localFxTime, float want, Graph& g) {
    floatListInjectBug() = injectBug;
    float got = cookEvalFlat(pg, g, terminal, localFxTime);
    floatListInjectBug() = false;
    bool pass = nearf(got, want);
    ok = ok && pass;
    std::printf("[selftest-animfloatlist] %-34s FLAT got=%.6f want=%.6f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };
  // RESIDENT runner: same graph, production cook+eval, assert nearf(want). Proves the resident
  // ctx(LocalFxTime)+params wiring (this lane's cookResidentFloatList edit) is LIVE.
  auto runResident = [&](const char* tag, const std::string& path, const std::string& slot,
                         float localFxTime, float want, Graph& g) {
    floatListInjectBug() = injectBug;
    float got = cookEvalResident(g, path, slot, localFxTime);
    floatListInjectBug() = false;
    bool pass = nearf(got, want);
    ok = ok && pass;
    std::printf("[selftest-animfloatlist] %-34s RES  got=%.6f want=%.6f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  // === CASE A — Shape=Sin(7), LocalFxTime=2.0, N=3, OffsetCycle=0.13, Phase=0.05, Rate=0.7, Amp=2, Off=10.
  //     Hand-computed (independent float32 ref): [8.048166, 8.594698, 9.588272]. PickFloatFromList[i].
  const float A0 = 8.048166f, A1 = 8.594698f, A2 = 9.588272f;
  {  // i=0 (FLAT + RESIDENT). LocalFxTime LIVE (2.0) — a resident time-0 regression diverges here.
    Graph g; int c = 100;
    int t = chainAnimPick(g, c, /*Index*/ 0, /*Sin*/ 7, /*N*/ 3, /*cycle*/ 0.13f, /*rate*/ 0.7f,
                          /*amp*/ 2, /*phase*/ 0.05f, /*off*/ 10, /*bias*/ 0.5f, /*ratio*/ 1, /*speed*/ 1);
    runFlat("A Sin t=2 N=3 [0]", t, 2.0f, A0, g);
    runResident("A Sin t=2 N=3 [0]", "4", "Selected", 2.0f, A0, g);
  }
  {  // i=1
    Graph g; int c = 100;
    int t = chainAnimPick(g, c, 1, 7, 3, 0.13f, 0.7f, 2, 0.05f, 10, 0.5f, 1, 1);
    runFlat("A Sin t=2 N=3 [1]", t, 2.0f, A1, g);
    runResident("A Sin t=2 N=3 [1]", "4", "Selected", 2.0f, A1, g);
  }
  {  // i=2 (the tail element — the one floatListInjectBug drops → RED here under -bug)
    Graph g; int c = 100;
    int t = chainAnimPick(g, c, 2, 7, 3, 0.13f, 0.7f, 2, 0.05f, 10, 0.5f, 1, 1);
    runFlat("A Sin t=2 N=3 [2]", t, 2.0f, A2, g);
    runResident("A Sin t=2 N=3 [2]", "4", "Selected", 2.0f, A2, g);
  }

  // === CASE B — Shape=Saws(2), LocalFxTime=1.0, N=4, OffsetCycle=0.25, Phase=0, Rate=1, Amp=1, Off=0.
  //     Saws = 1 - fmod(nt,1); nt = 5, 5.25, 5.5, 5.75 → [1.0, 0.75, 0.5, 0.25] (the per-index-time tooth).
  const float B[4] = {1.0f, 0.75f, 0.5f, 0.25f};
  for (int i = 0; i < 4; ++i) {
    Graph g; int c = 100;
    int t = chainAnimPick(g, c, (float)i, /*Saws*/ 2, /*N*/ 4, /*cycle*/ 0.25f, /*rate*/ 1, /*amp*/ 1,
                          /*phase*/ 0, /*off*/ 0, /*bias*/ 0.5f, /*ratio*/ 1, /*speed*/ 1);
    char tag[48];
    std::snprintf(tag, sizeof(tag), "B Saws t=1 N=4 [%d]", i);
    runFlat(tag, t, 1.0f, B[i], g);
    runResident(tag, "4", "Selected", 1.0f, B[i], g);
  }

  // === CASE C — OffsetNumber=0 → empty-list guard. FloatListLength == 0 (the .cs early-return path).
  //     (NOTE: injectBug's pop_back is a no-op on an empty list, so C stays 0 even under -bug; the teeth
  //     bite via A[2]/B's tail-drop. C is a parity assert, not a RED-leg carrier.)
  {
    Graph g; int c = 100;
    int t = chainAnimLen(g, c, /*Sin*/ 7, /*N*/ 0, /*cycle*/ 0.13f, /*rate*/ 0.7f, /*amp*/ 2,
                         /*phase*/ 0.05f, /*off*/ 10, /*bias*/ 0.5f, /*ratio*/ 1, /*speed*/ 1);
    runFlat("C N=0 empty-guard Length", t, 2.0f, 0.0f, g);
    runResident("C N=0 empty-guard Length", "4", "Length", 2.0f, 0.0f, g);
  }

  // === COUNT preserved (D): AnimFloatList(N=4).Length == 4 (one element per index). RED under -bug
  //     (the dropped tail → Length 3). Proves the resident FloatList gather reaches the producer.
  {
    Graph g; int c = 100;
    int t = chainAnimLen(g, c, /*Saws*/ 2, /*N*/ 4, /*cycle*/ 0.25f, /*rate*/ 1, /*amp*/ 1,
                         /*phase*/ 0, /*off*/ 0, /*bias*/ 0.5f, /*ratio*/ 1, /*speed*/ 1);
    // want is the CLEAN value (4); injectBug drops the last element → got reads 3 → RED (no inversion).
    runFlat("D N=4 Length", t, 1.0f, 4.0f, g);
    runResident("D N=4 Length", "4", "Length", 1.0f, 4.0f, g);
  }

  // === CASE E — OverrideTime knob (live clock substitute). Self-consistency: AnimFloatList driven by
  //     OverrideTime=T with LocalFxTime=0 must emit the SAME element as the default node (OverrideTime=0)
  //     driven by the clock at LocalFxTime=T. Proves the knob substitutes for the bars clock 1:1 on BOTH
  //     the flat and the production resident path, and that the new OverrideTime param actually gathers.
  //     Element index 0, CASE A's Sin params (N=3). (Not bug-carrying: injectBug only drops the tail.)
  {
    const float T = 2.0f;
    // Reference: default node (OverrideTime=0) on the clock at LocalFxTime=T → use the known A0=8.048166.
    // Override leg: OverrideTime=T, clock at 0 → must reproduce A0 from the knob, not the (zero) clock.
    Graph g; int c = 100;
    int t = chainAnimPick(g, c, /*Index*/ 0, /*Sin*/ 7, /*N*/ 3, /*cycle*/ 0.13f, /*rate*/ 0.7f,
                          /*amp*/ 2, /*phase*/ 0.05f, /*off*/ 10, /*bias*/ 0.5f, /*ratio*/ 1, /*speed*/ 1);
    g.node(2)->params["OverrideTime"] = T;  // nonzero → override the clock (node id 2 = AnimFloatList)
    runFlat("E OverrideTime=2 (clock=0) [0]", t, /*LocalFxTime*/ 0.0f, A0, g);
    runResident("E OverrideTime=2 (clock=0) [0]", "4", "Selected", /*LocalFxTime*/ 0.0f, A0, g);
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug drops AnimFloatList's last output
  // element → A[2] (the tail) reads wrong + D's Length reads 3 not 4 → ok false → return 1 (teeth bite).
  std::printf("[selftest-animfloatlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
