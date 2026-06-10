// Headless RED->GREEN proof of the batch-1b version-chasing cache (resident_eval_cache.cpp).
// Three things the cache must do, all on the float value graph (決策 6: 值圖 = eager 後序一趟):
//   1. STATIC short-circuit: Const(5)->Mul.a, Const(3)->Mul.b cooks 15 once; mutating Const#1's
//      constant to 9 WITHOUT an edit-time bump leaves the next pull at 15 — the cache hides the
//      change (proves recompute is actually skipped, TiXL DirtyFlag value-chasing).
//   2. EDIT-TIME push: bumping Const#1.out sourceVersion makes the change visible, propagating
//      through Multiply's summed upstream version -> recompute -> 9*3 = 27.
//   3. LIVE source: Time declares an always-dirty output; bumpLiveSources every frame -> Multiply
//      recomputes each frame (2*7=14 then 5*7=35). injectBug SKIPS the bump -> frame 1 stays
//      frozen at 14 (卡舊畫面) -> the assertion FAILS (teeth, spec 🪤#1).
#include "runtime/resident_eval_graph.h"

#include <cstdio>

namespace sw {
namespace {

// atomic symbol whose id == a registered NodeSpec type (so the cache's evaluate path resolves).
Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

int runResidentCacheSelfTest(bool injectBug) {
  Symbol cst = atomic("Const", {{"value", "value", "Float", 0.0f}}, {{"out", "out", "Float", 0.0f}});
  Symbol mul = atomic("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                      {{"out", "out", "Float", 0.0f}});
  Symbol tim = atomic("Time", {}, {{"out", "out", "Float", 0.0f}});

  // === STATIC graph: Const#1(5) -> Mul.a, Const#2(3) -> Mul.b -> output ===
  SymbolLibrary sl;
  sl.symbols["Const"] = cst;
  sl.symbols["Multiply"] = mul;
  Symbol sroot; sroot.id = "S"; sroot.name = "S"; sroot.atomic = false;
  sroot.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 5.0f;
  SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = 3.0f;
  SymbolChild cm; cm.id = 3; cm.symbolId = "Multiply";
  sroot.children = {c1, c2, cm};
  sroot.connections = {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  sl.symbols["S"] = sroot; sl.rootId = "S";

  ResidentEvalGraph g = buildEvalGraph(sl, "S");
  initResidentCache(g);
  ResidentEvalCtx ctx;  // localTime = localFxTime = 0
  auto outP = g.outputs["out"];  // (Multiply path "3", "out")

  float f0 = pullResidentFloat(g, outP.first, outP.second, ctx);  // first pull: 5*3 = 15

  // Mutate Const#1's projected constant 5 -> 9 with NO edit-time bump. Cache must hide it.
  for (ResidentInput& in : g.nodes[g.byPath["1"]].inputs)
    if (in.slotId == "value") in.constant = 9.0f;
  float f1 = pullResidentFloat(g, outP.first, outP.second, ctx);  // still 15 (short-circuit)
  bool cacheHolds = (f0 == 15.0f && f1 == 15.0f);

  // Edit-time push: bump Const#1.out sourceVersion -> dirty -> propagates (sum) -> 9*3 = 27.
  g.nodes[g.byPath["1"]].outCache["out"].sourceVersion++;
  float f2 = pullResidentFloat(g, outP.first, outP.second, ctx);
  bool editPush = (f2 == 27.0f);

  // === LIVE graph: Time -> Mul.a, Const(7) -> Mul.b -> output ===
  SymbolLibrary ll;
  ll.symbols["Time"] = tim;
  ll.symbols["Const"] = cst;
  ll.symbols["Multiply"] = mul;
  Symbol lroot; lroot.id = "L"; lroot.name = "L"; lroot.atomic = false;
  lroot.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild lt; lt.id = 1; lt.symbolId = "Time";
  SymbolChild lc; lc.id = 2; lc.symbolId = "Const"; lc.overrides["value"] = 7.0f;
  SymbolChild lm; lm.id = 3; lm.symbolId = "Multiply";
  lroot.children = {lt, lc, lm};
  lroot.connections = {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  ll.symbols["L"] = lroot; ll.rootId = "L";

  ResidentEvalGraph lg = buildEvalGraph(ll, "L");
  initResidentCache(lg);
  auto loutP = lg.outputs["out"];

  ResidentEvalCtx t0; t0.localFxTime = 2.0f;
  float L0 = pullResidentFloat(lg, loutP.first, loutP.second, t0);  // 2*7 = 14

  if (!injectBug) bumpLiveSources(lg);  // 🪤#1: every frame. injectBug skips it (teeth).

  ResidentEvalCtx t1; t1.localFxTime = 5.0f;
  float L1 = pullResidentFloat(lg, loutP.first, loutP.second, t1);  // want 5*7 = 35 (recomputed)
  bool liveTracks = (L0 == 14.0f && L1 == 35.0f);

  bool pass = cacheHolds && editPush && liveTracks;
  printf("[selftest-residentcache] static(f0=%.1f f1=%.1f cache@15)=%d editPush(f2=%.1f want27)=%d "
         "live(L0=%.1f L1=%.1f want35)=%d -> %s\n",
         f0, f1, cacheHolds, f2, editPush, L0, L1, liveTracks, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
