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
  // Time is now stateful (evaluate==nullptr); synthetic live Const stands in for headless tests.
  Symbol tim = atomic("Time", {}, {{"out", "out", "Float", 0.0f}});  // kept for buildFlat compat

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

  // Edit-time push: bump Const#1.out baseVersion -> dirty -> propagates (sum) -> 9*3 = 27.
  g.nodes[g.byPath["1"]].outCache["out"].baseVersion++;
  float f2 = pullResidentFloat(g, outP.first, outP.second, ctx);
  bool editPush = (f2 == 27.0f);

  // === LIVE graph: LiveConst(2,triggerAlways) -> Mul.a, Const(7) -> Mul.b -> output ===
  // Time was here but is now stateful (evaluate==nullptr); a synthetic live Const with
  // triggerOverrides["out"]=Always is the headless-safe stand-in (initResidentCache marks it live).
  // Frame0: LiveConst.value=2 -> 2*7=14. After bumpLiveSources (marks live-Const dirty), mutate
  // the projected constant to 5 -> frame1 recomputes -> 5*7=35. injectBug skips the bump (teeth).
  SymbolLibrary ll;
  ll.symbols["Const"] = cst;
  ll.symbols["Multiply"] = mul;
  Symbol lroot; lroot.id = "L"; lroot.name = "L"; lroot.atomic = false;
  lroot.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild lt; lt.id = 1; lt.symbolId = "Const"; lt.overrides["value"] = 2.0f;
  lt.triggerOverrides["out"] = TriggerOverride::Always;  // synthetic live source
  SymbolChild lc; lc.id = 2; lc.symbolId = "Const"; lc.overrides["value"] = 7.0f;
  SymbolChild lm; lm.id = 3; lm.symbolId = "Multiply";
  lroot.children = {lt, lc, lm};
  lroot.connections = {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}};
  ll.symbols["L"] = lroot; ll.rootId = "L";

  ResidentEvalGraph lg = buildEvalGraph(ll, "L");
  initResidentCache(lg);
  auto loutP = lg.outputs["out"];

  ResidentEvalCtx t0;
  float L0 = pullResidentFloat(lg, loutP.first, loutP.second, t0);  // 2*7 = 14

  if (!injectBug) bumpLiveSources(lg);  // 🪤#1: every frame. injectBug skips it (teeth).

  // Mutate the live-Const's projected constant to 5 (simulates a new live value next frame).
  for (ResidentInput& in : lg.nodes[lg.byPath["1"]].inputs)
    if (in.slotId == "value") in.constant = 5.0f;

  ResidentEvalCtx t1;
  float L1 = pullResidentFloat(lg, loutP.first, loutP.second, t1);  // want 5*7 = 35 (recomputed)
  bool liveTracks = (L0 == 14.0f && L1 == 35.0f);

  // === DANGLING connection (refuter D1): a derived slot whose Connection upstream does not
  // resolve must stay INITIALLY-DIRTY and compute (upstream treated as 0), not freeze on its
  // uninitialized cache. The version-chasing invariant: a slot's sourceVersion is never 0 (TiXL
  // starts at 1, only ++); a fully-dangling derived slot summing to 0 would collide with the
  // initial valueVersion 0 -> false-clean -> permanent 卡舊 (even an edit bump can't rescue it).
  // Remap(Value=dangling→0, RangeInMin=-10, RangeInMax=10, RangeOutMin=0, RangeOutMax=100):
  //   t=(0-(-10))/(10-(-10))=0.5 → out=50. A frozen uninitialized cache returns 0
  //   (distinguishable from 50). Port names match TiXL adjust/Remap.cs (批次12 lane F).
  SymbolLibrary dl;
  dl.symbols["Const"] = cst;
  Symbol rmp = atomic("Remap",
                      {{"Value",       "Value",       "Float", 0.0f},
                       {"RangeInMin",  "RangeInMin",  "Float", 0.0f},
                       {"RangeInMax",  "RangeInMax",  "Float", 1.0f},
                       {"RangeOutMin", "RangeOutMin", "Float", 0.0f},
                       {"RangeOutMax", "RangeOutMax", "Float", 1.0f}},
                      {{"out", "out", "Float", 0.0f}});
  dl.symbols["Remap"] = rmp;
  Symbol droot; droot.id = "D"; droot.name = "D"; droot.atomic = false;
  droot.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild dc; dc.id = 1; dc.symbolId = "Const"; dc.overrides["value"] = 4.0f;
  SymbolChild dr; dr.id = 2; dr.symbolId = "Remap";
  dr.overrides["RangeInMin"]  = -10.0f;
  dr.overrides["RangeInMax"]  =  10.0f;
  dr.overrides["RangeOutMin"] =   0.0f;
  dr.overrides["RangeOutMax"] = 100.0f;
  droot.children = {dc, dr};
  droot.connections = {{1, "out", 2, "Value"}, {2, "out", kSymbolBoundary, "out"}};
  dl.symbols["D"] = droot; dl.rootId = "D";

  ResidentEvalGraph dg = buildEvalGraph(dl, "D");
  for (ResidentInput& in : dg.nodes[dg.byPath["2"]].inputs)
    if (in.slotId == "Value") in.srcNodePath = "999";  // orphan upstream (unresolvable)
  initResidentCache(dg);
  auto doutP = dg.outputs["out"];
  // Value=0 (dangling→0), RangeIn=-10..10, RangeOut=0..100: t=(0-(-10))/20=0.5 → out=50.
  float D0 = pullResidentFloat(dg, doutP.first, doutP.second, ctx);
  bool danglingComputes = (D0 == 50.0f);  // bug: frozen uninitialized cache 0 → false-clean

  bool pass = cacheHolds && editPush && liveTracks && danglingComputes;
  printf("[selftest-residentcache] static(f0=%.1f f1=%.1f cache@15)=%d editPush(f2=%.1f want27)=%d "
         "live(L0=%.1f L1=%.1f want35)=%d dangling(D0=%.1f want50)=%d -> %s\n",
         f0, f1, cacheHolds, f2, editPush, L0, L1, liveTracks, D0, danglingComputes,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
