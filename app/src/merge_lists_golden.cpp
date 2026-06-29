// merge_lists_golden — --selftest-mergelists. CHAIN-THROUGH-evalFloat golden for the FloatList aggregator
// leaves: MergeFloatLists / MergeIntLists (Append / Htp / Average modes) + PickFloatList (index picker).
//
// Like floatlist_producers_golden (NOT a transport golden), each leg builds FloatsToList(...) sources →
// the AGGREGATOR → a DOWNSTREAM host-scalar consumer (FloatListLength for the result COUNT,
// PickFloatFromList for an indexed ELEMENT) → reads the consumer's scalar via evalFloat. So the
// aggregator's FloatList output is proven to FLOW node→node and be CONSUMED.
//
// Expected values hand-derived from the TiXL .cs Update():
//   MergeFloatLists.cs / MergeIntLists.cs:
//     Append (Enabled=false, default): wire-order CONCATENATION, skip empties → A++B.
//       [1,2]++[3,4,5] → len 5; element[3] = 4.
//     Htp (Enabled=true, mode=1): per-index MAX over present sources.
//       [1,9,2] vs [5,3] → [max(1,5),max(9,3),2] = [5,9,2]; len 3; element[0] = 5.
//     Average float (Enabled=true, mode=4): per-index float mean.
//       [2,4] vs [4,8] → [3,6]; element[1] = 6.0.
//     Average int (MergeIntLists, mode=4): per-index INTEGER division (int)(sum/count).
//       [3,4] vs [4,5] → [(7/2)=3, (9/2)=4] = [3,4]; element[0] = 3.0 (NOT 3.5).
//   PickFloatList.cs: Index.Mod(count) selects ONE whole source list.
//     sources [10,20] (idx0), [30,40,50] (idx1); Index 1 → picks list 1 → len 3; element[2] = 50.
//     Index 3 → 3.Mod(2)=1 → same list 1; Index -1 → (-1).Mod(2)=1 → list 1 (T3 floor-Mod).
//
// injectBug routes through floatListInjectBug() (drops the aggregator's last element in the REAL cook) so
// the DOWNSTREAM evalFloat reads a wrong count/element → RED on the actual cook path, NOT by flipping the
// expected value. --selftest-mergelists-bug must exit NON-zero.
#include <cmath>
#include <cstdio>
#include <string>
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

// Add a FloatsToList(vals) producer (id ftlId); wire its scalar Float MultiInput (port 0) with one Const
// per value (wire-declaration order). `nextConst`/`connId` advance. Returns the FloatsToList out pin.
int addFloatsToList(Graph& g, int ftlId, const std::vector<float>& vals, int& nextConst, int& connId) {
  Node ftl; ftl.id = ftlId; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  const int inPin = pinId(ftlId, /*Input*/ 0);
  for (float v : vals) {
    Node c; c.id = nextConst++; c.type = "Const"; c.params["value"] = v; g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), inPin});
  }
  return pinId(ftlId, /*out*/ 1);
}

// Cook `terminalId` (a host-scalar consumer) then read its output (port 0) via evalFloat.
float cookEval(PointGraph& pg, Graph& g, int terminalId) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/terminalId);
  return evalFloat(g, pinId(terminalId, /*out port 0*/ 0), ctx);
}

// Build: 2 FloatsToList(a,b) → aggregator(type, params) on its MultiInput → [Length | Pick(idx)].
// aggregatorPort0AfterInputs: for Merge* the InputLists MultiInput is port 0; for PickFloatList it is also
// port 0 (Input). So all three aggregators take the MultiInput on port 0. The aggregator's FloatList
// OUTPUT pin differs by spec: Merge*Lists out is the LAST port; PickFloatList out is the LAST port too.
// We pass the explicit output pin index per type.
//
// Returns evalFloat result. `useLength`=true → FloatListLength terminal (count); false → PickFloatFromList
// (element at pickIdx).
float runAggregate(PointGraph& pg, const char* aggType, int aggOutPortIdx,
                   const std::vector<std::pair<std::string, float>>& aggParams,
                   const std::vector<float>& a, const std::vector<float>& b, bool useLength, float pickIdx) {
  Graph g;
  int nextConst = 100, connId = 1000;
  const int aggId = 1, ftlA = 2, ftlB = 3, termId = 4;
  Node agg; agg.id = aggId; agg.type = aggType;
  for (const auto& p : aggParams) agg.params[p.first] = p.second;
  g.nodes.push_back(agg);
  const int aggMultiInPin = pinId(aggId, /*InputLists/Input = port 0*/ 0);

  const int outA = addFloatsToList(g, ftlA, a, nextConst, connId);
  const int outB = addFloatsToList(g, ftlB, b, nextConst, connId);
  g.connections.push_back({connId++, outA, aggMultiInPin});  // wire A first (wire-declaration order)
  g.connections.push_back({connId++, outB, aggMultiInPin});  // wire B second

  const int aggOutPin = pinId(aggId, aggOutPortIdx);
  if (useLength) {
    Node fll; fll.id = termId; fll.type = "FloatListLength"; g.nodes.push_back(fll);
    g.connections.push_back({connId++, aggOutPin, pinId(termId, /*Input*/ 1)});
  } else {
    Node pk; pk.id = termId; pk.type = "PickFloatFromList"; pk.params["Index"] = pickIdx;
    g.nodes.push_back(pk);
    g.connections.push_back({connId++, aggOutPin, pinId(termId, /*Input*/ 1)});
  }
  return cookEval(pg, g, termId);
}

}  // namespace

int runMergeListsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // Merge*Lists ports: [0]=InputLists(MultiInput), [1]=Enabled, [2]=MaxSize, [3]=MergeMode, [4]=out.
  const int kMergeOut = 4;
  // PickFloatList ports: [0]=Input(MultiInput), [1]=Index, [2]=out.
  const int kPickOut = 2;

  struct Leg {
    const char* tag;
    const char* type;
    int outPort;
    std::vector<std::pair<std::string, float>> params;
    std::vector<float> a, b;
    bool useLength;
    float pickIdx;
    float want;
  };

  const Leg legs[] = {
      // MergeFloatLists Append (default): [1,2]++[3,4,5] → len 5; element[3]=4.
      {"MergeFloatLists Append len", "MergeFloatLists", kMergeOut, {}, {1, 2}, {3, 4, 5}, true, 0, 5.0f},
      {"MergeFloatLists Append elem[3]", "MergeFloatLists", kMergeOut, {}, {1, 2}, {3, 4, 5}, false, 3, 4.0f},
      // Htp (Enabled=1, mode=1): [1,9,2] vs [5,3] → [5,9,2]; len 3; element[0]=5.
      {"MergeFloatLists Htp len", "MergeFloatLists", kMergeOut, {{"Enabled", 1}, {"MergeMode", 1}}, {1, 9, 2}, {5, 3}, true, 0, 3.0f},
      {"MergeFloatLists Htp elem[0]", "MergeFloatLists", kMergeOut, {{"Enabled", 1}, {"MergeMode", 1}}, {1, 9, 2}, {5, 3}, false, 0, 5.0f},
      // Average float (Enabled=1, mode=4): [2,4] vs [4,8] → [3,6]; element[1]=6.
      {"MergeFloatLists Avg elem[1]", "MergeFloatLists", kMergeOut, {{"Enabled", 1}, {"MergeMode", 4}}, {2, 4}, {4, 8}, false, 1, 6.0f},
      // MergeIntLists Average (integer division): [3,4] vs [4,5] → [(7/2)=3,(9/2)=4]; element[0]=3.
      {"MergeIntLists Avg elem[0] (int div)", "MergeIntLists", kMergeOut, {{"Enabled", 1}, {"MergeMode", 4}}, {3, 4}, {4, 5}, false, 0, 3.0f},
      {"MergeIntLists Avg elem[1] (int div)", "MergeIntLists", kMergeOut, {{"Enabled", 1}, {"MergeMode", 4}}, {3, 4}, {4, 5}, false, 1, 4.0f},
      // PickFloatList Index 1 → picks list B [30,40,50]; len 3; element[2]=50.
      {"PickFloatList idx1 len", "PickFloatList", kPickOut, {{"Index", 1}}, {10, 20}, {30, 40, 50}, true, 0, 3.0f},
      {"PickFloatList idx1 elem[2]", "PickFloatList", kPickOut, {{"Index", 1}}, {10, 20}, {30, 40, 50}, false, 2, 50.0f},
      // PickFloatList Index 3 → 3.Mod(2)=1 → list B; element[0]=30.
      {"PickFloatList idx3->Mod1 elem[0]", "PickFloatList", kPickOut, {{"Index", 3}}, {10, 20}, {30, 40, 50}, false, 0, 30.0f},
      // PickFloatList Index -1 → (-1).Mod(2)=1 → list B; len 3.
      {"PickFloatList idx-1->Mod1 len", "PickFloatList", kPickOut, {{"Index", -1}}, {10, 20}, {30, 40, 50}, true, 0, 3.0f},
  };

  for (const Leg& L : legs) {
    floatListInjectBug() = injectBug;
    float got = runAggregate(pg, L.type, L.outPort, L.params, L.a, L.b, L.useLength, L.pickIdx);
    floatListInjectBug() = false;
    bool pass = nearf(got, L.want);
    ok = ok && pass;
    std::printf("[selftest-mergelists] %s = %.2f want=%.2f -> %s\n", L.tag, got, L.want,
                pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mergelists] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
