// floatlist_amplifyvalues_golden — --selftest-amplifyvalues. MULTI-FRAME golden for AmplifyValues, the
// FIRST cross-frame state consumer on the FLOATLIST rail. The state IS the whole point, so this drives the
// SAME node across frames carrying the per-node FloatListState (_averagedValues/_lastValues/_output), on
// BOTH the flat cook (PointGraph reused across cook() calls → Impl::floatListState persists) AND the
// PRODUCTION resident path (rebuild the resident graph per frame, but carry ONE residentFloatListState()
// process store — the EXACT pattern of the KeepColors golden, keepcolors_golden.cpp).
//
// TiXL authority (AmplifyValues.cs): each frame, if the input CHANGED vs last frame, smoothed = Lerp(v,
// _averaged, smoothing) and _averaged := smoothed (the running average chases the input). output =
// clamp(v-smoothed)*mixAboveAverage + v*mixCurrent + smoothed*mixAverage. Identical input frame→frame =>
// FROZEN (no further drift). HAND-COMPUTED trajectories (independent float reference /tmp/amplify_ref.py):
//
//   ★ THE DAMP PROOF (the load-bearing 2-frame assertion):
//     smoothing=0.5, mixAverage=1, others 0 (→ output == smoothed). Single-element input.
//       f0 in=[0.0]  → last starts [0], no change → output [0.0]
//       f1 in=[10.0] → CHANGED: smoothed = Lerp(10, _avg=0, 0.5) = 10 + (0-10)*0.5 = 5.0 → output [5.0]
//                      (DAMPED toward 10, NOT 10 — the whole point: the f1 output is HALFWAY, not instant)
//     The state slot is what makes f1 read the PERSISTED _avg=0 (carried from f0) and damp to 5.0.
//
//   ★ THE PERSISTENCE PROOF (clean, injectBug-independent) = RAMP f3 below. f3 input=20 damps to
//     Lerp(20, _avg=5, 0.5) = 12.5 — but _avg=5 is ONLY there if the running average PERSISTED across the
//     frozen f2 frame. With NO persistence (_avg reset to 0 each frame) f3 would be Lerp(20,0,0.5)=10 ≠
//     12.5 → the f3 assert would FAIL. So the clean RAMP f3 PASS is the direct proof the state slot persists.
//
//   injectBug routes through the SHARED floatListInjectBug() (the rail's standard teeth): it corrupts the
//   REAL cook on EVERY floatlist op in the chain — the upstream FloatsToList drops its only element → an
//   EMPTY list reaches AmplifyValues → output empty → the picked value reads 0 (≠ the clean want) → RED on
//   the actual cook path. AmplifyValues ALSO honors the flag inside its damp (it reads _avg as if it were
//   the current input → smoothed=v, no damp), so the teeth bite at the op even if the input were intact.
//
//   RAMP (CONTINUE damping when the input keeps changing): smoothing=0.5, mixAverage=1.
//       f0 [0]→[0.0] ; f1 [10]→[5.0] ; f2 [10]→[5.0] (FROZEN, unchanged input) ; f3 [20]→ Lerp(20,_avg=5,0.5)
//       = 20+(5-20)*0.5 = 12.5 → [12.5]. Proves _avg PERSISTS across the frozen frame (f3 reads 5, not 0).
//
//   PEAK-AMPLIFY (the real preset — MixAboveAverage>0): smoothing=0.5, mixAbove=2, mixCurrent=1, mixAvg=0.
//       output = clamp(v-smoothed)*2 + v*1 + smoothed*0.
//       f0 [0]→[0.0] ; f1 [10] CHANGED: smoothed=5 → clamp(10-5)*2 + 10*1 = 5*2+10 = 20.0 → [20.0].
//       Proves the above-average term + the mix weights ride the same persisted average.
//
// ★R-2: the RESIDENT leg proves the per-node state PERSISTS on the PRODUCTION path (cookResidentFloatList's
// residentFloatListState process store the running app threads), not flat-only. The resident leg drives
// AmplifyValues THROUGH a downstream PickFloatFromList consumer (cookHostScalarNodes gathers AmplifyValues
// via cookResidentFloatList — the EXACT site this lane wired the state slot + cook-once guard), reading the
// picked element via evalResidentFloat. If the resident state wiring regressed (state→nullptr) the f1 read
// would jump to 10.0 → RED.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedFloatList
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat
#include "runtime/resident_value_cooks.h"    // resetResidentFloatListState (reset the process store per case)

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// One per-frame configuration of the AmplifyValues node.
struct Frame {
  float input;       // the single-element input value this frame
  float smoothing;
  float mixAverage;
  float mixCurrent;
  float mixAboveAverage;
  float want;        // expected output[0] AFTER this frame (clean)
};

// Build AmplifyValues(id=1) ← FloatsToList(id=2) ← Const(id=5, this frame's input). FloatsToList gathers its
// scalar Float MultiInput into a 1-element list. AmplifyValues reads that list, damps, publishes.
// PickFloatFromList(id=3) reads AmplifyValues.out (the resident consumer that drives cookHostScalarNodes).
//   Const ports: [0]=value(param), [1]=out(Float).   FloatsToList ports: [0]=Input(Float MultiInput), out implicit.
//   AmplifyValues ports: [0]=out(FloatList), [1]=Input(FloatList), [2..5]=Smoothing/MixAverage/MixCurrent/MixAboveAverage.
//   PickFloatFromList ports: [0]=Selected(out), [1]=Input(FloatList), [2]=Index(param).
Graph makeAmplify(const Frame& f, bool withPick) {
  Graph g;
  // Const(value) — the established scalar producer (out at port 1, value param key "value"); evalConst
  // returns the stored value verbatim (the -10..10 range is UI-only, no clamp).
  Node c; c.id = 5; c.type = "Const"; c.params["value"] = f.input; g.nodes.push_back(c);
  Node fl; fl.id = 2; fl.type = "FloatsToList"; g.nodes.push_back(fl);
  Node amp; amp.id = 1; amp.type = "AmplifyValues";
  amp.params["Smoothing"] = f.smoothing;
  amp.params["MixAverage"] = f.mixAverage;
  amp.params["MixCurrent"] = f.mixCurrent;
  amp.params["MixAboveAverage"] = f.mixAboveAverage;
  g.nodes.push_back(amp);
  int connId = 100;
  // Const.out (port 1) → FloatsToList.Input (port 0, scalar Float MultiInput).
  g.connections.push_back({connId++, pinId(5, 1), pinId(2, 0)});
  // FloatsToList.out (port index AFTER its inputs) → AmplifyValues.Input (port 1, FloatList). FloatsToList's
  // ONLY port is the Input MultiInput (index 0); its output is the implicit first non-input = the node's out.
  // The driver keys a FloatList output by node, not port, so a wire from the node's out reaches AmplifyValues.
  g.connections.push_back({connId++, pinId(2, /*out*/ 1), pinId(1, /*Input*/ 1)});
  if (withPick) {
    Node pk; pk.id = 3; pk.type = "PickFloatFromList"; pk.params["Index"] = 0.0f; g.nodes.push_back(pk);
    // AmplifyValues.out (port 0) → PickFloatFromList.Input (port 1).
    g.connections.push_back({connId++, pinId(1, /*out*/ 0), pinId(3, /*Input*/ 1)});
  }
  return g;
}

// FLAT leg: ONE PointGraph reused across all frames (so Impl::floatListState["#1"] persists). Cook each
// frame's graph with AmplifyValues as the terminal; return output[0] after EACH frame.
std::vector<float> cookFlatTrajectory(PointGraph& pg, const std::vector<Frame>& frames) {
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeAmplify(frames[fi], /*withPick=*/false);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);  // AmplifyValues is the terminal
    const std::vector<float>* o = pg.debugCookedFloatList(1);
    outs.push_back((o && !o->empty()) ? (*o)[0] : 0.0f);
  }
  return outs;
}

// ★R-2 RESIDENT leg (production): rebuild the resident graph each frame with the new input, but carry the
// residentFloatListState() process store across frames (reset once per trajectory). Drive AmplifyValues
// through PickFloatFromList via cookHostScalarNodes, read the picked element off node "3"'s extOut[0].
std::vector<float> cookResidentTrajectory(const std::vector<Frame>& frames) {
  resetResidentFloatListState();  // fresh process store for this trajectory (the cross-frame accumulator)
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeAmplify(frames[fi], /*withPick=*/true);
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = (uint32_t)fi; rc.lib = &lib;
    cookHostScalarNodes(rg, rc);  // PickFloatFromList gathers AmplifyValues via cookResidentFloatList
    outs.push_back(evalResidentFloat(rg, "3", "Selected", rc));  // the picked element (index 0)
  }
  return outs;
}

// Run ONE trajectory on BOTH legs, assert each frame's output == want. Returns true on PASS.
bool runCase(const char* label, const std::vector<Frame>& frames, bool injectBug) {
  bool ok = true;
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    floatListInjectBug() = injectBug;
    std::vector<float> got = cookFlatTrajectory(pg, frames);
    floatListInjectBug() = false;
    for (size_t i = 0; i < frames.size(); ++i) {
      bool pass = nearf(got[i], frames[i].want);
      ok = ok && pass;
      std::printf("[selftest-amplifyvalues] %s FLAT f%zu got=%.5f want=%.5f -> %s\n", label, i, got[i],
                  frames[i].want, pass ? "PASS" : "FAIL");
    }
    q->release(); dev->release(); pool->release();
  }
  {
    floatListInjectBug() = injectBug;
    std::vector<float> got = cookResidentTrajectory(frames);
    floatListInjectBug() = false;
    for (size_t i = 0; i < frames.size(); ++i) {
      bool pass = nearf(got[i], frames[i].want);
      ok = ok && pass;
      std::printf("[selftest-amplifyvalues] %s RES  f%zu got=%.5f want=%.5f -> %s\n", label, i, got[i],
                  frames[i].want, pass ? "PASS" : "FAIL");
    }
  }
  return ok;
}

}  // namespace

int runAmplifyValuesSelfTest(bool injectBug) {
  bool ok = true;

  // ★ DAMP PROOF: smoothing=0.5, mixAverage=1. f0 in=0 (no change→0); f1 in=10 (CHANGED→Lerp(10,0,0.5)=5.0).
  //   The f1 want=5.0 is the load-bearing damp assertion: WITHOUT persistence the bug jumps to 10.0 → RED.
  {
    std::vector<Frame> frames = {
        {/*in*/ 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, /*want*/ 0.0f},
        {/*in*/ 10.0f, 0.5f, 1.0f, 0.0f, 0.0f, /*want*/ 5.0f}};
    ok = runCase("DAMP", frames, injectBug) && ok;
  }

  // RAMP: smoothing=0.5, mixAverage=1. f0 0→0; f1 10→5; f2 10→5 (FROZEN unchanged input); f3 20→12.5
  //   (Lerp(20, _avg=5, 0.5) — proves _avg persisted ACROSS the frozen frame: 5 not 0).
  {
    std::vector<Frame> frames = {
        {0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f},
        {10.0f, 0.5f, 1.0f, 0.0f, 0.0f, 5.0f},
        {10.0f, 0.5f, 1.0f, 0.0f, 0.0f, 5.0f},
        {20.0f, 0.5f, 1.0f, 0.0f, 0.0f, 12.5f}};
    ok = runCase("RAMP", frames, injectBug) && ok;
  }

  // PEAK-AMPLIFY: smoothing=0.5, mixAbove=2, mixCurrent=1, mixAvg=0. f0 0→0; f1 10→ clamp(10-5)*2+10 = 20.0.
  {
    std::vector<Frame> frames = {
        {0.0f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f},
        {10.0f, 0.5f, 0.0f, 1.0f, 2.0f, 20.0f}};
    ok = runCase("PEAK", frames, injectBug) && ok;
  }

  std::printf("[selftest-amplifyvalues] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
