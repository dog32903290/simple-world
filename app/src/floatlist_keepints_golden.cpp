// floatlist_keepints_golden — --selftest-keepints. MULTI-FRAME golden for KeepInts, the INT twin of
// KeepFloatValues: a cross-frame STATE consumer on the FLOATLIST rail. The persistent accumulator `_list`
// IS the point, so this drives the SAME node across frames carrying the per-node FloatListState
// (FloatListState::keepList), on BOTH the flat cook (PointGraph reused → Impl::floatListState persists) AND
// the PRODUCTION resident path (rebuild per frame, carry ONE residentFloatListState() process store).
//
// TiXL authority (KeepInts.cs): each frame (AddValueToList=true, the .t3 default) Value is inserted at the
// FRONT of _list (cs:37-38) and the list is trimmed to BufferLength (cs:40-43) — a shift register / ring
// buffer, GROW-padded with LITERAL 0 (cs:33). So reading index k after pushing v0,v1,...,vN returns the
// value pushed (N-k) frames ago. HAND-COMPUTED with BufferLength=3, AddValueToList=true (the list seeds to
// [0,0,0] then accumulates):
//
//   push sequence (Value per frame): f0=7, f1=8, f2=9, f3=11
//     f0: grow→[0,0,0] ; insert 7→[7,0,0,0] ; trim→[7, 0, 0]
//     f1: insert 8→[8,7,0,0]              ; trim→[8, 7, 0]
//     f2: insert 9→[9,8,7,0]              ; trim→[9, 8, 7]
//     f3: insert 11→[11,9,8,7]            ; trim→[11, 9, 8]
//
//   ★ THE FRONT-PUSH PROOF (read INDEX 0 each frame): f0→7, f1→8, f2→9, f3→11 (the newest value is at 0).
//     probe坐發散中段 — the pushed value CHANGES每 frame away from any identity/zero, so a broken front-
//     insert (e.g. push-to-back, or no-op) reads the wrong number → RED.
//   ★ THE PERSISTENCE PROOF (read INDEX 2 — the cross-frame history): f2→7, f3→8. Index 2 holds the value
//     pushed 2 frames earlier — that value is ONLY there if _list PERSISTED across frames. With NO
//     persistence (a fresh list each frame) index 2 = pad 0 ≠ 7 → the assert FAILS. So the clean index-2
//     PASS is the direct proof the FloatListState::keepList slot persists frame→frame.
//
//   injectBug routes through floatListInjectBug(): the leaf accumulates into a FRESH scratch (not the
//   persisted _list) → the history vanishes → index 2 reads pad 0 (≠ the clean want 7) → RED on the actual
//   cook path (NOT a flipped expected value). KeepInts has no FloatList INPUT wire, so upstream-drop teeth
//   do not apply; the leaf's own no-persistence branch is the tooth.
//
//   KeepInts has NO FloatList input wire — Value is a scalar Float PARAM. So the graph is just the KeepInts
//   node (id=1) + a downstream PickFloatFromList(id=3) reading its out; the per-frame Value is set directly
//   on node 1's params. (Structurally identical to the KeepFloatValues golden.)
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph
#include "runtime/point_graph.h"             // PointGraph::cook
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat
#include "runtime/resident_value_cooks.h"    // resetResidentFloatListState

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// One per-frame configuration: the Value pushed this frame; assert the picked element AFTER this frame.
struct Frame {
  float value;      // KeepInts.Value this frame (front-inserted)
  int pickIndex;    // which element to read out via PickFloatFromList
  float want;       // expected list[pickIndex] AFTER this frame (clean)
};

// Build KeepInts(id=1) + PickFloatFromList(id=3) ← KeepInts.out. BufferLength=3.
//   KeepInts ports: [0]=out(FloatList), [1]=Value, [2]=AddValueToList, [3]=BufferLength, [4]=Reset.
Graph makeKeep(const Frame& f) {
  Graph g;
  Node kp; kp.id = 1; kp.type = "KeepInts";
  kp.params["Value"] = f.value;
  kp.params["AddValueToList"] = 1.0f;  // .t3 default TRUE
  kp.params["BufferLength"] = 3.0f;    // small for a hand-checkable trajectory
  kp.params["Reset"] = 0.0f;
  g.nodes.push_back(kp);
  Node pk; pk.id = 3; pk.type = "PickFloatFromList"; pk.params["Index"] = (float)f.pickIndex;
  g.nodes.push_back(pk);
  g.connections.push_back({100, pinId(1, /*out*/ 0), pinId(3, /*Input*/ 1)});  // Keep.out → Pick.Input
  return g;
}

// FLAT leg: ONE PointGraph reused (so Impl::floatListState["#1"] persists). Read the picked element each
// frame via the flat evalFloat on PickFloatFromList's Selected (port 0).
std::vector<float> cookFlatTrajectory(PointGraph& pg, const std::vector<Frame>& frames) {
  std::vector<float> outs;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeKeep(frames[fi]);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f; ctx.localFxTime = (float)fi;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/3);  // PickFloatFromList is the terminal (drives Keep's cook)
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
      std::printf("[selftest-keepints] %s FLAT f%zu idx=%d got=%.5f want=%.5f -> %s\n", label, i,
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
      std::printf("[selftest-keepints] %s RES  f%zu idx=%d got=%.5f want=%.5f -> %s\n", label, i,
                  frames[i].pickIndex, got[i], frames[i].want, pass ? "PASS" : "FAIL");
    }
  }
  return ok;
}

}  // namespace

int runKeepIntsSelfTest(bool injectBug) {
  bool ok = true;

  // ★ FRONT-PUSH PROOF (read INDEX 0): the newest value is always at the front. f0=7→7, f1=8→8, ...
  {
    std::vector<Frame> frames = {
        {/*v*/ 7.0f, /*idx*/ 0, /*want*/ 7.0f},
        {8.0f, 0, 8.0f},
        {9.0f, 0, 9.0f},
        {11.0f, 0, 11.0f}};
    ok = runCase("FRONT", frames, injectBug) && ok;
  }

  // ★ PERSISTENCE PROOF (read INDEX 2 — the cross-frame history). Push 7,8,9,11; index 2 holds the value
  //   pushed 2 frames ago: f0→0 (pad, not enough history) ; f1→0 ; f2→7 ; f3→8. The f2/f3 wants are the
  //   load-bearing assertions: WITHOUT persistence index 2 = pad 0 → RED.
  {
    std::vector<Frame> frames = {
        {7.0f, 2, 0.0f},   // list [7,0,0]  → idx2 = 0
        {8.0f, 2, 0.0f},   // list [8,7,0]  → idx2 = 0
        {9.0f, 2, 7.0f},   // list [9,8,7]  → idx2 = 7  (the value pushed at f0 — PERSISTED)
        {11.0f, 2, 8.0f}};  // list [11,9,8] → idx2 = 8  (the value pushed at f1 — PERSISTED)
    ok = runCase("HISTORY", frames, injectBug) && ok;
  }

  std::printf("[selftest-keepints] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
