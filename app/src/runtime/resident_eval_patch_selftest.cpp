// Headless RED->GREEN proof of the slice-3 first cut: incremental patch == rebuild, with cache
// preserved on untouched nodes. Two edits:
//   1. patchSetConstant (S1 value edit): Const#1(5)*Const#2(3)=15 cached; out-of-band poison
//      Const#2's constant to 99 (a cache probe — NOT a patch), then patch Const#1 -> 9. The pull
//      must be 9*3=27 (Const#2 returns its CACHED 3, not the poisoned 99) — proving only the edited
//      cone recomputes and the untouched sibling keeps its cache. A rebuilt graph (Const#1=9,
//      Const#2=3) also yields 27 -> patch == rebuild.
//   2. patchAddConnection (S11①): Mul.a = Constant(1), Mul.b <- Const(7) -> 1*7=7 cached. Wire
//      Time -> Mul.a, bump live, pull at t=5 -> 5*7=35. A rebuilt graph wired the same way -> 35.
//   3. B3 (批次9): patch* on a BYPASSED COMPOUND child (zero resident footprint) must REPORT
//      "not patchable" (return false) and leave the graph untouched; the same edit realized on
//      the lib + rebuild moves the value (only rebuild can represent it).
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

  // === 1b. patchSetConstant on a DERIVED node (refuter A4) — the edit must survive the pull-time
  // upstream-sum that owns a derived slot's sourceVersion. Const#1(5)->Mul.a (Connection),
  // Mul.b=Constant(3) -> 15. Edit Mul.b to 10: Mul must recompute to 5*10=50, even though Mul is
  // derived. (Pre-fix: ++sourceVersion is overwritten by sourceVersion=upstreamSum -> stale 15.)
  SymbolChild d1; d1.id = 1; d1.symbolId = "Const"; d1.overrides["value"] = 5.0f;
  SymbolChild dmb; dmb.id = 3; dmb.symbolId = "Multiply"; dmb.overrides["b"] = 3.0f;
  std::vector<SymbolConnection> derivedConns = {{1, "out", 3, "a"}, {3, "out", kSymbolBoundary, "out"}};
  ResidentEvalGraph gd = buildFlat("D", {d1, dmb}, derivedConns, cst, mul, tim);
  auto doutP = gd.outputs["out"];
  float d0 = pullResidentFloat(gd, doutP.first, doutP.second, ctx);  // 5*3 = 15
  patchSetConstant(gd, "3", "b", 10.0f);                            // edit a Constant input of a derived node
  float dpv = pullResidentFloat(gd, doutP.first, doutP.second, ctx);  // want 5*10 = 50
  bool derivedOk = (d0 == 15.0f && dpv == 50.0f);

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

  // === 3. B3 (批次9): an edit addressed at a BYPASSED COMPOUND child is NOT patchable — the 修C
  // rewire leaves zero resident footprint, so the patch functions cannot represent the edit in
  // place. They must SAY so (return false -> the caller rebuilds), not silently no-op while the
  // contract claims patch == rebuild. Shape: Const#1(5) -> Twice#2(BYPASSED) -> Mul#3(b=3) ->
  // out = 5*3 = 15 (the redirect feeds Mul.a straight from Const#1).
  Symbol twice; twice.id = "Twice"; twice.name = "Twice"; twice.atomic = false;
  twice.inputDefs = {{"in", "in", "Float", 0.0f}};
  twice.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild tw1; tw1.id = 1; tw1.symbolId = "Multiply";
  SymbolChild tw2; tw2.id = 2; tw2.symbolId = "Const"; tw2.overrides["value"] = 2.0f;
  twice.children = {tw1, tw2};
  twice.connections = {{kSymbolBoundary, "in", 1, "a"}, {2, "out", 1, "b"},
                       {1, "out", kSymbolBoundary, "out"}};
  SymbolLibrary bl;
  bl.symbols["Const"] = cst; bl.symbols["Multiply"] = mul; bl.symbols["Twice"] = twice;
  Symbol br; br.id = "B"; br.name = "B"; br.atomic = false;
  br.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild b1; b1.id = 1; b1.symbolId = "Const"; b1.overrides["value"] = 5.0f;
  SymbolChild b2; b2.id = 2; b2.symbolId = "Twice"; b2.isBypassed = true;
  SymbolChild b3; b3.id = 3; b3.symbolId = "Multiply"; b3.overrides["b"] = 3.0f;
  br.children = {b1, b2, b3};
  br.connections = {{1, "out", 2, "in"}, {2, "out", 3, "a"}, {3, "out", kSymbolBoundary, "out"}};
  bl.symbols["B"] = br; bl.rootId = "B";
  ResidentEvalGraph bg = buildEvalGraph(bl, "B");
  initResidentCache(bg);
  auto boutP = bg.outputs["out"];
  float B0 = pullResidentFloat(bg, boutP.first, boutP.second, ctx);          // 5*3 = 15
  bool sNot = patchSetConstant(bg, "2", "in", 7.0f);                         // zero footprint
  bool aNot = patchAddConnection(bg, "2", "in", "1", "out");                 // zero footprint
  bool rNot = patchRemoveConnection(bg, "2", "in");                          // zero footprint
  float B1 = pullResidentFloat(bg, boutP.first, boutP.second, ctx);          // untouched: 15
  bool realLanded = patchSetConstant(bg, "3", "b", 4.0f);                    // a real input lands
  float B2 = pullResidentFloat(bg, boutP.first, boutP.second, ctx);          // 5*4 = 20
  // Rebuild contrast: the edit the patch REFUSED (disconnect 1->2.in + set the compound input to
  // 7) realized on the LIB moves the value — proving the refusal was honest, not a no-op edit:
  // only a rebuild can realize it (production's path today).
  SymbolLibrary blR = bl;
  blR.symbols["B"].connections.erase(blR.symbols["B"].connections.begin());  // drop 1 -> 2.in
  blR.symbols["B"].children[1].overrides["in"] = 7.0f;   // the value patchSetConstant carried
  ResidentEvalGraph bgR = buildEvalGraph(blR, "B");
  initResidentCache(bgR);
  float BR = pullResidentFloat(bgR, bgR.outputs["out"].first, bgR.outputs["out"].second, ctx);
  bool bypassNotPatchable = (B0 == 15.0f && !sNot && !aNot && !rNot && B1 == 15.0f &&
                             realLanded && B2 == 20.0f && BR == 21.0f);  // 7 * 3 = 21

  bool pass = setOk && setRebuildMatch && derivedOk && addOk && addRebuildMatch &&
              bypassNotPatchable;
  printf("[selftest-residentpatch] setConst(f0=%.1f pv=%.1f want27 cachePreserved)=%d "
         "rebuild(rv=%.1f)=%d derived(d0=%.1f dpv=%.1f want50)=%d | addConn(A0=%.1f A1=%.1f want35)=%d "
         "rebuild(ar=%.1f)=%d | bypassCompoundNotPatchable(B0=%.1f reported=%d%d%d held=%.1f "
         "real=%d B2=%.1f rebuild=%.1f want21)=%d -> %s\n",
         f0, pv, setOk, rv, setRebuildMatch, d0, dpv, derivedOk, A0, A1, addOk, ar, addRebuildMatch,
         B0, !sNot, !aNot, !rNot, B1, realLanded, B2, BR, bypassNotPatchable,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
