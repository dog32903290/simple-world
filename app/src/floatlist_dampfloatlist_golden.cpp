// floatlist_dampfloatlist_golden — --selftest-dampfloatlist. MULTI-FRAME golden for DampFloatList, a cross-
// frame STATE consumer on the FLOATLIST rail (sibling of AmplifyValues). The state IS the point, so this
// drives the SAME node across frames carrying the per-node FloatListState (_dampedValues / _velocities /
// _lastEvalTime), on BOTH the flat cook (PointGraph reused → Impl::floatListState persists) AND the PRODUCTION
// resident path (rebuild the resident graph per frame, carry ONE residentFloatListState() process store).
//
// TiXL authority (DampFloatList.cs, Method 0 = LinearInterpolation = the .t3 default): each evaluated frame
// (passing the MinTimeElapsed dt-gate), dampedValue = Lerp(input, _dampedValues, damping) = input +
// (_dampedValues - input)*damping ; _dampedValues := dampedValue (persists). HAND-COMPUTED trajectories
// (independent float reference reproduces; damping=0.5 single-element input, localFxTime advancing per frame):
//
//   ★ THE DAMP PROOF (the load-bearing 2-frame assertion):
//     damping=0.5. f0 lfx=1.0 in=[0.0] → first cook, prev=0 → Lerp(0,0,0.5)=0.0 → [0.0]
//                  f1 lfx=2.0 in=[10.0] → gate |2-1|=1≥0.001 PASS ; Lerp(10, prev=0, 0.5)=10+(0-10)*0.5=5.0 → [5.0]
//     The f1 output is HALFWAY (5.0), NOT instant 10.0 — the damp reads the PERSISTED prev=0 carried from f0.
//
//   ★ THE PERSISTENCE PROOF (clean, injectBug-independent) = RAMP f2/f3 below:
//     f2 lfx=3.0 in=[10] → Lerp(10, prev=5, 0.5)=10+(5-10)*0.5=7.5  (prev=5 ONLY if it persisted from f1)
//     f3 lfx=4.0 in=[20] → Lerp(20, prev=7.5, 0.5)=20+(7.5-20)*0.5=13.75
//     With NO persistence (prev reset to 0 each frame) f2 would be Lerp(10,0,0.5)=5.0 ≠ 7.5 → the assert FAILS.
//     So the clean RAMP PASS is the direct proof the state slot persists.
//
//   ★ THE DT-GATE PROOF (fork-dampfloatlist-dt-gate): a frame whose localFxTime is UNCHANGED from the prior
//     evaluated frame (|Δ| < 1/1000) is GATED — the op re-publishes the previous _dampedValues WITHOUT damping.
//     GATE case: f0 lfx=1.0 in=[0]→[0] ; f1 lfx=2.0 in=[10]→[5.0] ; f2 lfx=2.0 in=[10] GATED → re-publish [5.0]
//     (NOT Lerp(10,5,0.5)=7.5) ; f3 lfx=3.0 in=[10] PASS → Lerp(10, prev=5, 0.5)=7.5. The f2 want=5.0 proves
//     the gate suppresses the damp; the f3 want=7.5 proves _dampedValues survived the gated frame (still 5).
//
//   injectBug routes through floatListInjectBug() (the rail's standard teeth): upstream FloatsToList drops its
//   only element → empty list reaches DampFloatList → empty output → picked value 0 (≠ clean want) → RED. The
//   leaf ALSO honors the flag inside its damp (prev := input → Lerp(input,input,d)=input, no damp) → the
//   output jumps instantly to the input, proving the state slot is load-bearing.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext (+ localFxTime, the bars clock for the dt-gate)
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedFloatList
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat
#include "runtime/resident_value_cooks.h"    // resetResidentFloatListState

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// One per-frame configuration of the DampFloatList node.
struct Frame {
  float input;        // the single-element input value this frame
  float localFxTime;  // BARS clock (drives the dt-gate)
  float damping;
  float want;         // expected output[0] AFTER this frame (clean)
};

// Build DampFloatList(id=1) ← FloatsToList(id=2) ← Const(id=5, this frame's input). PickFloatFromList(id=3)
// reads DampFloatList.out (the resident consumer that drives cookHostScalarNodes).
//   DampFloatList ports: [0]=out(FloatList), [1]=Values(FloatList), [2]=Damping, [3]=Method, [4]=UseAppRunTime.
Graph makeDamp(const Frame& f, bool withPick) {
  Graph g;
  Node c; c.id = 5; c.type = "Const"; c.params["value"] = f.input; g.nodes.push_back(c);
  Node fl; fl.id = 2; fl.type = "FloatsToList"; g.nodes.push_back(fl);
  Node dp; dp.id = 1; dp.type = "DampFloatList";
  dp.params["Damping"] = f.damping;
  dp.params["Method"] = 0.0f;          // LinearInterpolation (.t3 default)
  dp.params["UseAppRunTime"] = 0.0f;   // false → currentTime = LocalFxTime
  g.nodes.push_back(dp);
  int connId = 100;
  g.connections.push_back({connId++, pinId(5, 1), pinId(2, 0)});            // Const.out → FloatsToList.Input
  g.connections.push_back({connId++, pinId(2, /*out*/ 1), pinId(1, /*Values*/ 1)});  // FloatsToList.out → Damp.Values
  if (withPick) {
    Node pk; pk.id = 3; pk.type = "PickFloatFromList"; pk.params["Index"] = 0.0f; g.nodes.push_back(pk);
    g.connections.push_back({connId++, pinId(1, /*out*/ 0), pinId(3, /*Input*/ 1)});  // Damp.out → Pick.Input
  }
  return g;
}

// FLAT leg: ONE PointGraph reused across all frames (so Impl::floatListState["#1"] persists).
std::vector<float> cookFlatTrajectory(PointGraph& pg, const std::vector<Frame>& frames) {
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeDamp(frames[fi], /*withPick=*/false);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    ctx.localFxTime = frames[fi].localFxTime;  // the bars clock the dt-gate reads
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    const std::vector<float>* o = pg.debugCookedFloatList(1);
    outs.push_back((o && !o->empty()) ? (*o)[0] : 0.0f);
  }
  return outs;
}

// RESIDENT leg (production): rebuild per frame, carry residentFloatListState() across frames.
std::vector<float> cookResidentTrajectory(const std::vector<Frame>& frames) {
  resetResidentFloatListState();
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeDamp(frames[fi], /*withPick=*/true);
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = frames[fi].localFxTime; rc.frameIndex = (uint32_t)fi; rc.lib = &lib;
    cookHostScalarNodes(rg, rc);
    outs.push_back(evalResidentFloat(rg, "3", "Selected", rc));
  }
  return outs;
}

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
      std::printf("[selftest-dampfloatlist] %s FLAT f%zu got=%.5f want=%.5f -> %s\n", label, i, got[i],
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
      std::printf("[selftest-dampfloatlist] %s RES  f%zu got=%.5f want=%.5f -> %s\n", label, i, got[i],
                  frames[i].want, pass ? "PASS" : "FAIL");
    }
  }
  return ok;
}

}  // namespace

int runDampFloatListSelfTest(bool injectBug) {
  bool ok = true;

  // ★ DAMP PROOF: damping=0.5. f0 lfx=1 in=0 → 0 ; f1 lfx=2 in=10 → Lerp(10,prev=0,0.5)=5.0 (HALFWAY, not 10).
  {
    std::vector<Frame> frames = {
        {/*in*/ 0.0f, /*lfx*/ 1.0f, 0.5f, /*want*/ 0.0f},
        {/*in*/ 10.0f, /*lfx*/ 2.0f, 0.5f, /*want*/ 5.0f}};
    ok = runCase("DAMP", frames, injectBug) && ok;
  }

  // RAMP (persistence across changing frames): f2 7.5 reads prev=5 ; f3 13.75 reads prev=7.5.
  {
    std::vector<Frame> frames = {
        {0.0f, 1.0f, 0.5f, 0.0f},
        {10.0f, 2.0f, 0.5f, 5.0f},
        {10.0f, 3.0f, 0.5f, 7.5f},
        {20.0f, 4.0f, 0.5f, 13.75f}};
    ok = runCase("RAMP", frames, injectBug) && ok;
  }

  // ★ DT-GATE PROOF: f2 has the SAME lfx (2.0) as f1 → GATED → re-publish [5.0] (NOT Lerp(10,5,.5)=7.5).
  //   f3 lfx=3.0 PASS → Lerp(10, prev=5, 0.5)=7.5 (proves _dampedValues survived the gated frame at 5, not 7.5).
  {
    std::vector<Frame> frames = {
        {0.0f, 1.0f, 0.5f, 0.0f},
        {10.0f, 2.0f, 0.5f, 5.0f},
        {10.0f, 2.0f, 0.5f, 5.0f},   // GATED (Δlfx=0 < 1/1000) → re-publish prev damped 5.0
        {10.0f, 3.0f, 0.5f, 7.5f}};  // PASS → damp reads the persisted prev=5 → 7.5
    ok = runCase("DTGATE", frames, injectBug) && ok;
  }

  std::printf("[selftest-dampfloatlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
