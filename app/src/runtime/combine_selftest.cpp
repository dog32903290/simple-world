// runtime/combine_selftest — headless RED->GREEN proof of combineChildren (combine.h).
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/combine.h"
#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"  // v2 roundtrip leg
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

  printf("[selftest-combine] structure+rewire+overrides+eval-identical%s -> %s\n",
         injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
