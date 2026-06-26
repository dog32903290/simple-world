// floatlist_conversion_golden — --selftest-floatlistconversion. CHAIN-THROUGH-evalFloat golden for the
// FloatList<->IntList conversion ops (numbers/floats/conversion, list-state harvest):
//   FloatListToIntList / IntListToFloatList.
//
// Same BRIDGE-grade pattern as floatlist_producers_golden: build FloatsToList([...]) → CONVERSION-OP →
// a DOWNSTREAM host-scalar consumer (FloatListLength for COUNT, PickFloatFromList for an indexed ELEMENT)
// → read the consumer's scalar via evalFloat (the pure value recursion). So the conversion op's output is
// proven to FLOW node→node and be CONSUMED, not merely transported.
//
// Each leg's expected value is hand-derived from the TiXL .cs Update():
//   FloatListToIntList.cs — Select(f => (int)f) — C# (int) TRUNCATES TOWARD ZERO. The negative-case leg
//                           (-1.9 → -1, NOT -2) is the LOAD-BEARING assertion: it distinguishes truncate
//                           toward zero from floor (which would give -2).
//   IntListToFloatList.cs — Select(i => (float)i) — exact widening (identity on the dissolved currency).
//
// injectBug routes through floatListInjectBug() (drops the conversion op's last element in the REAL cook)
// so the DOWNSTREAM evalFloat reads a wrong count → RED on the actual cook path, NOT by flipping the
// expected value. The --bite harness runs --selftest-floatlistconversion-bug → must exit NON-zero.
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/point_graph.h"             // PointGraph::cook

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Append a chain of Const(vals[i]) nodes wired into `toPin` in vals order (= wire-declaration order).
void wireConstsInto(Graph& g, const std::vector<float>& vals, int toPin, int firstConstId, int& connId) {
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = firstConstId + (int)i; c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), toPin});
  }
}

// Cook `terminalId` (a host-scalar consumer) and read its output port 0 via evalFloat.
float cookEval(PointGraph& pg, Graph& g, int terminalId) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/terminalId);
  return evalFloat(g, pinId(terminalId, /*out port 0*/ 0), ctx);
}

// FloatsToList(vals) producer (id `ftlId`); wire its scalar Float MultiInput (port 0).
void addFloatsToList(Graph& g, int ftlId, const std::vector<float>& vals, int firstConstId, int& connId) {
  Node ftl; ftl.id = ftlId; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  wireConstsInto(g, vals, pinId(ftlId, /*Input*/ 0), firstConstId, connId);
}

// FloatListLength(id=lenId) reading `srcOutPin` (Length port 0, Input port 1).
void addLength(Graph& g, int lenId, int srcOutPin, int& connId) {
  Node fll; fll.id = lenId; fll.type = "FloatListLength"; g.nodes.push_back(fll);
  g.connections.push_back({connId++, srcOutPin, pinId(lenId, /*Input*/ 1)});
}

// PickFloatFromList(id=pkId, Index=index) reading `srcOutPin` (Selected port 0, Input port 1).
void addPick(Graph& g, int pkId, int srcOutPin, float index, int& connId) {
  Node pk; pk.id = pkId; pk.type = "PickFloatFromList"; pk.params["Index"] = index; g.nodes.push_back(pk);
  g.connections.push_back({connId++, srcOutPin, pinId(pkId, /*Input*/ 1)});
}

// FloatsToList(vals) → <convType> (out port 0, input port 1) → PickFloatFromList(pickIndex).
int chainConvPick(Graph& g, const char* convType, const std::vector<float>& vals, float pickIndex) {
  int connId = 100, nextConst = 50;
  int ftl = 2, conv = 3, pk = 4;
  addFloatsToList(g, ftl, vals, nextConst, connId);
  Node n; n.id = conv; n.type = convType; g.nodes.push_back(n);
  g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(conv, /*input*/ 1)});
  addPick(g, pk, pinId(conv, /*out*/ 0), pickIndex, connId);
  return pk;
}
// FloatsToList(vals) → <convType> → FloatListLength (count).
int chainConvLen(Graph& g, const char* convType, const std::vector<float>& vals) {
  int connId = 100, nextConst = 50;
  int ftl = 2, conv = 3, len = 4;
  addFloatsToList(g, ftl, vals, nextConst, connId);
  Node n; n.id = conv; n.type = convType; g.nodes.push_back(n);
  g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(conv, /*input*/ 1)});
  addLength(g, len, pinId(conv, /*out*/ 0), connId);
  return len;
}

}  // namespace

int runFloatListConversionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  bool ok = true;

  auto run = [&](const char* tag, Graph& g, int terminal, float want) {
    floatListInjectBug() = injectBug;
    float got = cookEval(pg, g, terminal);
    floatListInjectBug() = false;
    bool pass = nearf(got, want);
    ok = ok && pass;
    std::printf("[selftest-floatlistconversion] %s = %.4f want=%.4f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  // === FloatListToIntList — Select(f => (int)f), TRUNCATE TOWARD ZERO ===
  // L1 — positive truncation: FloatListToIntList([1.9, 2.1, -1.9])[0] = (int)1.9 = 1.
  { Graph g; int t = chainConvPick(g, "FloatListToIntList", {1.9f, 2.1f, -1.9f}, 0);
    run("FloatToInt([1.9,2.1,-1.9])[0]=1", g, t, 1.0f); }
  // L2 — positive truncation mid-list: index 1 → (int)2.1 = 2.
  { Graph g; int t = chainConvPick(g, "FloatListToIntList", {1.9f, 2.1f, -1.9f}, 1);
    run("FloatToInt([1.9,2.1,-1.9])[1]=2", g, t, 2.0f); }
  // L3 — *** LOAD-BEARING *** NEGATIVE truncation: (int)(-1.9) = -1 (truncate toward zero), NOT -2 (floor).
  //      This is the leg that distinguishes truncate-toward-zero from floor.
  { Graph g; int t = chainConvPick(g, "FloatListToIntList", {1.9f, 2.1f, -1.9f}, 2);
    run("FloatToInt([..,-1.9])[2]=-1(trunc!=floor)", g, t, -1.0f); }
  // L4 — count preserved through the conversion: 3 elements in → 3 out.
  { Graph g; int t = chainConvLen(g, "FloatListToIntList", {1.9f, 2.1f, -1.9f});
    run("FloatToInt([1.9,2.1,-1.9]).Count=3", g, t, 3.0f); }

  // === IntListToFloatList — Select(i => (float)i), exact widening (identity on dissolved currency) ===
  // L5 — exact widening element: IntListToFloatList([1,2,3])[2] = 3.0.
  { Graph g; int t = chainConvPick(g, "IntListToFloatList", {1, 2, 3}, 2);
    run("IntToFloat([1,2,3])[2]=3.0", g, t, 3.0f); }
  // L6 — head element exact: index 0 → 1.0.
  { Graph g; int t = chainConvPick(g, "IntListToFloatList", {1, 2, 3}, 0);
    run("IntToFloat([1,2,3])[0]=1.0", g, t, 1.0f); }
  // L7 — count preserved: 3 in → 3 out.
  { Graph g; int t = chainConvLen(g, "IntListToFloatList", {1, 2, 3});
    run("IntToFloat([1,2,3]).Count=3", g, t, 3.0f); }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug drops the conversion op's last element
  // → a downstream Length reads wrong → ok false → return 1 (teeth bite). No inversion.
  std::printf("[selftest-floatlistconversion] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
