// hasstringchanged_golden — --selftest-hasstringchanged. MULTI-FRAME golden for HasStringChanged, the
// FIRST cross-frame STATE consumer on the STRING rail (the string twin of KeepColors on the colorlist
// rail). The state IS the whole point, so this drives the SAME node across 3 frames carrying the per-node
// `_lastString`, on the PRODUCTION resident path (rebuild the resident graph per frame with the new Value
// override, but carry ONE s_stringState map — the EXACT pattern of the KeepColors production golden,
// keepcolors_golden.cpp:104-123 / the AnimInt production golden) AND on the flat cook (one PointGraph
// reused across cook() calls → Impl::stringState persists).
//
// TiXL authority (string/logic/HasStringChanged.cs): each frame hasChanged = (newString != _lastString);
// then _lastString = newString. Hand-computed 3-frame trajectory (Value driven via strParams["Value"]):
//   frame0 "A": _lastString init "" → "A" != "" → CHANGED (1); store "A".
//   frame1 "A": "A" != "A"          → UNCHANGED (0); store "A".
//   frame2 "B": "B" != "A"          → CHANGED (1); store "B".
// The per-frame answer is right ONLY if `_lastString` PERSISTED across frames (without state every frame
// would compare against a fresh "" → frame1 "A" vs "" = CHANGED, wrong).
//
// ★R-2: the RESIDENT leg proves the per-node state PERSISTS on the PRODUCTION path (cookStringNodes + the
// s_stringState the running app threads via cook_host_values.cpp), not flat-only. RED tooth: injectBug
// routes through stringInjectBug() — HasStringChanged then SKIPS the `_lastString = newString` store on the
// REAL state slot → frame1 compares "A" against the STALE "" (never updated) → mis-reports CHANGED (1) when
// the truth is UNCHANGED (0). Teeth on the actual cross-frame state path (NOT by flipping the expected).
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (R-2 resident leg)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph / Node
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph::cook (flat leg)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / cookStringNodes / ResidentEvalGraph (R-2)
#include "runtime/string_op_registry.h"   // StringState / stringInjectBug

namespace sw {
namespace {

// The strings fed over frames (distinct so the trajectory pins the cross-frame compare, not a constant).
const char* kFrames3[3] = {"A", "A", "B"};
// Expected HasChanged per frame: changed vs the PREVIOUS frame's stored string (init "").
//   f0 "A" vs ""  -> 1 ;  f1 "A" vs "A" -> 0 ;  f2 "B" vs "A" -> 1.
const float kWant3[3] = {1.0f, 0.0f, 1.0f};

// Build a single HasStringChanged node (id 1) with the given Value as the stored String const (the
// wire-OR-const path: unwired Value → strInputs["Value"] override). Same node id every frame → flatKey
// ("#1") / resident path "1" stays stable → the `_lastString` state persists across cooks.
Graph makeHasStringChanged(const char* value) {
  Graph g;
  Node n; n.id = 1; n.type = "HasStringChanged";
  n.strParams["Value"] = value;  // String const override (unwired Value input)
  g.nodes.push_back(n);
  return g;
}

bool nearf(float a, float b) { return (a > b ? a - b : b - a) < 1e-5f; }

// ★R-2 RESIDENT leg (production): rebuild the resident graph each frame with the new Value override, but
// carry ONE s_stringState map across frames (mirror of KeepColors's s_colorListState carry). Returns the
// HasChanged scalar off node "1"'s extOut[0] for EACH frame (so every frame's answer is asserted).
std::vector<float> cookResidentTrajectory(const char* const* values, int n) {
  std::map<std::string, StringState> s_stringState;  // the cross-frame `_lastString` store
  std::vector<float> out;
  for (int fi = 0; fi < n; ++fi) {
    Graph g = makeHasStringChanged(values[fi]);
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = (uint32_t)fi; rc.lib = &lib;
    cookStringNodes(rg, rc, &s_stringState);  // PRODUCTION pass, state carried across frames
    const ResidentNode* nd = rg.node("1");
    // HasStringChanged's HasChanged scalar (bool→Float) rides extOut[0] (its only output, port 0).
    out.push_back(nd ? nd->extOut[0] : -1.0f);
  }
  return out;
}

// FLAT leg: one PointGraph reused across all frames (so Impl::stringState["#1"] persists). Cook each
// frame's graph with HasStringChanged as the terminal; the bool→Float rides Node::outCache[0]. Returns
// the per-frame HasChanged scalar.
std::vector<float> cookFlatTrajectory(PointGraph& pg, const char* const* values, int n) {
  std::vector<float> out;
  for (int fi = 0; fi < n; ++fi) {
    Graph g = makeHasStringChanged(values[fi]);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);  // HasStringChanged terminal → cookStringNode
    const Node* nd = g.node(1);
    out.push_back(nd ? nd->outCache[0] : -1.0f);  // bool→Float on the flat host-scalar/bridge channel
  }
  return out;
}

// Run BOTH legs over the 3-frame trajectory; assert every frame matches `want`. Returns true on PASS.
bool runCase(bool injectBug) {
  bool ok = true;
  // RESIDENT (production) leg.
  {
    stringInjectBug() = injectBug;
    std::vector<float> got = cookResidentTrajectory(kFrames3, 3);
    stringInjectBug() = false;
    for (int i = 0; i < 3; ++i) {
      bool pass = nearf(got[i], kWant3[i]);
      ok = ok && pass;
      std::printf("[selftest-hasstringchanged] RESIDENT f%d \"%s\" HasChanged=%.0f want=%.0f -> %s\n",
                  i, kFrames3[i], got[i], kWant3[i], pass ? "PASS" : "FAIL");
    }
  }
  // FLAT leg.
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    stringInjectBug() = injectBug;
    std::vector<float> got = cookFlatTrajectory(pg, kFrames3, 3);
    stringInjectBug() = false;
    for (int i = 0; i < 3; ++i) {
      bool pass = nearf(got[i], kWant3[i]);
      ok = ok && pass;
      std::printf("[selftest-hasstringchanged] FLAT     f%d \"%s\" HasChanged=%.0f want=%.0f -> %s\n",
                  i, kFrames3[i], got[i], kWant3[i], pass ? "PASS" : "FAIL");
    }
    q->release(); dev->release(); pool->release();
  }
  return ok;
}

}  // namespace

int runHasStringChangedSelfTest(bool injectBug) {
  // The teeth bite on frame 1: under injectBug the `_lastString` store is skipped, so frame1 "A" compares
  // against the stale "" (never updated to "A") → mis-reports CHANGED (1) when the truth is UNCHANGED (0).
  // We do NOT flip the expected — kWant3 stays {1,0,1}; the bug makes the REAL cross-frame state wrong.
  bool ok = runCase(injectBug);
  std::printf("[selftest-hasstringchanged] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
