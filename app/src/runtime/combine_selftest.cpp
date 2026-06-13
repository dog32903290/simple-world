// runtime/combine_selftest — headless RED->GREEN proof of combineChildren (combine.h).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/combine.h"
#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"  // v2 roundtrip leg
#include "runtime/curve.h"          // animation-survival leg
#include "runtime/curve_animator.h"
#include "runtime/graph.h"         // defaultParticleGraph (structural leg seed)
#include "runtime/graph_bridge.h"  // libFromGraph / atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"

namespace sw {

int runCombineSelfTest(bool injectBug) {
  bool ok = true;

  // --- leg 1: STRUCTURE on the real default graph. Combine {RadialPoints, ParticleSystem}:
  // Radial->PS.emit is internal; Turb->PS.forces crosses IN; PS.result->Draw crosses OUT.
  {
    SymbolLibrary lib = libFromGraph(defaultParticleGraph());
    Symbol* root = lib.find(lib.rootId);
    int radialId = 0, psId = 0;
    for (const SymbolChild& c : root->children) {
      if (c.symbolId == "RadialPoints") radialId = c.id;
      if (c.symbolId == "ParticleSystem") psId = c.id;
    }
    const size_t baseChildren = root->children.size();
    const std::map<std::string, float> radialOv =
        childById(*root, radialId)->overrides;  // ride-along check

    CombineResult r = combineChildren(lib, lib.rootId, {radialId, psId}, "發射器");
    const Symbol* sym = r.ok ? lib.find(r.newSymbolId) : nullptr;
    ok = ok && r.ok && sym && !sym->atomic && sym->name == "發射器";
    ok = ok && sym && sym->children.size() == 2 && sym->connections.size() == 3 &&
         sym->inputDefs.size() == 1 && sym->outputDefs.size() == 1 &&
         sym->inputDefs[0].id == "forces" && sym->outputDefs[0].id == "result";
    // parent: -2 children +1 instance; 1 inbound + 1 outbound rewire, nothing else
    root = lib.find(lib.rootId);
    ok = ok && root->children.size() == baseChildren - 1 &&
         root->connections.size() == 2 && childById(*root, r.newChildId) &&
         childById(*root, r.newChildId)->symbolId == r.newSymbolId;
    // overrides carried verbatim onto the moved RadialPoints (new id 1 or 2; find by type)
    bool ovOk = false;
    if (sym)
      for (const SymbolChild& c : sym->children)
        if (c.symbolId == "RadialPoints") ovOk = c.overrides == radialOv;
    ok = ok && ovOk;
    // the combined graph still INLINES: the instance's producer resolves to a real path
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    std::string vp = viewProducerPath(lib, "", r.newChildId);
    ok = ok && !vp.empty() && g.node(vp);
  }

  // --- leg 2: EVALUATION-IDENTICAL on a value graph. Const(4) -> Multiply.a (b stays
  // default 1): combining {Const} must leave the Multiply's value bit-identical. ---
  {
    SymbolLibrary lib;
    lib.rootId = "Root";
    lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
    lib.symbols["Multiply"] = atomicSymbolFromSpec(*findSpec("Multiply"));
    Symbol root;
    root.id = "Root";
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 4.0f;
    SymbolChild m;  m.id = 2;  m.symbolId = "Multiply";
    root.children = {c1, m};
    root.connections = {{1, "out", 2, "a"}};
    root.nextChildId = 3;
    lib.symbols["Root"] = root;

    ResidentEvalGraph g0 = buildEvalGraph(lib, lib.rootId);
    ResidentEvalCtx ctx;
    const float pre = evalResidentFloat(g0, "2", "out", ctx);

    CombineResult r = combineChildren(lib, "Root", {1}, "");
    ok = ok && r.ok;
    if (injectBug) {  // teeth: drop the parent rewire -> Multiply.a falls to its default
      Symbol* rt = lib.find("Root");
      if (!rt->connections.empty()) rt->connections.pop_back();
    }
    ResidentEvalGraph g1 = buildEvalGraph(lib, lib.rootId);
    const float post = evalResidentFloat(g1, "2", "out", ctx);
    ok = ok && pre == post && pre == 4.0f;

    // and the roundtrip survives disk (CJK-named symbols included via leg 1's shape)
    SymbolLibrary back;
    ok = ok && libFromJsonAny(libToJsonV2(lib), back, nullptr) &&
         libToJsonV2(back) == libToJsonV2(lib);
  }

  // --- leg 3: the 99-def cap refuses BEFORE surgery (kept practical port ceiling, parity
  // with TiXL; no longer a pin-encoding limit since boundary pins moved to their own band). 100
  // Consts all feeding one Multiply... impossible (single-cardinality); instead 100
  // selected Consts each feeding its own outside Multiply = 100 OUTBOUND crossings. ---
  {
    SymbolLibrary lib;
    lib.rootId = "Root";
    lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
    lib.symbols["Multiply"] = atomicSymbolFromSpec(*findSpec("Multiply"));
    Symbol root;
    root.id = "Root";
    std::vector<int> sel;
    for (int i = 1; i <= 100; ++i) {
      SymbolChild c; c.id = i; c.symbolId = "Const";
      SymbolChild m; m.id = 100 + i; m.symbolId = "Multiply";
      root.children.push_back(c);
      root.children.push_back(m);
      root.connections.push_back({i, "out", 100 + i, "a"});
      sel.push_back(i);
    }
    root.nextChildId = 201;
    lib.symbols["Root"] = root;
    CombineResult r = combineChildren(lib, "Root", sel, "");
    ok = ok && !r.ok && !lib.symbols.count("Compound-1") &&
         lib.find("Root")->children.size() == 200;  // refused = untouched
  }

  // --- leg 4: ANIMATION moves with the combined children (BROKEN-1). Const(1).value animated
  // (ramp 0..8 over t=0..4) -> Multiply.a. Combine {Const(1)}: (a) the new compound's animator
  // carries the curve under the REMAPPED child id, sampling equal to the original; (b) the parent
  // has NO殭屍 entry on the removed child 1; (c) snapshot-restore (combine is NOT undoable — the
  // command stack is cleared — so "undo" = restore the pre-combine parent animator snapshot) puts
  // the curve back on parent child 1 at the original sample value. ---
  {
    SymbolLibrary lib;
    lib.rootId = "Root";
    lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
    lib.symbols["Multiply"] = atomicSymbolFromSpec(*findSpec("Multiply"));
    Symbol root;
    root.id = "Root";
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const";
    SymbolChild m;  m.id = 2;  m.symbolId = "Multiply";
    root.children = {c1, m};
    root.connections = {{1, "out", 2, "a"}};
    root.nextChildId = 3;
    // animate Const(1).value: linear ramp t=0->0, t=4->8. @t=2 -> 4.0.
    Curve ramp;
    VDefinition k0; k0.value = 0.0;
    k0.inInterpolation = k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1; k1.value = 8.0;
    k1.inInterpolation = k1.outInterpolation = KeyInterpolation::Linear;
    ramp.addOrUpdate(0.0, k0);
    ramp.addOrUpdate(4.0, k1);
    Animator::CurveArray arr; arr.push_back(ramp);
    root.animator.setCurves(1, "value", arr);
    lib.symbols["Root"] = root;

    const float sampleBefore = lib.symbols["Root"].animator.curvesFor(1, "value")->front().sample(2.0);

    // snapshot the parent animator BEFORE combine (the not-undoable "undo" mechanism = snapshot-restore).
    Animator parentSnapshot = lib.symbols["Root"].animator;

    CombineResult r = combineChildren(lib, "Root", {1}, "");
    ok = ok && r.ok;
    const Symbol* comp = r.ok ? lib.find(r.newSymbolId) : nullptr;
    Symbol* rt = lib.find("Root");

    // (a) the moved Const's NEW id inside the compound carries the curve, equal sample.
    bool curveMoved = false;
    if (comp) {
      // the moved Const got a regenerated id (1..N); find it by type.
      for (const SymbolChild& cc : comp->children)
        if (cc.symbolId == "Const") {
          const Animator::CurveArray* a = comp->animator.curvesFor(cc.id, "value");
          if (a && !a->empty() && std::abs(a->front().sample(2.0) - sampleBefore) <= 1e-5)
            curveMoved = true;
        }
    }
    ok = ok && curveMoved;

    // (b) parent has NO殭屍 entry on the removed child 1.
    const bool parentZombie = rt && rt->animator.isInstanceAnimated(1);
    if (injectBug) {
      // teeth: assert the WRONG thing (a zombie SHOULD remain) — must FAIL when the fix is in.
      ok = ok && parentZombie;
    } else {
      ok = ok && !parentZombie;
    }

    // (c) snapshot-restore returns the curve to parent child 1 at the original sample value.
    rt->animator = parentSnapshot;
    const Animator::CurveArray* restored = rt->animator.curvesFor(1, "value");
    ok = ok && restored && !restored->empty() &&
         std::abs(restored->front().sample(2.0) - sampleBefore) <= 1e-5;
  }

  // --- leg 5: ANNOTATIONS travel with combine (R-AN #1, = TiXL Combine.cs:170,250-254). Two children
  // at (0,0) and (300,0); a frame CONTAINING both (the children's point-bbox 0,0..300,0) moves into
  // the new compound with a FRESH id (distinct from the original) re-based to the new canvas, and is
  // REMOVED from the parent. A second frame OUTSIDE the bbox stays on the parent untouched. ---
  {
    SymbolLibrary lib;
    lib.rootId = "Root";
    lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
    Symbol root;
    root.id = "Root";
    SymbolChild a; a.id = 1; a.symbolId = "Const"; a.x = 0;   a.y = 0;
    SymbolChild b; b.id = 2; b.symbolId = "Const"; b.x = 300; b.y = 0;
    root.children = {a, b};
    root.nextChildId = 3;
    Annotation framing;  // contains the selected children's point-bbox 0,0..300,0
    framing.id = "frame-inside"; framing.title = "群"; framing.x = -10; framing.y = -10;
    framing.w = 330; framing.h = 60;
    Annotation outside;  // far away, contains neither child
    outside.id = "frame-outside"; outside.x = 1000; outside.y = 1000; outside.w = 50; outside.h = 50;
    root.annotations = {framing, outside};
    lib.symbols["Root"] = root;

    CombineResult r = combineChildren(lib, "Root", {1, 2}, "群組");
    ok = ok && r.ok;
    const Symbol* comp = r.ok ? lib.find(r.newSymbolId) : nullptr;
    Symbol* rt = lib.find("Root");

    // (a) the new compound carries exactly one annotation, with a FRESH id (not "frame-inside")
    //     and the carried text verbatim.
    bool movedOk = comp && comp->annotations.size() == 1 &&
                   comp->annotations[0].id != "frame-inside" &&
                   comp->annotations[0].title == "群";
    // (b) the parent keeps ONLY the outside frame.
    bool parentOk = rt && rt->annotations.size() == 1 && rt->annotations[0].id == "frame-outside";
    if (injectBug) {  // teeth: assert the WRONG thing (compound keeps the ORIGINAL id) — fails when correct
      ok = ok && comp && comp->annotations.size() == 1 && comp->annotations[0].id == "frame-inside";
    } else {
      ok = ok && movedOk && parentOk;
    }
  }

  printf("[selftest-combine] structure+rewire+overrides+eval-identical+anim-move+annotation%s -> %s\n",
         injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
