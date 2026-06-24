// floatlist_producers_golden — --selftest-floatlistproducers. CHAIN-THROUGH-evalFloat golden for the
// WAVE-2 FloatList→FloatList producer ops (LIST_SEAM_BLUEPRINT §3 wave-2):
//   CombineFloatLists / IntsToList / SetFloatListValue / SetIntListValue / RemapFloatList.
//
// Unlike a TRANSPORT golden (cook + read back via debugCookedFloatList — which passes even if the bridge
// is broken), this is the BRIDGE-grade golden the blueprint §4 mandates: build FloatsToList([...]) →
// PRODUCER-OP → a DOWNSTREAM host-scalar consumer (FloatListLength for COUNT, PickFloatFromList for an
// indexed ELEMENT) → read the consumer's scalar via evalFloat (the pure value recursion). So the
// producer's FloatList output is proven to FLOW node→node and be CONSUMED, not merely transported.
//
// Each leg's expected value is hand-derived from the TiXL .cs Update() (NOT self-consistent):
//   CombineFloatLists.cs   — concatenate wired lists in wire order (skip empties).
//   IntsToList.cs          — collect wired scalars (int-dissolved) in wire order.
//   SetFloatListValue.cs   — copy input; if TriggerSet: index>=0 → list[Index.Mod(Count)] op= Value;
//                            index==-2 → ALL; Set/Add/Multiply. Not-triggered/empty → passthrough.
//   SetIntListValue.cs     — int twin of the above (integer Add/Multiply).
//   RemapFloatList.cs      — per-element (value-inMin)/inRange → *(outMax-outMin)+outMin, with the
//                            ApplyGainAndBias curve on 0<normalized<1 + Clamped/Modulo modes.
//
// injectBug routes through floatListInjectBug() (drops the producer's last element in the REAL cook) so
// the DOWNSTREAM evalFloat reads a wrong count/element → RED on the actual cook path, NOT by flipping the
// expected value. The --bite harness runs --selftest-floatlistproducers-bug → must exit NON-zero.
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

// ---- shared graph-builder helpers ----------------------------------------------------------------

// Append a chain of Const(vals[i]) nodes (ids starting at `firstConstId`), each wired into `toPin` in
// vals order (= wire-declaration order). Returns the next free node id.
int wireConstsInto(Graph& g, const std::vector<float>& vals, int toPin, int firstConstId, int& connId) {
  for (size_t i = 0; i < vals.size(); ++i) {
    Node c; c.id = firstConstId + (int)i; c.type = "Const"; c.params["value"] = vals[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), toPin});
  }
  return firstConstId + (int)vals.size();
}

// Cook `terminalId` (a host-scalar consumer) and read its Selected/Length output (port 0) via evalFloat.
float cookEval(PointGraph& pg, Graph& g, int terminalId) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/terminalId);
  return evalFloat(g, pinId(terminalId, /*out port 0*/ 0), ctx);
}

// FloatsToList(vals) producer (id `ftlId`); wire its scalar Float MultiInput (port 0). Returns next const id.
int addFloatsToList(Graph& g, int ftlId, const std::vector<float>& vals, int firstConstId, int& connId) {
  Node ftl; ftl.id = ftlId; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  return wireConstsInto(g, vals, pinId(ftlId, /*Input*/ 0), firstConstId, connId);
}

// Append a FloatListLength(id=lenId) reading the FloatList output of `srcOutPin`. Length is port 0,
// Input is port 1 (mirror host_scalar_ops_floatlistlength).
void addLength(Graph& g, int lenId, int srcOutPin, int& connId) {
  Node fll; fll.id = lenId; fll.type = "FloatListLength"; g.nodes.push_back(fll);
  g.connections.push_back({connId++, srcOutPin, pinId(lenId, /*Input*/ 1)});
}

// Append a PickFloatFromList(id=pkId, Index=index) reading `srcOutPin`. Selected port 0, Input port 1,
// Index param (mirror host_scalar_ops_pickfloatfromlist).
void addPick(Graph& g, int pkId, int srcOutPin, float index, int& connId) {
  Node pk; pk.id = pkId; pk.type = "PickFloatFromList"; pk.params["Index"] = index; g.nodes.push_back(pk);
  g.connections.push_back({connId++, srcOutPin, pinId(pkId, /*Input*/ 1)});
}

// ---- per-op chain builders (producer fed by FloatsToList; consumed by Length or Pick) -------------

// CombineFloatLists(listA, listB) → FloatListLength. Two FloatsToList producers wired into the combiner's
// FloatList MultiInput. CombineFloatLists ports (declaration order): [0]=InputLists (FloatList MultiInput),
// [1]=out. Returns the chain's terminal id (the Length node).
int chainCombineLen(Graph& g, const std::vector<float>& a, const std::vector<float>& b) {
  int connId = 100, nextConst = 50;
  int ftlA = 2, ftlB = 3, comb = 4, len = 5;
  nextConst = addFloatsToList(g, ftlA, a, nextConst, connId);
  nextConst = addFloatsToList(g, ftlB, b, nextConst, connId);
  Node cb; cb.id = comb; cb.type = "CombineFloatLists"; g.nodes.push_back(cb);
  const int combIn = pinId(comb, /*InputLists*/ 0);
  g.connections.push_back({connId++, pinId(ftlA, /*out*/ 1), combIn});  // wire A first
  g.connections.push_back({connId++, pinId(ftlB, /*out*/ 1), combIn});  // then B (wire-decl order)
  addLength(g, len, pinId(comb, /*out*/ 1), connId);
  return len;
}
// CombineFloatLists(a,b) → PickFloatFromList(index) (read a specific concatenated element).
int chainCombinePick(Graph& g, const std::vector<float>& a, const std::vector<float>& b, float index) {
  int connId = 100, nextConst = 50;
  int ftlA = 2, ftlB = 3, comb = 4, pk = 5;
  nextConst = addFloatsToList(g, ftlA, a, nextConst, connId);
  nextConst = addFloatsToList(g, ftlB, b, nextConst, connId);
  Node cb; cb.id = comb; cb.type = "CombineFloatLists"; g.nodes.push_back(cb);
  const int combIn = pinId(comb, /*InputLists*/ 0);
  g.connections.push_back({connId++, pinId(ftlA, 1), combIn});
  g.connections.push_back({connId++, pinId(ftlB, 1), combIn});
  addPick(g, pk, pinId(comb, /*out*/ 1), index, connId);
  return pk;
}

// IntsToList(vals) → PickFloatFromList(index). IntsToList: out port 1, Input (scalar MultiInput) port 0.
int chainIntsToListPick(Graph& g, const std::vector<float>& vals, float index) {
  int connId = 100, nextConst = 50;
  int itl = 2, pk = 3;
  Node n; n.id = itl; n.type = "IntsToList"; g.nodes.push_back(n);
  wireConstsInto(g, vals, pinId(itl, /*Input*/ 0), nextConst, connId);
  addPick(g, pk, pinId(itl, /*out*/ 1), index, connId);
  return pk;
}
int chainIntsToListLen(Graph& g, const std::vector<float>& vals) {
  int connId = 100, nextConst = 50;
  int itl = 2, len = 3;
  Node n; n.id = itl; n.type = "IntsToList"; g.nodes.push_back(n);
  wireConstsInto(g, vals, pinId(itl, 0), nextConst, connId);
  addLength(g, len, pinId(itl, 1), connId);
  return len;
}

// SetFloatListValue: out port 0, FloatList port 1, Mode/TriggerSet/Index/Value pinless params.
// FloatsToList(vals) → SetFloatListValue(params) → PickFloatFromList(pickIndex).
int chainSetFloatPick(Graph& g, const std::vector<float>& vals, float mode, bool trigger, float index,
                      float value, float pickIndex) {
  int connId = 100, nextConst = 50;
  int ftl = 2, setN = 3, pk = 4;
  nextConst = addFloatsToList(g, ftl, vals, nextConst, connId);
  Node sv; sv.id = setN; sv.type = "SetFloatListValue";
  sv.params["Mode"] = mode; sv.params["TriggerSet"] = trigger ? 1.0f : 0.0f;
  sv.params["Index"] = index; sv.params["Value"] = value;
  g.nodes.push_back(sv);
  g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(setN, /*FloatList*/ 1)});
  addPick(g, pk, pinId(setN, /*out*/ 0), pickIndex, connId);
  return pk;
}
// SetIntListValue: same port layout (out 0, IntList 1, params).
int chainSetIntPick(Graph& g, const std::vector<float>& vals, float mode, bool trigger, float index,
                    float value, float pickIndex) {
  int connId = 100, nextConst = 50;
  int ftl = 2, setN = 3, pk = 4;
  nextConst = addFloatsToList(g, ftl, vals, nextConst, connId);
  Node sv; sv.id = setN; sv.type = "SetIntListValue";
  sv.params["Mode"] = mode; sv.params["TriggerSet"] = trigger ? 1.0f : 0.0f;
  sv.params["Index"] = index; sv.params["Value"] = value;
  g.nodes.push_back(sv);
  g.connections.push_back({connId++, pinId(ftl, 1), pinId(setN, 1)});
  addPick(g, pk, pinId(setN, 0), pickIndex, connId);
  return pk;
}

// RemapFloatList: out 0, FloatList 1, Mode + 4 ranges + BiasAndGain.x/.y params.
// FloatsToList(vals) → RemapFloatList(ranges, mode) → PickFloatFromList(pickIndex).
int chainRemapPick(Graph& g, const std::vector<float>& vals, float inMin, float inMax, float outMin,
                   float outMax, float gain, float bias, float mode, float pickIndex) {
  int connId = 100, nextConst = 50;
  int ftl = 2, rm = 3, pk = 4;
  nextConst = addFloatsToList(g, ftl, vals, nextConst, connId);
  Node n; n.id = rm; n.type = "RemapFloatList";
  n.params["RangeInMin"] = inMin; n.params["RangeInMax"] = inMax;
  n.params["RangeOutMin"] = outMin; n.params["RangeOutMax"] = outMax;
  n.params["BiasAndGain.x"] = gain; n.params["BiasAndGain.y"] = bias;
  n.params["Mode"] = mode;
  g.nodes.push_back(n);
  g.connections.push_back({connId++, pinId(ftl, /*out*/ 1), pinId(rm, /*FloatList*/ 1)});
  addPick(g, pk, pinId(rm, /*out*/ 0), pickIndex, connId);
  return pk;
}

}  // namespace

int runFloatListProducersSelfTest(bool injectBug) {
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
    std::printf("[selftest-floatlistproducers] %s = %.4f want=%.4f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  // === CombineFloatLists ===
  // L1 — concatenation COUNT: [1,2] ++ [3,4,5] → 5 elements (CombineFloatLists.cs AddRange both).
  { Graph g; int t = chainCombineLen(g, {1, 2}, {3, 4, 5}); run("Combine([1,2],[3,4,5]).Count", g, t, 5.0f); }
  // L2 — concatenation ORDER (element from the SECOND list): index 2 of [1,2,3,4,5] = 3.0 (list B's head).
  { Graph g; int t = chainCombinePick(g, {1, 2}, {3, 4, 5}, 2); run("Combine([1,2],[3,4,5])[2]", g, t, 3.0f); }
  // L3 — empty-list skip: [] ++ [9] → 1 element (the empty wired list is skipped, .cs Count>0 guard).
  { Graph g; int t = chainCombineLen(g, {}, {9}); run("Combine([],[9]).Count", g, t, 1.0f); }

  // === IntsToList ===
  // L4 — int fold ELEMENT: IntsToList(5,7,9)[1] = 7 (round-trip integer-valued).
  { Graph g; int t = chainIntsToListPick(g, {5, 7, 9}, 1); run("IntsToList(5,7,9)[1]", g, t, 7.0f); }
  // L5 — int fold rounds non-integer inputs: IntsToList(2.6) → round → 3 (Count 1, element 3).
  { Graph g; int t = chainIntsToListPick(g, {2.6f}, 0); run("IntsToList(2.6)[0]=round", g, t, 3.0f); }
  // L6 — int fold COUNT: IntsToList(1,2,3,4) → 4.
  { Graph g; int t = chainIntsToListLen(g, {1, 2, 3, 4}); run("IntsToList(1,2,3,4).Count", g, t, 4.0f); }

  // === SetFloatListValue ===
  // L7 — Set single index (Mode 0): [10,20,30] Index 1 Value 99 → list[1]=99; pick[1]=99.
  { Graph g; int t = chainSetFloatPick(g, {10, 20, 30}, /*Set*/ 0, /*trig*/ true, /*idx*/ 1, /*v*/ 99, /*pick*/ 1);
    run("SetFloat([10,20,30],Set,i1,99)[1]", g, t, 99.0f); }
  // L8 — Add with index Mod(Count): [10,20,30] Index 4 → 4.Mod(3)=1; Mode Add 5 → list[1]=25; pick[1]=25.
  { Graph g; int t = chainSetFloatPick(g, {10, 20, 30}, /*Add*/ 1, true, /*idx*/ 4, /*v*/ 5, /*pick*/ 1);
    run("SetFloat([10,20,30],Add,i4Mod3=1,5)[1]", g, t, 25.0f); }
  // L9 — index -2 applies to ALL (Multiply 2): [10,20,30] → [20,40,60]; pick[2]=60.
  { Graph g; int t = chainSetFloatPick(g, {10, 20, 30}, /*Multiply*/ 2, true, /*idx*/ -2, /*v*/ 2, /*pick*/ 2);
    run("SetFloat([10,20,30],Mul,all,2)[2]", g, t, 60.0f); }
  // L10 — TriggerSet false → passthrough (no mutation): [10,20,30] pick[1] stays 20.
  { Graph g; int t = chainSetFloatPick(g, {10, 20, 30}, 0, /*trig*/ false, 1, 99, /*pick*/ 1);
    run("SetFloat(trigger=false)[1]=passthru", g, t, 20.0f); }

  // === SetIntListValue ===
  // L11 — int Add with Mod: [10,20,30] Index 4→Mod3=1 Add 5 → 25 (integer arithmetic); pick[1]=25.
  { Graph g; int t = chainSetIntPick(g, {10, 20, 30}, /*Add*/ 1, true, /*idx*/ 4, /*v*/ 5, /*pick*/ 1);
    run("SetInt([10,20,30],Add,i4Mod3=1,5)[1]", g, t, 25.0f); }
  // L12 — int Set ALL (index -2): [3,3,3] Set 7 → [7,7,7]; pick[0]=7.
  { Graph g; int t = chainSetIntPick(g, {3, 3, 3}, /*Set*/ 0, true, /*idx*/ -2, /*v*/ 7, /*pick*/ 0);
    run("SetInt([3,3,3],Set,all,7)[0]", g, t, 7.0f); }

  // === RemapFloatList ===
  // L13 — linear remap (Normal, no bias/gain curve since gain=bias=0 → ApplyGainAndBias only fires for
  // 0<normalized<1; with gain=0,bias=0 GetBias/GetSchlickBias collapse): value 5 in [0,10]→[0,100].
  // normalized=0.5 ∈(0,1) → ApplyGainAndBias(0.5, gain=0, bias=0). g<0.5: GetBias(0,0.5) then
  // GetSchlickBias(0,·). GetBias(0,x)=x/((1/0-2)(1-x)+1)=x/(inf)=0 → 0; GetSchlickBias(0,0): x<0.5 →
  // 0.5*GetBias(0,0)=0. So normalized→0 → v=0*100+0=0. (bias/gain at 0 crush to the low end — faithful.)
  { Graph g; int t = chainRemapPick(g, {5}, /*inMin*/ 0, /*inMax*/ 10, /*outMin*/ 0, /*outMax*/ 100,
                                    /*gain*/ 0, /*bias*/ 0, /*Normal*/ 0, /*pick*/ 0);
    run("Remap(5,[0,10]->[0,100],g0b0)[0]", g, t, 0.0f); }
  // L14 — neutral gain/bias (0.5,0.5) is the IDENTITY curve: ApplyGainAndBias(x,0.5,0.5)=x. value 2 in
  // [0,4]→[0,10]: normalized 0.5 → identity → v=0.5*10=5.0. (GetSchlickBias(0.5,·)+GetBias(0.5,·)=identity.)
  { Graph g; int t = chainRemapPick(g, {2}, 0, 4, 0, 10, /*gain*/ 0.5f, /*bias*/ 0.5f, /*Normal*/ 0, /*pick*/ 0);
    run("Remap(2,[0,4]->[0,10],g.5b.5)[0]", g, t, 5.0f); }
  // L15 — normalized OUTSIDE (0,1) skips the curve: value 0 in [0,4] → normalized 0 (not in open (0,1))
  // → v = 0*(10-0)+0 = 0.0 (the endpoint passes through untouched).
  { Graph g; int t = chainRemapPick(g, {0}, 0, 4, 0, 10, 0.5f, 0.5f, 0, /*pick*/ 0);
    run("Remap(0,[0,4]->[0,10])[0]=endpoint", g, t, 0.0f); }
  // L16 — Clamped mode caps to [min(out),max(out)]: value 8 in [0,4]→[0,10] normalized 2 (skips curve,
  // not in (0,1)) → v=2*10=20 → Clamped to [0,10] → 10.0.
  { Graph g; int t = chainRemapPick(g, {8}, 0, 4, 0, 10, 0.5f, 0.5f, /*Clamped*/ 1, /*pick*/ 0);
    run("Remap(8,[0,4]->[0,10],Clamped)[0]", g, t, 10.0f); }
  // L17 — degenerate input range (inMin==inMax) → every element = outMin (RemapFloatList.cs:154-164).
  { Graph g; int t = chainRemapPick(g, {1, 2, 3}, /*inMin*/ 5, /*inMax*/ 5, /*outMin*/ 7, /*outMax*/ 9,
                                    0.5f, 0.5f, 0, /*pick*/ 2);
    run("Remap(deg-range)->outMin[2]", g, t, 7.0f); }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug drops the producer's last element →
  // a downstream Length/Pick reads wrong → ok false → return 1 (teeth bite). No inversion.
  std::printf("[selftest-floatlistproducers] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
