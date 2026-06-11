// Headless RED->GREEN proof of slice-3 rest — the remaining S11 edits, each asserted against a
// freshly REBUILT graph (patch == rebuild) plus cache-preservation probes. Together with the
// set-const / add-connection goldens (resident_eval_patch_selftest.cpp) this completes the
// six-edit sweep the spec's batch-1 golden demands (S11①-⑥ + S1).
//   A. remove-connection: Const(5)->Mul.a removed -> falls back to the KEPT pre-wire constant (1)
//      -> 1*3 = 3 == rebuild-without-the-wire.
//   B. change-default (IsDefault filter): two Consts, one overriding (10) one on the default; set
//      the definition default 0 -> 4. Only the defaulting instance updates; the overriding one is
//      NOT invalidated — probed by poisoning its constant out-of-band (a recompute would yield 99;
//      the pull must return the cached 10). 10*4 = 40 == rebuild.
//   C. add-child: broadcast across reuse (Comp used twice -> "1/9" AND "2/9" appear, values
//      instance-isolated), plus the dangling-resolution force: a build-time wire to a missing
//      child 5 evaluates as 0; adding child 5 must force the consumer (the dangling fixed-1
//      version contribution aliases the fresh node's 1 — resolvability is the tripwire) -> 2*3=6.
//   D. remove-child: same-scope sibling falls back to its effectiveInput ((1*3)*2 = 6 == rebuild);
//      an untouched root branch keeps its cache (out-of-band poison stays invisible); removing the
//      compound's output producer leaves the cross-boundary consumer dangling -> 0 == rebuild.
//   E. remove-input-def (S13 收屍): boundary wire + obsolete override scrubbed; the inner consumer
//      falls back to its own default -> 1*4 = 4 == rebuild.
// injectBug performs B's edit by writing the definition + constants directly, SKIPPING the
// edit-time bump -> the pull stays at the stale cached 0 (卡舊) -> FAILS (teeth).
#include <cstdio>

#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

Symbol atom(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s;
  s.id = id;
  s.name = id;
  s.atomic = true;
  s.inputDefs = std::move(ins);
  s.outputDefs = std::move(outs);
  return s;
}

SymbolLibrary baseLib(float constDef = 0.0f) {
  SymbolLibrary lib;
  lib.symbols["Const"] =
      atom("Const", {{"value", "value", "Float", constDef}}, {{"out", "out", "Float", 0.0f}});
  lib.symbols["Multiply"] = atom("Multiply", {{"a", "a", "Float", 1.0f}, {"b", "b", "Float", 1.0f}},
                                 {{"out", "out", "Float", 0.0f}});
  return lib;
}

Symbol compound(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs,
                std::vector<SymbolChild> children, std::vector<SymbolConnection> conns) {
  Symbol s;
  s.id = id;
  s.name = id;
  s.atomic = false;
  s.inputDefs = std::move(ins);
  s.outputDefs = std::move(outs);
  s.children = std::move(children);
  s.connections = std::move(conns);
  return s;
}

ResidentEvalGraph build(const SymbolLibrary& lib) {
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

float pullOut(ResidentEvalGraph& g, const ResidentEvalCtx& ctx, const char* outId = "out") {
  auto it = g.outputs.find(outId);
  if (it == g.outputs.end()) return -999.0f;
  return pullResidentFloat(g, it->second.first, it->second.second, ctx);
}

void poisonConstant(ResidentEvalGraph& g, const char* path, const char* slot, float v) {
  auto it = g.byPath.find(path);
  if (it == g.byPath.end()) return;
  for (ResidentInput& in : g.nodes[it->second].inputs)
    if (in.slotId == slot) in.constant = v;  // out-of-band: NO bump — a cache probe
}

}  // namespace

int runResidentLibPatchSelfTest(bool injectBug) {
  ResidentEvalCtx ctx;

  // === A. patchRemoveConnection: restore kept constant + force ===
  SymbolLibrary la = baseLib();
  SymbolChild ac1{1, "Const", {{"value", 5.0f}}, 0, 0};
  SymbolChild am{2, "Multiply", {{"b", 3.0f}}, 0, 0};
  la.symbols["RA"] = compound("RA", {}, {{"out", "out", "Float", 0.0f}}, {ac1, am},
                              {{1, "out", 2, "a"}, {2, "out", kSymbolBoundary, "out"}});
  la.rootId = "RA";
  ResidentEvalGraph ga = build(la);
  float a0 = pullOut(ga, ctx);  // 5*3 = 15, cached
  patchRemoveConnection(ga, "2", "a");
  float a1 = pullOut(ga, ctx);  // kept pre-wire constant a=1 -> 1*3 = 3
  SymbolLibrary laR = la;
  laR.symbols["RA"].connections = {{2, "out", kSymbolBoundary, "out"}};
  ResidentEvalGraph gaR = build(laR);
  float aR = pullOut(gaR, ctx);
  bool removeConnOk = (a0 == 15.0f && a1 == 3.0f && a1 == aR);

  // === B. patchLibSetDefault: IsDefault filter + overriding instance keeps cache ===
  SymbolLibrary lb = baseLib();
  SymbolChild k1{1, "Const", {{"value", 10.0f}}, 0, 0};  // overrides -> filter must skip it
  SymbolChild k2{2, "Const", {}, 0, 0};                  // on the default -> must update
  SymbolChild bm{3, "Multiply", {}, 0, 0};
  lb.symbols["RB"] =
      compound("RB", {}, {{"out", "out", "Float", 0.0f}}, {k1, k2, bm},
               {{1, "out", 3, "a"}, {2, "out", 3, "b"}, {3, "out", kSymbolBoundary, "out"}});
  lb.rootId = "RB";
  ResidentEvalGraph gb = build(lb);
  float b0 = pullOut(gb, ctx);        // 10*0 = 0, cached
  poisonConstant(gb, "1", "value", 99.0f);  // probe: a recompute of k1 would now yield 99
  if (injectBug) {
    // Buggy edit: definition + projected constant written directly, NO edit-time bump -> stale.
    for (SlotDef& d : lb.symbols["Const"].inputDefs)
      if (d.id == "value") d.def = 4.0f;
    poisonConstant(gb, "2", "value", 4.0f);
  } else {
    patchLibSetDefault(lb, gb, "Const", "value", 4.0f);
  }
  float b1 = pullOut(gb, ctx);  // want 10 (k1 CACHED, not 99) * 4 = 40
  SymbolLibrary lbR = lb;       // lib already carries def=4 in both arms
  lbR.rootId = "RB";
  ResidentEvalGraph gbR = build(lbR);
  float bR = pullOut(gbR, ctx);  // rebuild: override 10 * def 4 = 40
  bool setDefOk = (b0 == 0.0f && b1 == 40.0f && bR == 40.0f);

  // === C. patchLibAddChild: reuse broadcast + dangling-resolution force ===
  SymbolChild im{1, "Multiply", {{"a", 2.0f}, {"b", 3.0f}}, 0, 0};
  SymbolLibrary lc = baseLib();
  lc.symbols["Comp"] = compound("Comp", {}, {{"out1", "out1", "Float", 0.0f}}, {im},
                                {{1, "out", kSymbolBoundary, "out1"}});
  SymbolChild cc1{1, "Comp", {}, 0, 0};
  SymbolChild cc2{2, "Comp", {}, 0, 0};
  SymbolChild cm{3, "Multiply", {}, 0, 0};
  lc.symbols["RC"] =
      compound("RC", {}, {{"out", "out", "Float", 0.0f}}, {cc1, cc2, cm},
               {{1, "out1", 3, "a"}, {2, "out1", 3, "b"}, {3, "out", kSymbolBoundary, "out"}});
  lc.rootId = "RC";
  ResidentEvalGraph gc = build(lc);
  float c0 = pullOut(gc, ctx);  // (2*3)*(2*3) = 36
  bool added = patchLibAddChild(lc, gc, "Comp", 9, "Const");
  float c1 = pullOut(gc, ctx);  // unwired add -> still 36
  bool bothInstances = gc.byPath.count("1/9") == 1 && gc.byPath.count("2/9") == 1;
  patchSetConstant(gc, "1/9", "value", 7.0f);
  float c19 = pullResidentFloat(gc, "1/9", "out", ctx);  // 7 (instance-isolated)
  float c29 = pullResidentFloat(gc, "2/9", "out", ctx);  // 0 (sibling instance untouched)
  bool addOk = added && c0 == 36.0f && c1 == 36.0f && bothInstances && c19 == 7.0f && c29 == 0.0f;

  // dangling-resolution force: a wire to missing child 5 evaluates 0; adding child 5 must dirty
  // the consumer even though the version contribution stays 1 (resolvability tripwire).
  SymbolLibrary ld = baseLib(2.0f);  // Const definition default = 2
  SymbolChild dm{1, "Multiply", {{"b", 3.0f}}, 0, 0};
  ld.symbols["RD"] = compound("RD", {}, {{"out", "out", "Float", 0.0f}}, {dm},
                              {{5, "out", 1, "a"}, {1, "out", kSymbolBoundary, "out"}});
  ld.rootId = "RD";
  ResidentEvalGraph gd = build(ld);
  float d0 = pullOut(gd, ctx);  // dangling a=0 -> 0*3 = 0, cached
  patchLibAddChild(ld, gd, "RD", 5, "Const");
  float d1 = pullOut(gd, ctx);  // resolved: Const def 2 -> 2*3 = 6 (frozen 0 = the bug this guards)
  bool danglingOk = (d0 == 0.0f && d1 == 6.0f);

  // === D. patchLibRemoveChild: same-scope restore, untouched-branch cache, cross-boundary dangling ===
  SymbolLibrary le = baseLib();
  SymbolChild ic{1, "Const", {{"value", 5.0f}}, 0, 0};
  SymbolChild iw{2, "Multiply", {{"b", 3.0f}}, 0, 0};
  le.symbols["Comp"] = compound("Comp", {}, {{"out1", "out1", "Float", 0.0f}}, {ic, iw},
                                {{1, "out", 2, "a"}, {2, "out", kSymbolBoundary, "out1"}});
  SymbolChild ec{1, "Comp", {}, 0, 0};
  SymbolChild em{2, "Multiply", {{"b", 2.0f}}, 0, 0};
  SymbolChild eu{3, "Const", {{"value", 7.0f}}, 0, 0};  // untouched branch (cache probe)
  le.symbols["RE"] = compound("RE", {}, {{"out", "out", "Float", 0.0f}}, {ec, em, eu},
                              {{1, "out1", 2, "a"}, {2, "out", kSymbolBoundary, "out"}});
  le.rootId = "RE";
  ResidentEvalGraph ge = build(le);
  float e0 = pullOut(ge, ctx);                          // (5*3)*2 = 30
  float eu0 = pullResidentFloat(ge, "3", "out", ctx);   // 7, cached
  poisonConstant(ge, "3", "value", 99.0f);              // probe: recompute would yield 99
  bool removed = patchLibRemoveChild(le, ge, "Comp", 1);  // inner Const gone; iw.a falls back to 1
  float e1 = pullOut(ge, ctx);                          // (1*3)*2 = 6
  float eu1 = pullResidentFloat(ge, "3", "out", ctx);   // still cached 7 (migration preserved)
  SymbolLibrary leR = le;                               // le already edited
  ResidentEvalGraph geR = build(leR);
  float eR = pullOut(geR, ctx);
  bool removeChildOk = removed && e0 == 30.0f && e1 == 6.0f && e1 == eR && eu0 == 7.0f && eu1 == 7.0f;

  bool removed2 = patchLibRemoveChild(le, ge, "Comp", 2);  // the output producer -> consumer dangles
  float e2 = pullOut(ge, ctx);                             // M.a dangling 0 -> 0*2 = 0
  ResidentEvalGraph geR2 = build(le);
  float eR2 = pullOut(geR2, ctx);
  bool crossBoundaryOk = removed2 && e2 == 0.0f && e2 == eR2;

  // === E. patchLibRemoveInputDef: 收屍 + inner fallback ===
  SymbolLibrary lf = baseLib();
  SymbolChild fm{1, "Multiply", {{"b", 4.0f}}, 0, 0};
  lf.symbols["Comp2"] =
      compound("Comp2", {{"g1", "g1", "Float", 3.0f}}, {{"out1", "out1", "Float", 0.0f}}, {fm},
               {{kSymbolBoundary, "g1", 1, "a"}, {1, "out", kSymbolBoundary, "out1"}});
  SymbolChild fc{1, "Comp2", {{"g1", 5.0f}}, 0, 0};  // override feeds the boundary input
  lf.symbols["RF"] = compound("RF", {}, {{"out", "out", "Float", 0.0f}}, {fc},
                              {{1, "out1", kSymbolBoundary, "out"}});
  lf.rootId = "RF";
  ResidentEvalGraph gf = build(lf);
  float f0 = pullOut(gf, ctx);  // 5*4 = 20
  bool dropped = patchLibRemoveInputDef(lf, gf, "Comp2", "g1");
  float f1 = pullOut(gf, ctx);  // inner a falls back to its own default 1 -> 4
  ResidentEvalGraph gfR = build(lf);
  float fR = pullOut(gfR, ctx);
  bool overrideGone = lf.symbols["RF"].children[0].overrides.count("g1") == 0;
  bool removeDefOk = dropped && f0 == 20.0f && f1 == 4.0f && f1 == fR && overrideGone;

  // === F. consecutive structural edits, NO pull between (refuter A-1: the stale sourceVersion
  // field must not regress baseVersion — downstream kept-cache sums would collide -> 卡舊) ===
  SymbolLibrary lg6 = baseLib(2.0f);
  SymbolChild m1{1, "Multiply", {{"b", 3.0f}}, 0, 0};
  SymbolChild m2{2, "Multiply", {{"b", 2.0f}}, 0, 0};
  lg6.symbols["RG"] = compound("RG", {}, {{"out", "out", "Float", 0.0f}}, {m1, m2},
                               {{5, "out", 1, "a"},  // dangling until child 5 exists
                                {1, "out", 2, "a"},
                                {2, "out", kSymbolBoundary, "out"}});
  lg6.rootId = "RG";
  ResidentEvalGraph gg = build(lg6);
  float g0 = pullOut(gg, ctx);                     // (0*3)*2 = 0, caches built
  patchLibAddChild(lg6, gg, "RG", 5, "Const");     // edit 1 (no pull between!)
  patchLibRemoveChild(lg6, gg, "RG", 5);           // edit 2: back-to-back command group
  float g1 = pullOut(gg, ctx);                     // wire to 5 scrubbed -> a=1 -> (1*3)*2 = 6
  ResidentEvalGraph ggR = build(lg6);
  float gR = pullOut(ggR, ctx);
  bool seqOk = (g0 == 0.0f && g1 == 6.0f && g1 == gR);

  // === G. setDefault refreshes the KEPT fallback under a wire (refuter A-2): disconnect after a
  // default edit must restore the NEW default ===
  SymbolLibrary lh = baseLib();
  SymbolChild hc{1, "Const", {{"value", 5.0f}}, 0, 0};
  SymbolChild hm{2, "Multiply", {{"b", 3.0f}}, 0, 0};  // a: no override, wired
  lh.symbols["RH"] = compound("RH", {}, {{"out", "out", "Float", 0.0f}}, {hc, hm},
                              {{1, "out", 2, "a"}, {2, "out", kSymbolBoundary, "out"}});
  lh.rootId = "RH";
  ResidentEvalGraph gh = build(lh);
  float h0 = pullOut(gh, ctx);                         // 5*3 = 15
  patchLibSetDefault(lh, gh, "Multiply", "a", 9.0f);   // a is wired -> fallback refresh, no bump
  float h1 = pullOut(gh, ctx);                         // still 15 (value not consumed)
  auto& hconns = lh.symbols["RH"].connections;         // command pair: lib disconnect + projection
  hconns.erase(hconns.begin());
  patchRemoveConnection(gh, "2", "a");
  float h2 = pullOut(gh, ctx);                         // restored fallback = NEW default 9 -> 27
  ResidentEvalGraph ghR = build(lh);
  float hR = pullOut(ghR, ctx);
  bool keptDefOk = (h0 == 15.0f && h1 == 15.0f && h2 == 27.0f && h2 == hR);

  // === H. setConstant on a WIRED input stores the fallback (refuter A-3, TiXL SetTypedInputValue
  // writes regardless of wiring; disconnect falls back to it) ===
  SymbolLibrary li = lh;  // same shape, fresh graph (lh already disconnected above — rebuild base)
  li.symbols["RH"].connections = {{1, "out", 2, "a"}, {2, "out", kSymbolBoundary, "out"}};
  ResidentEvalGraph gi = build(li);
  float i0 = pullOut(gi, ctx);                       // 9(def from G's lib edit)*... a wired: 5*3=15
  li.symbols["RH"].children[1].overrides["a"] = 7.0f;  // command pair: lib override...
  patchSetConstant(gi, "2", "a", 7.0f);                // ...+ projection store (wired -> no bump)
  float i1 = pullOut(gi, ctx);                         // still 15
  auto& iconns = li.symbols["RH"].connections;
  iconns.erase(iconns.begin());
  patchRemoveConnection(gi, "2", "a");
  float i2 = pullOut(gi, ctx);                       // falls back to the stored 7 -> 7*3 = 21
  ResidentEvalGraph giR = build(li);
  float iR = pullOut(giR, ctx);
  bool wiredStoreOk = (i0 == 15.0f && i1 == 15.0f && i2 == 21.0f && i2 == iR);

  // === I. compound setDefault routes through migration (refuter A-4: no silent lib/g desync);
  // the IsDefault filter emerges (overriding instance keeps its value AND its cache) ===
  SymbolLibrary lj = baseLib();
  SymbolChild jm{1, "Multiply", {{"b", 4.0f}}, 0, 0};
  lj.symbols["CompJ"] =
      compound("CompJ", {{"g1", "g1", "Float", 3.0f}}, {{"out1", "out1", "Float", 0.0f}}, {jm},
               {{kSymbolBoundary, "g1", 1, "a"}, {1, "out", kSymbolBoundary, "out1"}});
  SymbolChild jc1{1, "CompJ", {}, 0, 0};                  // on the default
  SymbolChild jc2{2, "CompJ", {{"g1", 5.0f}}, 0, 0};      // overriding
  SymbolChild jM{3, "Multiply", {}, 0, 0};
  lj.symbols["RJ"] =
      compound("RJ", {}, {{"out", "out", "Float", 0.0f}}, {jc1, jc2, jM},
               {{1, "out1", 3, "a"}, {2, "out1", 3, "b"}, {3, "out", kSymbolBoundary, "out"}});
  lj.rootId = "RJ";
  ResidentEvalGraph gj = build(lj);
  float j0 = pullOut(gj, ctx);  // (3*4)*(5*4) = 240
  patchLibSetDefault(lj, gj, "CompJ", "g1", 9.0f);
  float j1 = pullOut(gj, ctx);  // defaulting instance follows (9*4), overriding stays (5*4) -> 720
  // (cache-preservation probing under migration lives in test D — an out-of-band constant poison
  // here would itself read as an input diff and force the node, voiding the probe.)
  ResidentEvalGraph gjR = build(lj);
  float jR = pullOut(gjR, ctx);
  bool compoundDefOk = (j0 == 240.0f && j1 == 720.0f && jR == 720.0f);

  bool pass = removeConnOk && setDefOk && addOk && danglingOk && removeChildOk && crossBoundaryOk &&
              removeDefOk && seqOk && keptDefOk && wiredStoreOk && compoundDefOk;
  printf("[selftest-residentlibpatch] removeConn(a0=%.1f a1=%.1f want3)=%d "
         "setDefault(b0=%.1f b1=%.1f want40 isDefaultFilter+cache)=%d "
         "addChild(c1=%.1f both=%d c19=%.1f c29=%.1f)=%d dangling(d0=%.1f d1=%.1f want6)=%d "
         "removeChild(e1=%.1f want6 cache7=%.1f)=%d crossBoundary(e2=%.1f want0)=%d "
         "removeInputDef(f1=%.1f want4)=%d | editSeqNoPull(g1=%.1f want6)=%d "
         "keptDefault(h2=%.1f want27)=%d wiredStore(i2=%.1f want21)=%d "
         "compoundDefault(j1=%.1f want720)=%d -> %s\n",
         a0, a1, removeConnOk, b0, b1, setDefOk, c1, bothInstances, c19, c29, addOk, d0, d1,
         danglingOk, e1, eu1, removeChildOk, e2, crossBoundaryOk, f1, removeDefOk, g1, seqOk, h2,
         keptDefOk, i2, wiredStoreOk, j1, compoundDefOk, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
