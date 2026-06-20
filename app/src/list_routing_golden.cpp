// list_routing_golden — --selftest-listrouting. THE BRIDGE golden for the FloatList→Float /
// String→Float seam (list-routing, SEAM_COMPLETION_PLAN §2 stage 1). UNLIKE floatlist_golden /
// string_rail_golden (which only prove TRANSPORT: cook a host value + read it back via
// debugCookedFloatList), this golden proves the BRIDGE: a host-scalar op's cooked Float flows
// DOWNSTREAM into a Float INPUT port and is read by evalFloat (the pure value recursion) — NOT just
// transported.
//
// MECHANISM (mirror of graph_bridge_selftest's AudioReaction probe): the flat cook driver's
// host-scalar branch writes the scalar into Node::outCache (the bridge channel); evalFloat's
// generalised stateful escape hatch (graph.cpp, predicate widened from "AudioReaction" to
// isHostScalarOp) reads it. So the golden: COOK the host-scalar node as terminal (populates outCache),
// then build a downstream value op (Multiply / direct) wired to it and call evalFloat on the
// downstream output — asserting the BRIDGED value, hand-derived from the TiXL .cs (NOT self-consistent).
//
// What this proves (the architecture-defining part of the seam):
//   (1) BRIDGE NOT TRANSPORT: FloatListLength.Length wired into Multiply.a is read by evalFloat as the
//       count (3), so Multiply(3, 2) = 6 — the value crossed from the host-list rail into the Float rail.
//   (2) StringLength downstream now LIVES (string-rail fork-6 closed): StringLength.Length → Multiply
//       reads the length, so length×B flows through evalFloat (was 0 before this seam — evalFloat
//       returned 0 for the unbridged host scalar).
//   (3) PickFloatFromList: [10,20,30] Index 4 → T3 floor-Mod(3)=1 → 20.0, read downstream via evalFloat.
//   (4) BOUNDARIES: empty/unwired list → 0 (FloatListLength + PickFloatFromList), single-element list.
//
// injectBug routes through hostScalarInjectBug() (FloatListLength/PickFloatFromList) + stringInjectBug()
// (StringLength): the RED case corrupts the REAL cooked scalar (sentinel / cleared) so the DOWNSTREAM
// evalFloat reads the wrong value — teeth on the actual bridge path, NOT by flipping the expected value.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"            // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"              // PointGraph::cook
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookHostScalarNodes / evalResidentFloat
#include "runtime/string_op_registry.h"      // stringInjectBug

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Cook `hostScalarId` as the terminal (populates its Node::outCache via the host-scalar branch), then
// evalFloat the downstream output pin — proving the bridge. Returns the downstream evalFloat result.
float cookThenEval(PointGraph& pg, Graph& g, int hostScalarId, int downstreamOutPin) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/hostScalarId);  // writes outCache on the host-scalar node
  return evalFloat(g, downstreamOutPin, ctx);               // pure recursion reads outCache via the wire
}

// RESIDENT-PATH proof (★ the production leg the refuter found missing): mirror the SAME flat Graph into
// a SymbolLibrary (libFromGraph → resident paths == flat node ids as strings), build the resident graph,
// run cookHostScalarNodes (the per-frame production pass frame_cook.cpp drives), then evalResidentFloat
// the downstream node's output slot — the EXACT production path (cookResident's eval uses
// evalResidentFloat). Proves the bridge is LIVE in the running app, not flat-only. `downstreamPath` is
// the resident node path (= flat node id string), `outSlot` its output slot id.
float cookResidentThenEval(Graph& g, const std::string& downstreamPath, const std::string& outSlot) {
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookHostScalarNodes(rg, rc);  // PRODUCTION pass: walks the resident graph, writes extOut on host-scalars
  return evalResidentFloat(rg, downstreamPath, outSlot, rc);  // production eval reads the bridged extOut
}

// Build FloatsToList(vals) → FloatListLength → Multiply(.Length, b). Shared by the flat + resident legs.
// Node ids: FloatsToList=2, FloatListLength=1 (the cooked terminal), Multiply=3.
// FloatListLength ports: [0]=Length(out), [1]=Input(FloatList). Multiply ports: [0]=a,[1]=b,[2]=out.
Graph makeFloatListLengthTimes(const std::vector<float>& vals, float b) {
  Graph g;
  Node fll; fll.id = 1; fll.type = "FloatListLength"; g.nodes.push_back(fll);
  Node ftl; ftl.id = 2; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  Node mul; mul.id = 3; mul.type = "Multiply"; mul.params["b"] = b; g.nodes.push_back(mul);

  // FloatsToList.Input (scalar Float MultiInput, port 0) ← one Const per value (wire-declaration order).
  const int ftlInputPin = pinId(2, /*Input*/ 0);
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 10); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), ftlInputPin});
  }
  // FloatsToList.out (port 1) → FloatListLength.Input (port 1).
  g.connections.push_back({200, pinId(2, /*out*/ 1), pinId(1, /*Input*/ 1)});
  // FloatListLength.Length (port 0) → Multiply.a (port 0). THIS is the bridged wire.
  g.connections.push_back({201, pinId(1, /*Length*/ 0), pinId(3, /*a*/ 0)});
  return g;
}

// FLAT leg: cook FloatListLength as terminal (outCache) → evalFloat(Multiply.out).
float bridgeFloatListLengthTimes(PointGraph& pg, const std::vector<float>& vals, float b) {
  Graph g = makeFloatListLengthTimes(vals, b);
  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Multiply.out*/ pinId(3, 2));
}

// RESIDENT leg (production): cookHostScalarNodes writes FloatListLength.Length onto extOut →
// evalResidentFloat(Multiply.out) reads the bridged count × b. Multiply path "3", out slot "out".
float bridgeFloatListLengthTimesResident(const std::vector<float>& vals, float b) {
  Graph g = makeFloatListLengthTimes(vals, b);
  return cookResidentThenEval(g, /*Multiply path*/ "3", /*Multiply out slot*/ "out");
}

// Build PickFloatFromList(vals, index). Shared by the flat + resident legs. Node id 1 (cooked terminal).
// Ports: [0]=Selected(out), [1]=Input(FloatList), [2]=Index(Float). Index is a stored const param.
Graph makePickFloatFromList(const std::vector<float>& vals, float index) {
  Graph g;
  Node pk; pk.id = 1; pk.type = "PickFloatFromList"; pk.params["Index"] = index; g.nodes.push_back(pk);
  Node ftl; ftl.id = 2; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);

  const int ftlInputPin = pinId(2, /*Input*/ 0);
  int connId = 100;
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = (int)(i + 10); c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), ftlInputPin});
  }
  g.connections.push_back({200, pinId(2, /*out*/ 1), pinId(1, /*Input*/ 1)});  // FloatsToList → Pick.Input
  return g;
}

// FLAT leg: read Pick.Selected (port 0) directly via evalFloat (the bridge: outCache → value pull).
float bridgePickFloatFromList(PointGraph& pg, const std::vector<float>& vals, float index) {
  Graph g = makePickFloatFromList(vals, index);
  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Selected*/ pinId(1, 0));
}

// RESIDENT leg (production): cookHostScalarNodes writes Pick.Selected onto extOut →
// evalResidentFloat reads it directly. Pick path "1", out slot "Selected".
float bridgePickFloatFromListResident(const std::vector<float>& vals, float index) {
  Graph g = makePickFloatFromList(vals, index);
  return cookResidentThenEval(g, /*Pick path*/ "1", /*Selected slot*/ "Selected");
}

// Build FloatToString(value) → StringLength → Multiply(.Length, b). Returns evalFloat(Multiply.out).
// Closes string-rail fork-6 (StringLength.Length downstream接線活). Node ids: StringLength=1 (cooked
// terminal), FloatToString=2, Multiply=3. StringLength ports: [0]=Length(out),[1]=InputString.
float bridgeStringLengthTimes(PointGraph& pg, float value, float b) {
  Graph g;
  Node sl; sl.id = 1; sl.type = "StringLength"; g.nodes.push_back(sl);
  Node fts; fts.id = 2; fts.type = "FloatToString";
  fts.params["Value"] = value; fts.strParams["Format"] = "";  // default ToString
  g.nodes.push_back(fts);
  Node mul; mul.id = 3; mul.type = "Multiply"; mul.params["b"] = b; g.nodes.push_back(mul);

  // FloatToString.Output (port 0) → StringLength.InputString (port 1).
  g.connections.push_back({200, pinId(2, /*Output*/ 0), pinId(1, /*InputString*/ 1)});
  // StringLength.Length (port 0) → Multiply.a (port 0). The bridged wire.
  g.connections.push_back({201, pinId(1, /*Length*/ 0), pinId(3, /*a*/ 0)});

  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Multiply.out*/ pinId(3, 2));
}

}  // namespace

int runListRoutingSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // LEG 1 — FloatListLength BRIDGE (the headline): FloatsToList([1,2,3]) → FloatListLength → Multiply(_,2).
  // FloatListLength.Length = 3 (FloatListLength.cs:23 list.Count); Multiply(3,2) = 6 read via evalFloat.
  // Proves the count crossed the host-list→Float rail (NOT transport — a downstream Float input read it).
  // injectBug: hostScalarInjectBug → cook writes -999 → Multiply(-999,2) = -1998 ≠ 6 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgeFloatListLengthTimes(pg, {1.0f, 2.0f, 3.0f}, 2.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 6.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE FloatListLength([1,2,3])*2 via evalFloat=%.1f want=6.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 2 — FloatListLength BOUNDARY (empty/unwired list): FloatsToList with NO Const wires → empty list
  // → FloatListLength = 0 (FloatListLength.cs null/empty→0); Multiply(0,5) = 0. Proves the empty path.
  // injectBug → -999*5 ≠ 0 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgeFloatListLengthTimes(pg, {}, 5.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 0.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE FloatListLength([])*5 via evalFloat=%.1f want=0.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 3 — FloatListLength BOUNDARY (single element): FloatsToList([7]) → length 1; Multiply(1,4) = 4.
  // injectBug → -999*4 ≠ 4 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgeFloatListLengthTimes(pg, {7.0f}, 4.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 4.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE FloatListLength([7])*4 via evalFloat=%.1f want=4.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 4 — StringLength DOWNSTREAM (string-rail fork-6 closed, the work-order's task #2): FloatToString
  // (3.14,"") → "3.14" (4 chars) → StringLength → Multiply(_,2). StringLength.Length = 4; Multiply(4,2)=8
  // read via evalFloat. BEFORE this seam evalFloat returned 0 for StringLength.Length (unbridged). Now it
  // bridges through outCache. injectBug: stringInjectBug clears the host scalar → outCache 0 → 0*2 = 0
  // ≠ 8 → RED.
  {
    stringInjectBug() = injectBug;
    float got = bridgeStringLengthTimes(pg, 3.14f, 2.0f);
    stringInjectBug() = false;
    bool pass = nearf(got, 8.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE StringLength(\"3.14\")*2 via evalFloat=%.1f want=8.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 5 — PickFloatFromList TYPICAL with T3 floor-Mod wrap (the work-order's #3): [10,20,30], Index 4
  // → 4.Mod(3) = 1 → list[1] = 20.0 (PickFloatFromList.cs:25-27). Read Selected DIRECTLY via evalFloat.
  // injectBug → -999 ≠ 20 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgePickFloatFromList(pg, {10.0f, 20.0f, 30.0f}, 4.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 20.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE PickFloatFromList([10,20,30],idx4→Mod3=1)=%.1f want=20.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 6 — PickFloatFromList NEGATIVE-INDEX floor-Mod (the load-bearing T3-Mod distinction vs C
  // remainder): [10,20,30], Index -1 → (-1)%3 = -1 → +3 → 2 → list[2] = 30.0. A C remainder would give
  // -1 (out of range / crash); T3 floor-Mod wraps to 2. injectBug → -999 ≠ 30 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgePickFloatFromList(pg, {10.0f, 20.0f, 30.0f}, -1.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 30.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE PickFloatFromList([10,20,30],idx-1→Mod3=2)=%.1f want=30.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 7 — PickFloatFromList BOUNDARY (empty list): no wires → empty → Selected = 0 (default(float),
  // PickFloatFromList.cs:19-21). The Mod-by-zero guard never fires (the empty guard returns first).
  // injectBug → -999 ≠ 0 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgePickFloatFromList(pg, {}, 0.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 0.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] BRIDGE PickFloatFromList([],idx0)=%.1f want=0.0 (empty) -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // ===== RESIDENT-PATH legs (★ the PRODUCTION bridge — the refuter found the flat legs above prove a
  // rail with ZERO production callers; the running app cooks via cookResident + evalResidentFloat, and
  // cookHostScalarNodes is the per-frame pass that writes the host-scalar onto extOut). Each mirrors the
  // SAME graph through libFromGraph → buildEvalGraph (resident paths == node ids) → cookHostScalarNodes →
  // evalResidentFloat — the exact production evaluation. injectBug (hostScalarInjectBug) corrupts the
  // scalar INSIDE the cook, so the resident teeth bite too (no flat-only escape). =====

  // R-LEG 1 — FloatListLength BRIDGE on the RESIDENT path: FloatsToList([1,2,3]) → FloatListLength →
  // Multiply(_,2) = 6, read via evalResidentFloat (production). This is the value that was 0 before the
  // fix (extOut never written on the resident side). injectBug → -999*2 = -1998 ≠ 6 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgeFloatListLengthTimesResident({1.0f, 2.0f, 3.0f}, 2.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 6.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] RESIDENT FloatListLength([1,2,3])*2 via evalResidentFloat=%.1f want=6.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // R-LEG 2 — FloatListLength BOUNDARY (empty list) on the RESIDENT path: no Const wires → empty →
  // length 0; Multiply(0,5) = 0. injectBug → -999*5 ≠ 0 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgeFloatListLengthTimesResident({}, 5.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 0.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] RESIDENT FloatListLength([])*5 via evalResidentFloat=%.1f want=0.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // R-LEG 3 — FloatListLength BOUNDARY (single element) on the RESIDENT path: [7] → length 1;
  // Multiply(1,4) = 4. injectBug → -999*4 ≠ 4 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgeFloatListLengthTimesResident({7.0f}, 4.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 4.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] RESIDENT FloatListLength([7])*4 via evalResidentFloat=%.1f want=4.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // R-LEG 5 — PickFloatFromList TYPICAL (T3 floor-Mod) on the RESIDENT path: [10,20,30], Index 4 →
  // 4.Mod(3)=1 → 20.0. The Index param resolves through resolveResidentFloatInputs (the value spine).
  // injectBug → -999 ≠ 20 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgePickFloatFromListResident({10.0f, 20.0f, 30.0f}, 4.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 20.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] RESIDENT PickFloatFromList([10,20,30],idx4→Mod3=1)=%.1f want=20.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // R-LEG 6 — PickFloatFromList NEGATIVE-INDEX floor-Mod on the RESIDENT path: [10,20,30], Index -1 →
  // (-1).Mod(3)=2 → 30.0. injectBug → -999 ≠ 30 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgePickFloatFromListResident({10.0f, 20.0f, 30.0f}, -1.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 30.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] RESIDENT PickFloatFromList([10,20,30],idx-1→Mod3=2)=%.1f want=30.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // R-LEG 7 — PickFloatFromList BOUNDARY (empty list) on the RESIDENT path: no wires → empty →
  // Selected = 0. injectBug → -999 ≠ 0 → RED.
  {
    hostScalarInjectBug() = injectBug;
    float got = bridgePickFloatFromListResident({}, 0.0f);
    hostScalarInjectBug() = false;
    bool pass = nearf(got, 0.0f);
    ok = ok && pass;
    std::printf("[selftest-listrouting] RESIDENT PickFloatFromList([],idx0)=%.1f want=0.0 (empty) -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // NOTE on StringLength (the work-order's task #2 "StringLength fork-6 resident"): NO resident leg is
  // added, BY DESIGN. StringLength's value rides extOut on the FLAT path (cookStringLength writes
  // Node::outCache), but the resident flatten DROPS its String wire (FloatToString.Output →
  // StringLength.InputString) — resident_eval_flatten.cpp:100-103 gives String slots a resolved-constant
  // strInputs, never a Connection driver. So cookHostScalarNodes cannot follow that wire and SKIPS
  // StringLength (it has a String input port); a resident StringLength reads extOut == 0. Adding a
  // resident leg that "passed" would require either faking it (read the strDef constant, NOT the wired
  // value = self-deception) or building the resident STRING-wire rail first (a separate seam). The
  // honest status: StringLength's bridge is FLAT-ONLY today; its resident bridge is blocked on the
  // resident string-wire rail (reported to orchestrator).

  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // corrupts the REAL cooked scalar → downstream evalFloat reads wrong → ok false → return 1 (teeth).
  std::printf("[selftest-listrouting] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
