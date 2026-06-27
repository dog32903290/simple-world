// floatlist_keepfloatvalues_golden — --selftest-keepfloatvalues. MULTI-FRAME golden for KeepFloatValues, a
// cross-frame STATE consumer on the FLOATLIST rail (sibling of AmplifyValues). The persistent accumulator
// `_list` IS the point, so this drives the SAME node across frames carrying the per-node FloatListState
// (FloatListState::keepList), on BOTH the flat cook (PointGraph reused → Impl::floatListState persists) AND
// the PRODUCTION resident path (rebuild per frame, carry ONE residentFloatListState() process store).
//
// TiXL authority (KeepFloatValues.cs): each frame (AddValueToList=true, the .t3 default) Value is inserted at
// the FRONT of _list and the list is trimmed to BufferLength — a shift register / ring buffer. So reading
// index k after pushing v0,v1,...,vN returns the value pushed (N-k) frames ago. HAND-COMPUTED with
// BufferLength=3, DefaultValue=0, AddValueToList=true (the list seeds to [0,0,0] then accumulates):
//
//   push sequence (Value per frame): f0=10, f1=20, f2=30, f3=40
//     f0: grow→[0,0,0] ; insert 10→[10,0,0,0] ; trim→[10, 0, 0]
//     f1: insert 20→[20,10,0,0]              ; trim→[20,10, 0]
//     f2: insert 30→[30,20,10,0]             ; trim→[30,20,10]
//     f3: insert 40→[40,30,20,10]            ; trim→[40,30,20]
//
//   ★ THE FRONT-PUSH PROOF (read INDEX 0 each frame): f0→10, f1→20, f2→30, f3→40 (the newest value is at 0).
//   ★ THE PERSISTENCE PROOF (read INDEX 2 — the history): f2→10, f3→20. Index 2 holds the value pushed 2
//     frames earlier — that value is ONLY there if _list PERSISTED across frames. With NO persistence (a
//     fresh list each frame) index 2 = defaultValue 0 ≠ 10 → the assert FAILS. So the clean index-2 PASS is
//     the direct proof the FloatListState::keepList slot persists frame→frame.
//
//   injectBug routes through floatListInjectBug(): the leaf accumulates into a FRESH scratch (not the
//   persisted _list) → the history vanishes → index 2 reads defaultValue 0 (≠ the clean want 10) → RED on the
//   actual cook path. (KeepFloatValues has no FloatList INPUT wire, so the upstream-drop teeth do not apply;
//   the leaf's own no-persistence branch is the tooth.)
//
//   KeepFloatValues has NO FloatList input wire — Value is a scalar Float PARAM. So the graph is just the
//   KeepFloatValues node (id=1) + a downstream PickFloatFromList(id=3) reading its out; the per-frame Value is
//   set directly on node 1's params.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedFloatList
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat
#include "runtime/resident_value_cooks.h"    // resetResidentFloatListState

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// One per-frame configuration: the Value pushed this frame; assert the picked element AFTER this frame.
struct Frame {
  float value;      // KeepFloatValues.Value this frame (front-inserted)
  int pickIndex;    // which element to read out via PickFloatFromList
  float want;       // expected list[pickIndex] AFTER this frame (clean)
};

// Build KeepFloatValues(id=1) + PickFloatFromList(id=3) ← KeepFloatValues.out. BufferLength=3, default 0.
//   KeepFloatValues ports: [0]=out(FloatList), [1]=Value, [2]=AddValueToList, [3]=BufferLength, [4]=Reset, [5]=DefaultValue.
Graph makeKeep(const Frame& f) {
  Graph g;
  Node kp; kp.id = 1; kp.type = "KeepFloatValues";
  kp.params["Value"] = f.value;
  kp.params["AddValueToList"] = 1.0f;  // .t3 default TRUE
  kp.params["BufferLength"] = 3.0f;    // small for a hand-checkable trajectory
  kp.params["Reset"] = 0.0f;
  kp.params["DefaultValue"] = 0.0f;
  g.nodes.push_back(kp);
  Node pk; pk.id = 3; pk.type = "PickFloatFromList"; pk.params["Index"] = (float)f.pickIndex;
  g.nodes.push_back(pk);
  g.connections.push_back({100, pinId(1, /*out*/ 0), pinId(3, /*Input*/ 1)});  // Keep.out → Pick.Input
  return g;
}

// FLAT leg: ONE PointGraph reused (so Impl::floatListState["#1"] persists). Read the picked element each frame
// via the flat evalFloat on PickFloatFromList's Selected (port 0).
std::vector<float> cookFlatTrajectory(PointGraph& pg, const std::vector<Frame>& frames) {
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeKeep(frames[fi]);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f; ctx.localFxTime = (float)fi;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/3);  // PickFloatFromList is the terminal (drives the cook of Keep)
    outs.push_back(evalFloat(g, pinId(3, /*Selected*/ 0), ctx));
  }
  return outs;
}

// RESIDENT leg (production): rebuild per frame, carry residentFloatListState() across frames.
std::vector<float> cookResidentTrajectory(const std::vector<Frame>& frames) {
  resetResidentFloatListState();
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeKeep(frames[fi]);
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = (float)fi; rc.frameIndex = (uint32_t)fi; rc.lib = &lib;
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
      std::printf("[selftest-keepfloatvalues] %s FLAT f%zu idx=%d got=%.5f want=%.5f -> %s\n", label, i,
                  frames[i].pickIndex, got[i], frames[i].want, pass ? "PASS" : "FAIL");
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
      std::printf("[selftest-keepfloatvalues] %s RES  f%zu idx=%d got=%.5f want=%.5f -> %s\n", label, i,
                  frames[i].pickIndex, got[i], frames[i].want, pass ? "PASS" : "FAIL");
    }
  }
  return ok;
}

}  // namespace

int runKeepFloatValuesSelfTest(bool injectBug) {
  bool ok = true;

  // ★ FRONT-PUSH PROOF (read INDEX 0): the newest value is always at the front. f0=10→10, f1=20→20, ...
  {
    std::vector<Frame> frames = {
        {/*v*/ 10.0f, /*idx*/ 0, /*want*/ 10.0f},
        {20.0f, 0, 20.0f},
        {30.0f, 0, 30.0f},
        {40.0f, 0, 40.0f}};
    ok = runCase("FRONT", frames, injectBug) && ok;
  }

  // ★ PERSISTENCE PROOF (read INDEX 2 — the cross-frame history). Push 10,20,30,40; index 2 holds the value
  //   pushed 2 frames ago: f0→0 (default, not enough history) ; f1→0 ; f2→10 ; f3→20. The f2/f3 wants are the
  //   load-bearing assertions: WITHOUT persistence index 2 = default 0 → RED.
  {
    std::vector<Frame> frames = {
        {10.0f, 2, 0.0f},   // list [10,0,0] → idx2 = 0
        {20.0f, 2, 0.0f},   // list [20,10,0] → idx2 = 0
        {30.0f, 2, 10.0f},  // list [30,20,10] → idx2 = 10 (the value pushed at f0 — PERSISTED)
        {40.0f, 2, 20.0f}};  // list [40,30,20] → idx2 = 20 (the value pushed at f1 — PERSISTED)
    ok = runCase("HISTORY", frames, injectBug) && ok;
  }

  std::printf("[selftest-keepfloatvalues] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
