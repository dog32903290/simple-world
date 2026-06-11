// Headless RED->GREEN proof of the dynamic compound spec table (批次 3 N1): a compound
// symbol becomes a findSpec-resolvable NodeSpec (ports mirror its defs) so the canvas /
// inspector / cook treat a compound child like any operator; built-ins always win on id
// clash (a compound named "Const" cannot shadow the atomic); a refresh after a def edit
// updates the spec, and a refresh from an empty lib drops stale entries wholesale.
// injectBug skips the refresh after the def edit -> the updated-default assertion FAILS.
#include "runtime/graph_bridge.h"

#include <cstdio>

namespace sw {

int runCompoundSpecSelfTest(bool injectBug) {
  SymbolLibrary lib;
  Symbol scale;
  scale.id = "c-scale";
  scale.name = "Scale";
  scale.atomic = false;
  scale.inputDefs = {{"in", "in", "Float", 1.0f}, {"pts", "pts", "Points", 0.0f}};
  scale.outputDefs = {{"out", "out", "Float", 0.0f}};
  lib.symbols[scale.id] = scale;
  Symbol shadow;  // a compound that tries to shadow the atomic Const
  shadow.id = "Const";
  shadow.name = "NotTheRealConst";
  shadow.atomic = false;
  lib.symbols[shadow.id] = shadow;

  refreshCompoundSpecs(lib);

  const NodeSpec* cs = findSpec("c-scale");
  bool portsOk = cs && cs->ports.size() == 3 && cs->evaluate == nullptr &&
                 cs->ports[0].id == "in" && cs->ports[0].isInput &&
                 cs->ports[0].dataType == "Float" && cs->ports[0].def == 1.0f &&
                 cs->ports[1].dataType == "Points" &&
                 cs->ports[2].id == "out" && !cs->ports[2].isInput;
  const NodeSpec* atom = findSpec("Const");
  bool builtinWins = atom && atom->evaluate != nullptr;  // the registry's Const, not the compound

  // Def edit + refresh -> the spec follows. injectBug skips the refresh -> stale def caught.
  lib.symbols["c-scale"].inputDefs[0].def = 6.0f;
  if (!injectBug) refreshCompoundSpecs(lib);
  const NodeSpec* cs2 = findSpec("c-scale");
  bool refreshOk = cs2 && cs2->ports[0].def == 6.0f;

  // Wholesale staleness: an empty lib leaves no compound specs behind.
  refreshCompoundSpecs(SymbolLibrary{});
  bool staleOk = findSpec("c-scale") == nullptr && findSpec("Const") != nullptr;

  bool pass = portsOk && builtinWins && refreshOk && staleOk;
  printf("[selftest-compoundspec] ports=%d builtinWins=%d refresh(def 6)=%d staleDrop=%d -> %s\n",
         portsOk ? 1 : 0, builtinWins ? 1 : 0, refreshOk ? 1 : 0, staleOk ? 1 : 0,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
