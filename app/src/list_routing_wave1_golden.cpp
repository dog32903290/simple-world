// list_routing_wave1_golden — --selftest-listroutingwave1. BRIDGE goldens for the list fan-out WAVE-1
// host-scalar consumers ported on the ALREADY-BUILT list-seam: SumRange / IntListLength / PickIntFromList /
// CompareFloatLists. Sibling of list_routing_golden.cpp (FloatListLength / PickFloatFromList / StringLength)
// — split into its own TU to honour the ≤400-line ratchet (ARCHITECTURE.md rule 4) + "一個檔一個職責".
//
// SAME CONTRACT as list_routing_golden: each op is a SINGLE-output FloatList→Float host-scalar consumer
// (HostScalarCookCtx::output, one float). The golden proves the BRIDGE (not transport): cook the op as the
// terminal (the host-scalar branch writes the scalar into Node::outCache on the flat path / extOut on the
// resident path), then read the op's Float output DOWNSTREAM via evalFloat (flat) / evalResidentFloat
// (resident). Expected values are hand-derived from each TiXL .cs Update() (NOT self-consistent).
//
// TiXL authorities:
//   • SumRange.cs           (numbers/floats/process) — clamped windowed sum, lower=max(0,L), upper=min(Count,U).
//   • IntListLength.cs      (numbers/ints)           — list.Count (int-fold twin of FloatListLength).
//   • PickIntFromList.cs    (numbers/ints)           — list[Index.Mod(Count)] (int-fold twin of PickFloatFromList).
//   • CompareFloatLists.cs  (numbers/floats/process) — differing-element ratio of two lists vs Threshold.
//
// injectBug routes through hostScalarInjectBug(): the RED case corrupts the REAL cooked scalar (sentinel)
// so the DOWNSTREAM evalFloat reads the wrong value — teeth on the actual bridge path, NOT by flipping the
// expected value. Both the flat AND resident legs bite (no flat-only escape).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"             // EvaluationContext
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId + evalFloat
#include "runtime/graph_bridge.h"             // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug
#include "runtime/point_graph.h"              // PointGraph::cook
#include "runtime/resident_eval_graph.h"      // buildEvalGraph / cookHostScalarNodes / evalResidentFloat

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Cook `hostScalarId` as terminal (populates Node::outCache via the host-scalar branch), then evalFloat the
// downstream output pin — the FLAT bridge. (Same helper as list_routing_golden.cpp; copied for TU isolation.)
float cookThenEval(PointGraph& pg, Graph& g, int hostScalarId, int downstreamOutPin) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/hostScalarId);  // writes outCache on the host-scalar node
  return evalFloat(g, downstreamOutPin, ctx);               // pure recursion reads outCache via the wire
}

// RESIDENT (production) bridge: mirror the flat Graph into a SymbolLibrary (resident paths == flat node ids),
// build the resident graph, run cookHostScalarNodes (the per-frame production pass), then evalResidentFloat
// the downstream slot — the EXACT running-app path. (Same helper as list_routing_golden.cpp.)
float cookResidentThenEval(Graph& g, const std::string& downstreamPath, const std::string& outSlot) {
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookHostScalarNodes(rg, rc);
  return evalResidentFloat(rg, downstreamPath, outSlot, rc);
}

// One Const(vals[i]) per value, wired into FloatsToList(ftlId).Input (port 0) in wire-declaration order.
// `nodeBase`/`connBase` keep ids unique when two FloatsToList feed one op (CompareFloatLists).
void wireFloatsToList(Graph& g, int ftlId, const std::vector<float>& vals, int nodeBase, int connBase) {
  const int ftlInputPin = pinId(ftlId, /*Input*/ 0);
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = nodeBase + (int)i; c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connBase + (int)i, pinId(c.id, /*out*/ 1), ftlInputPin});
  }
}

// --- SumRange: FloatsToList(vals) → SumRange(Lower,Upper). Node ids: SumRange=1 (terminal), FloatsToList=2.
// SumRange ports: [0]=Selected(out),[1]=Input,[2]=LowerLimit,[3]=UpperLimit.
Graph makeSumRange(const std::vector<float>& vals, float lower, float upper) {
  Graph g;
  Node sr; sr.id = 1; sr.type = "SumRange";
  sr.params["LowerLimit"] = lower; sr.params["UpperLimit"] = upper; g.nodes.push_back(sr);
  Node ftl; ftl.id = 2; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  wireFloatsToList(g, 2, vals, /*nodeBase*/ 10, /*connBase*/ 100);
  g.connections.push_back({200, pinId(2, /*out*/ 1), pinId(1, /*Input*/ 1)});  // FloatsToList → SumRange.Input
  return g;
}
float bridgeSumRange(PointGraph& pg, const std::vector<float>& vals, float lo, float hi) {
  Graph g = makeSumRange(vals, lo, hi);
  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Selected*/ pinId(1, 0));
}
float bridgeSumRangeResident(const std::vector<float>& vals, float lo, float hi) {
  Graph g = makeSumRange(vals, lo, hi);
  return cookResidentThenEval(g, /*SumRange path*/ "1", /*Selected slot*/ "Selected");
}

// --- IntListLength: FloatsToList(vals) → IntListLength → Multiply(.Length, b). Proves the int-fold count
// bridges the SAME as FloatListLength. Node ids: IntListLength=1 (terminal), FloatsToList=2, Multiply=3.
Graph makeIntListLengthTimes(const std::vector<float>& vals, float b) {
  Graph g;
  Node ill; ill.id = 1; ill.type = "IntListLength"; g.nodes.push_back(ill);
  Node ftl; ftl.id = 2; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  Node mul; mul.id = 3; mul.type = "Multiply"; mul.params["b"] = b; g.nodes.push_back(mul);
  wireFloatsToList(g, 2, vals, /*nodeBase*/ 10, /*connBase*/ 100);
  g.connections.push_back({200, pinId(2, /*out*/ 1), pinId(1, /*Input*/ 1)});  // FloatsToList → IntListLength.Input
  g.connections.push_back({201, pinId(1, /*Length*/ 0), pinId(3, /*a*/ 0)});   // IntListLength.Length → Multiply.a
  return g;
}
float bridgeIntListLengthTimes(PointGraph& pg, const std::vector<float>& vals, float b) {
  Graph g = makeIntListLengthTimes(vals, b);
  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Multiply.out*/ pinId(3, 2));
}
float bridgeIntListLengthTimesResident(const std::vector<float>& vals, float b) {
  Graph g = makeIntListLengthTimes(vals, b);
  return cookResidentThenEval(g, /*Multiply path*/ "3", /*Multiply out slot*/ "out");
}

// --- PickIntFromList: FloatsToList(vals) → PickIntFromList(Index). Node ids: Pick=1 (terminal), FloatsToList=2.
// Ports: [0]=Selected(out),[1]=Input,[2]=Index.
Graph makePickIntFromList(const std::vector<float>& vals, float index) {
  Graph g;
  Node pk; pk.id = 1; pk.type = "PickIntFromList"; pk.params["Index"] = index; g.nodes.push_back(pk);
  Node ftl; ftl.id = 2; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  wireFloatsToList(g, 2, vals, /*nodeBase*/ 10, /*connBase*/ 100);
  g.connections.push_back({200, pinId(2, /*out*/ 1), pinId(1, /*Input*/ 1)});  // FloatsToList → Pick.Input
  return g;
}
float bridgePickIntFromList(PointGraph& pg, const std::vector<float>& vals, float index) {
  Graph g = makePickIntFromList(vals, index);
  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Selected*/ pinId(1, 0));
}
float bridgePickIntFromListResident(const std::vector<float>& vals, float index) {
  Graph g = makePickIntFromList(vals, index);
  return cookResidentThenEval(g, /*Pick path*/ "1", /*Selected slot*/ "Selected");
}

// --- CompareFloatLists: FloatsToList(A)→.ListA + FloatsToList(B)→.ListB, Threshold param. Node ids:
// CompareFloatLists=1 (terminal), FloatsToList(A)=2, FloatsToList(B)=3. Ports: [0]=Difference,[1]=ListA,
// [2]=ListB,[3]=Threshold.
Graph makeCompareFloatLists(const std::vector<float>& a, const std::vector<float>& b, float threshold) {
  Graph g;
  Node cmp; cmp.id = 1; cmp.type = "CompareFloatLists"; cmp.params["Threshold"] = threshold; g.nodes.push_back(cmp);
  Node ftlA; ftlA.id = 2; ftlA.type = "FloatsToList"; g.nodes.push_back(ftlA);
  Node ftlB; ftlB.id = 3; ftlB.type = "FloatsToList"; g.nodes.push_back(ftlB);
  wireFloatsToList(g, 2, a, /*nodeBase*/ 10, /*connBase*/ 100);   // A consts: ids 10.., conns 100..
  wireFloatsToList(g, 3, b, /*nodeBase*/ 50, /*connBase*/ 150);   // B consts: ids 50.., conns 150.. (disjoint)
  g.connections.push_back({200, pinId(2, /*out*/ 1), pinId(1, /*ListA*/ 1)});  // FloatsToList(A) → ListA
  g.connections.push_back({201, pinId(3, /*out*/ 1), pinId(1, /*ListB*/ 2)});  // FloatsToList(B) → ListB
  return g;
}
float bridgeCompareFloatLists(PointGraph& pg, const std::vector<float>& a, const std::vector<float>& b, float t) {
  Graph g = makeCompareFloatLists(a, b, t);
  return cookThenEval(pg, g, /*hostScalarId=*/1, /*Difference*/ pinId(1, 0));
}
float bridgeCompareFloatListsResident(const std::vector<float>& a, const std::vector<float>& b, float t) {
  Graph g = makeCompareFloatLists(a, b, t);
  return cookResidentThenEval(g, /*Compare path*/ "1", /*Difference slot*/ "Difference");
}

// Run one leg: set/clear injectBug around the bridge call, compare to `want`, log, fold into `ok`.
void leg(bool& ok, bool injectBug, const char* tag, float got, float want) {
  bool pass = nearf(got, want);
  ok = ok && pass;
  std::printf("[selftest-listroutingwave1] %s=%.4f want=%.4f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
}

}  // namespace

int runListRoutingWave1SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  bool ok = true;

  // ---- SumRange (SumRange.cs:21-27) ----
  // TYPICAL: [1,2,3,4,5], lower 1, upper 4 → indices {1,2,3} → 2+3+4 = 9.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeSumRange(pg, {1, 2, 3, 4, 5}, 1, 4); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE SumRange([1..5],lo1,hi4)", g, 9.0f); }
  // LOWER-CLAMP (Math.Max(0,Lower)): [10,20], lower -5 → 0, upper 2 → 10+20 = 30.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeSumRange(pg, {10, 20}, -5, 2); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE SumRange([10,20],lo-5->0,hi2)", g, 30.0f); }
  // BOUNDARY empty (cs:17-20 → 0): no wires → 0.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeSumRange(pg, {}, 0, 10); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE SumRange([],lo0,hi10)", g, 0.0f); }

  // ---- IntListLength (IntListLength.cs:22, int-fold) ----
  // [5,5,5,5] → length 4; Multiply(4,3) = 12.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeIntListLengthTimes(pg, {5, 5, 5, 5}, 3); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE IntListLength([5,5,5,5])*3", g, 12.0f); }
  // BOUNDARY empty (cs:17-19 → 0): Multiply(0,7) = 0.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeIntListLengthTimes(pg, {}, 7); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE IntListLength([])*7", g, 0.0f); }

  // ---- PickIntFromList (PickIntFromList.cs:25-27, T3 floor-Mod, int-fold) ----
  // [10,20,30], Index 4 → 4.Mod(3)=1 → 20.
  hostScalarInjectBug() = injectBug;
  { float g = bridgePickIntFromList(pg, {10, 20, 30}, 4); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE PickIntFromList([10,20,30],idx4->Mod3=1)", g, 20.0f); }
  // NEGATIVE-INDEX floor-Mod: Index -1 → (-1).Mod(3)=2 → 30.
  hostScalarInjectBug() = injectBug;
  { float g = bridgePickIntFromList(pg, {10, 20, 30}, -1); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE PickIntFromList([10,20,30],idx-1->Mod3=2)", g, 30.0f); }
  // BOUNDARY empty (cs:19-21 → 0).
  hostScalarInjectBug() = injectBug;
  { float g = bridgePickIntFromList(pg, {}, 0); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE PickIntFromList([],idx0)", g, 0.0f); }

  // ---- CompareFloatLists (CompareFloatLists.cs:20-47) ----
  // IDENTICAL (equal-length, the faithful regime — quirk guard never fires): [1,2,3]==[1,2,3], t0 → 0/3 = 0.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeCompareFloatLists(pg, {1, 2, 3}, {1, 2, 3}, 0); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE CompareFloatLists([1,2,3]==[1,2,3],t0)", g, 0.0f); }
  // ONE DIFFERING (cs:41-47): [1,2,3] vs [1,9,3], t0.5 → |2-9|=7>0.5 → 1/3 = 0.3333.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeCompareFloatLists(pg, {1, 2, 3}, {1, 9, 3}, 0.5f); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE CompareFloatLists([1,2,3]vs[1,9,3],t0.5)", g, 1.0f / 3.0f); }
  // BOUNDARY one list empty (cs:20-23 → 1): [1,2,3] vs [] → 1 (unwired ListB → no entry → empty).
  hostScalarInjectBug() = injectBug;
  { float g = bridgeCompareFloatLists(pg, {1, 2, 3}, {}, 0); hostScalarInjectBug() = false;
    leg(ok, injectBug, "BRIDGE CompareFloatLists([1,2,3]vs[],t0)", g, 1.0f); }

  // ===== RESIDENT-PATH legs (the PRODUCTION bridge: cookHostScalarNodes → evalResidentFloat). =====
  // SumRange resident TYPICAL.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeSumRangeResident({1, 2, 3, 4, 5}, 1, 4); hostScalarInjectBug() = false;
    leg(ok, injectBug, "RESIDENT SumRange([1..5],lo1,hi4)", g, 9.0f); }
  // IntListLength resident.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeIntListLengthTimesResident({5, 5, 5, 5}, 3); hostScalarInjectBug() = false;
    leg(ok, injectBug, "RESIDENT IntListLength([5,5,5,5])*3", g, 12.0f); }
  // PickIntFromList resident (T3 floor-Mod).
  hostScalarInjectBug() = injectBug;
  { float g = bridgePickIntFromListResident({10, 20, 30}, 4); hostScalarInjectBug() = false;
    leg(ok, injectBug, "RESIDENT PickIntFromList([10,20,30],idx4->Mod3=1)", g, 20.0f); }
  // CompareFloatLists resident IDENTICAL (proves resident two-FloatList gather).
  hostScalarInjectBug() = injectBug;
  { float g = bridgeCompareFloatListsResident({1, 2, 3}, {1, 2, 3}, 0); hostScalarInjectBug() = false;
    leg(ok, injectBug, "RESIDENT CompareFloatLists([1,2,3]==[1,2,3],t0)", g, 0.0f); }
  // CompareFloatLists resident ONE DIFFERING.
  hostScalarInjectBug() = injectBug;
  { float g = bridgeCompareFloatListsResident({1, 2, 3}, {1, 9, 3}, 0.5f); hostScalarInjectBug() = false;
    leg(ok, injectBug, "RESIDENT CompareFloatLists([1,2,3]vs[1,9,3],t0.5)", g, 1.0f / 3.0f); }

  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // corrupts the REAL cooked scalar → downstream evalFloat reads wrong → ok false → return 1 (teeth).
  std::printf("[selftest-listroutingwave1] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
