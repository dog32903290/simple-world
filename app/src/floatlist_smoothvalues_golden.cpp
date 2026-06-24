// floatlist_smoothvalues_golden — --selftest-smoothvalues. CHAIN-THROUGH-evalFloat golden for the
// SmoothValues FloatList→FloatList op (LIST_SEAM_BLUEPRINT §3 wave-3, but STATELESS — see the leaf header:
// SmoothValues.cs holds no cross-frame field, it is a pure forward-window box average rebuilt each cook).
//
// BRIDGE-grade golden (blueprint §4): build FloatsToList([...]) → SmoothValues(WindowSize) → a DOWNSTREAM
// host-scalar consumer (FloatListLength for COUNT, PickFloatFromList for an indexed ELEMENT) → read the
// consumer's scalar via evalFloat. So SmoothValues' FloatList output is proven to FLOW node→node and be
// CONSUMED, not merely transported (a transport-only readback passes even if the bridge is broken).
//
// Expected values hand-derived from SmoothValues.cs Update() (NOT self-consistent). The .cs samples index
// `index` ONCE before the window loop, then again as windowIndex==0 — so the element AT index is weighted
// twice and the window spans [index, index+WindowSize-1] forward (the fork-double-count-index contract):
//     mean(i) = ( in[i] + Σ_{w=0..W-1, i+w<n} in[i+w] ) / ( 1 + (#in-bounds in [i, i+W-1]) )
// WindowSize.Clamp(1,10): default 0 → 1; a too-large window clamps to 10.
//
// injectBug routes through floatListInjectBug() (drops SmoothValues' last output element in the REAL cook)
// so the DOWNSTREAM evalFloat reads a wrong count/element → RED on the actual cook path, NOT by flipping
// the expected value. --bite runs --selftest-smoothvalues-bug → must exit NON-zero.
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

// Append Const(vals[i]) nodes wired into `toPin` in vals order (= wire-declaration order).
int wireConstsInto(Graph& g, const std::vector<float>& vals, int toPin, int firstConstId, int& connId) {
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = firstConstId + (int)i; c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), toPin});
  }
  return firstConstId + (int)vals.size();
}

float cookEval(PointGraph& pg, Graph& g, int terminalId) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/terminalId);
  return evalFloat(g, pinId(terminalId, /*out port 0*/ 0), ctx);
}

// FloatsToList(vals) producer (id `ftlId`); wire its scalar Float MultiInput (port 0).
int addFloatsToList(Graph& g, int ftlId, const std::vector<float>& vals, int firstConstId, int& connId) {
  Node ftl; ftl.id = ftlId; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  return wireConstsInto(g, vals, pinId(ftlId, /*Input*/ 0), firstConstId, connId);
}

// FloatListLength(id=lenId) reading `srcOutPin` (Length port 0, Input port 1).
void addLength(Graph& g, int lenId, int srcOutPin, int& connId) {
  Node fll; fll.id = lenId; fll.type = "FloatListLength"; g.nodes.push_back(fll);
  g.connections.push_back({connId++, srcOutPin, pinId(lenId, /*Input*/ 1)});
}

// PickFloatFromList(id=pkId, Index=index) reading `srcOutPin` (Selected port 0, Input port 1, Index param).
void addPick(Graph& g, int pkId, int srcOutPin, float index, int& connId) {
  Node pk; pk.id = pkId; pk.type = "PickFloatFromList"; pk.params["Index"] = index; g.nodes.push_back(pk);
  g.connections.push_back({connId++, srcOutPin, pinId(pkId, /*Input*/ 1)});
}

// FloatsToList(vals) → SmoothValues(WindowSize) → PickFloatFromList(pickIndex).
// SmoothValues ports: out 0, Input (FloatList) 1, WindowSize (pinless param).
int chainSmoothPick(Graph& g, const std::vector<float>& vals, float windowSize, float pickIndex) {
  int connId = 100, nextConst = 50;
  int ftl = 2, sv = 3, pk = 4;
  nextConst = addFloatsToList(g, ftl, vals, nextConst, connId);
  Node n; n.id = sv; n.type = "SmoothValues"; n.params["WindowSize"] = windowSize; g.nodes.push_back(n);
  g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(sv, /*Input*/ 1)});
  addPick(g, pk, pinId(sv, /*out*/ 0), pickIndex, connId);
  return pk;
}
// FloatsToList(vals) → SmoothValues(WindowSize) → FloatListLength.
int chainSmoothLen(Graph& g, const std::vector<float>& vals, float windowSize) {
  int connId = 100, nextConst = 50;
  int ftl = 2, sv = 3, len = 4;
  nextConst = addFloatsToList(g, ftl, vals, nextConst, connId);
  Node n; n.id = sv; n.type = "SmoothValues"; n.params["WindowSize"] = windowSize; g.nodes.push_back(n);
  g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(sv, /*Input*/ 1)});
  addLength(g, len, pinId(sv, /*out*/ 1), connId);
  return len;
}

}  // namespace

int runSmoothValuesSelfTest(bool injectBug) {
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
    std::printf("[selftest-smoothvalues] %s = %.4f want=%.4f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  // Input list reused: [2,4,6,8] (n=4).
  // L1 — WindowSize 1 is the IDENTITY (window {i}, but index double-counted → sum=2*in[i], count=2 →
  //      mean=in[i]). pick[2] = 6.0.
  { Graph g; int t = chainSmoothPick(g, {2, 4, 6, 8}, /*W*/ 1, /*pick*/ 2); run("Smooth([2,4,6,8],W1)[2]=id", g, t, 6.0f); }
  // L2 — WindowSize 2, i=0: sum = in[0] + in[0] + in[1] = 2+2+4 = 8; count = 1 + 2 = 3 → 8/3 = 2.66667.
  { Graph g; int t = chainSmoothPick(g, {2, 4, 6, 8}, /*W*/ 2, /*pick*/ 0); run("Smooth(W2)[0]=8/3", g, t, 8.0f / 3.0f); }
  // L3 — WindowSize 2, i=3 (last, forward window clips at end): in[4] OOB → only in[3]. sum=8+8=16,
  //      count=1+1=2 → 8.0 (edge: forward window shrinks at the tail).
  { Graph g; int t = chainSmoothPick(g, {2, 4, 6, 8}, /*W*/ 2, /*pick*/ 3); run("Smooth(W2)[3]=edge8", g, t, 8.0f); }
  // L4 — WindowSize 3, i=1: window [1,2,3] all in-bounds. sum = in[1] + in[1]+in[2]+in[3] = 4+4+6+8 = 22;
  //      count = 1 + 3 = 4 → 22/4 = 5.5.
  { Graph g; int t = chainSmoothPick(g, {2, 4, 6, 8}, /*W*/ 3, /*pick*/ 1); run("Smooth(W3)[1]=5.5", g, t, 5.5f); }
  // L5 — WindowSize 20 CLAMPS to 10: i=0 window [0..9], only [0..3] in-bounds. sum = in[0] + (2+4+6+8)
  //      = 2 + 20 = 22; count = 1 + 4 = 5 → 22/5 = 4.4 (proves the Clamp(1,10) upper bound).
  { Graph g; int t = chainSmoothPick(g, {2, 4, 6, 8}, /*W*/ 20, /*pick*/ 0); run("Smooth(W20->10)[0]=4.4", g, t, 4.4f); }
  // L6 — WindowSize 0 (default) CLAMPS to 1 → identity. pick[1] = 4.0 (proves the Clamp(1,10) lower bound).
  { Graph g; int t = chainSmoothPick(g, {2, 4, 6, 8}, /*W*/ 0, /*pick*/ 1); run("Smooth(W0->1)[1]=id4", g, t, 4.0f); }
  // L7 — COUNT preserved: SmoothValues outputs one element PER input element regardless of window → 4.
  { Graph g; int t = chainSmoothLen(g, {2, 4, 6, 8}, /*W*/ 3); run("Smooth(W3).Count=4", g, t, 4.0f); }
  // L8 — single-element list boundary: [5], W3, i=0 window [0,1,2] only in[0] in-bounds. sum=5+5=10,
  //      count=1+1=2 → 5.0 (single-cell input → identity).
  { Graph g; int t = chainSmoothPick(g, {5}, /*W*/ 3, /*pick*/ 0); run("Smooth([5],W3)[0]=5", g, t, 5.0f); }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug drops SmoothValues' last output element
  // → a downstream Length(L7) reads 3 not 4 (RED) and Pick(L3,L8 at the dropped tail) reads wrong → ok
  // false → return 1 (teeth bite). No inversion.
  std::printf("[selftest-smoothvalues] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
