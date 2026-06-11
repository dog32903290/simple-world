// Headless RED->GREEN proof of the slice-3 first cut: incremental patch == rebuild, with cache
// preserved on untouched nodes. Two edits:
//   1. patchSetConstant (S1 value edit): Const#1(5)*Const#2(3)=15 cached; out-of-band poison
//      Const#2's constant to 99 (a cache probe — NOT a patch), then patch Const#1 -> 9. The pull
//      must be 9*3=27 (Const#2 returns its CACHED 3, not the poisoned 99) — proving only the edited
//      cone recomputes and the untouched sibling keeps its cache. A rebuilt graph (Const#1=9,
//      Const#2=3) also yields 27 -> patch == rebuild.
//   2. patchAddConnection (S11①): Mul.a = Constant(1), Mul.b <- Const(7) -> 1*7=7 cached. Wire
//      Time -> Mul.a, bump live, pull at t=5 -> 5*7=35. A rebuilt graph wired the same way -> 35.
// injectBug edits the constant directly WITHOUT the patch's edit-time invalidation -> the patched
// value stays frozen at the cached 15 (卡舊) -> the assertion FAILS (teeth).
#include "runtime/resident_eval_graph.h"

#include <cstdio>

namespace sw {
namespace {

Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Build a flat single-symbol library with given children/connections, return its resident graph
// (cache initialized). The root symbol id is `rootId`, one Float output "out".
ResidentEvalGraph buildFlat(const char* rootId, std::vector<SymbolChild> children,
                            std::vector<SymbolConnection> conns, const Symbol& cst,
                            const Symbol& mul, const Symbol& tim) {
  SymbolLibrary lib;
  lib.symbols["Const"] = cst; lib.symbols["Multiply"] = mul; lib.symbols["Time"] = tim;
  Symbol root; root.id = rootId; root.name = rootId; root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  root.children = std::move(children);
  root.connections = std::move(conns);
  lib.symbols[rootId] = root; lib.rootId = rootId;
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);
  return g;
}

}  // namespace

int runResidentPatchSelfTest(bool injectBug) {
  Symbol cst = atomic("Const", {{"value", "value", "Float", 0.0f}}, {{"out", "out", "Float", 0.0f}});
  Symbol mul = atomic("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                      {{"out", "out", "Float", 0.0f}});
  Symbol tim = atomic("Time", {}, {{"out", "out", "Float", 0.0f}});
  ResidentEvalCtx ctx;  // time 0

  // === 1. patchSetConstant — value edit, untouched sibling cache preserved ===
  SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 5.0f;
  SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = 3.0f;
  SymbolChild cm; cm.id = 3; cm.symbolId = "Multiply";
  std::vector<SymbolConnection> mulConns = {
      {1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  ResidentEvalGraph g = buildFlat("S", {c1, c2, cm}, mulConns, cst, mul, tim);
  auto outP = g.outputs["out"];
  float f0 = pullResidentFloat(g, outP.first, outP.second, ctx);  // 5*3 = 15, cache built

  // Poison Const#2's constant to 99 out-of-band (cache probe: if patch over-invalidates, the pull
  // would see 99). NOT a patch -> no bump.
  for (ResidentInput& in : g.nodes[g.byPath["2"]].inputs)
    if (in.slotId == "value") in.constant = 99.0f;

  if (injectBug) {
    // Buggy edit path: change the constant directly, skipping the patch's edit-time invalidation.
    for (ResidentInput& in : g.nodes[g.byPath["1"]].inputs)
      if (in.slotId == "value") in.constant = 9.0f;  // no bump -> stays stale
  } else {
    patchSetConstant(g, "1", "value", 9.0f);  // correct: edits + edit-time push
  }
  float pv = pullResidentFloat(g, outP.first, outP.second, ctx);  // want 9 * cached 3 = 27
  bool setOk = (f0 == 15.0f && pv == 27.0f);

  // patch == rebuild: a fresh graph with Const#1=9, Const#2=3 (the real post-edit state).
  SymbolChild r1 = c1; r1.overrides["value"] = 9.0f;
  ResidentEvalGraph gR = buildFlat("SR", {r1, c2, cm}, mulConns, cst, mul, tim);
  float rv = pullResidentFloat(gR, gR.outputs["out"].first, gR.outputs["out"].second, ctx);
  bool setRebuildMatch = (rv == 27.0f && pv == rv);

  // === 2. patchAddConnection — add a Connection, force first-pull recompute ===
  SymbolChild lt; lt.id = 1; lt.symbolId = "Time";
  SymbolChild lc; lc.id = 2; lc.symbolId = "Const"; lc.overrides["value"] = 7.0f;
  SymbolChild lm; lm.id = 3; lm.symbolId = "Multiply"; lm.overrides["a"] = 1.0f;  // a starts Constant(1)
  // Initially Mul.a is NOT wired (only b <- Const(7)). 1*7 = 7.
  std::vector<SymbolConnection> startConns = {{2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  ResidentEvalGraph lg = buildFlat("L", {lt, lc, lm}, startConns, cst, mul, tim);
  auto loutP = lg.outputs["out"];
  float A0 = pullResidentFloat(lg, loutP.first, loutP.second, ctx);  // 1*7 = 7, cached

  patchAddConnection(lg, "3", "a", "1", "out");  // wire Time.out -> Mul.a
  bumpLiveSources(lg);
  ResidentEvalCtx t5; t5.localFxTime = 5.0f;
  float A1 = pullResidentFloat(lg, loutP.first, loutP.second, t5);  // want 5*7 = 35
  bool addOk = (A0 == 7.0f && A1 == 35.0f);

  // patch == rebuild: a fresh graph wired Time->Mul.a from the start.
  std::vector<SymbolConnection> wiredConns = {
      {1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  ResidentEvalGraph lgR = buildFlat("LR", {lt, lc, lm}, wiredConns, cst, mul, tim);
  bumpLiveSources(lgR);
  float ar = pullResidentFloat(lgR, lgR.outputs["out"].first, lgR.outputs["out"].second, t5);
  bool addRebuildMatch = (ar == 35.0f && A1 == ar);

  bool pass = setOk && setRebuildMatch && addOk && addRebuildMatch;
  printf("[selftest-residentpatch] setConst(f0=%.1f pv=%.1f want27 cachePreserved)=%d "
         "rebuild(rv=%.1f)=%d | addConn(A0=%.1f A1=%.1f want35)=%d rebuild(ar=%.1f)=%d -> %s\n",
         f0, pv, setOk, rv, setRebuildMatch, A0, A1, addOk, ar, addRebuildMatch,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
