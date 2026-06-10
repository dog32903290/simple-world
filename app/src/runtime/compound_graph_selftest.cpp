// Headless RED->GREEN proof of the nested compound model (compound_graph.h). Builds a small
// library: two atomic symbols (RadialPoints / DrawPoints) + a compound "TwoRings" whose subgraph
// has TWO RadialPoints children (reuse — same symbolId) plus a DrawPoints, with child1 overriding
// Radius and a boundary wire to the compound's external output. Asserts: structure, reuse-shares-
// definition, override-vs-default, reuse ISOLATION (an override on one instance does not leak to
// the other or pollute the definition), default fallback, and the boundary sentinel.
// injectBug pollutes the RadialPoints definition (Radius def 1 -> 2) as if an instance override
// leaked into the shared definition; the un-overridden reuse child then reads 2, so the
// reuse-isolation assertion FAILS — teeth.
#include "runtime/compound_graph.h"

#include <cstdio>

namespace sw {

int runCompoundModelSelfTest(bool injectBug) {
  SymbolLibrary lib;

  // atomic: RadialPoints — Count(def 100), Radius(def 1) -> points(Points)
  Symbol radial;
  radial.id = "RadialPoints"; radial.name = "RadialPoints"; radial.atomic = true;
  radial.inputDefs = {{"Count", "Count", "Float", 100.0f}, {"Radius", "Radius", "Float", 1.0f}};
  radial.outputDefs = {{"points", "points", "Points", 0.0f}};
  if (injectBug) radial.inputDefs[1].def = 2.0f;  // BUG: shared definition polluted
  lib.symbols[radial.id] = radial;

  // atomic: DrawPoints — points(Points) -> out(Command)
  Symbol draw;
  draw.id = "DrawPoints"; draw.name = "DrawPoints"; draw.atomic = true;
  draw.inputDefs = {{"points", "points", "Points", 0.0f}};
  draw.outputDefs = {{"out", "out", "Command", 0.0f}};
  lib.symbols[draw.id] = draw;

  // compound: TwoRings — RadialPoints x2 (reuse) + DrawPoints; child1 overrides Radius=2.
  Symbol comp;
  comp.id = "TwoRings"; comp.name = "TwoRings"; comp.atomic = false;
  comp.outputDefs = {{"image", "image", "Command", 0.0f}};  // external output
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints"; c1.overrides["Radius"] = 2.0f;
  SymbolChild c2; c2.id = 2; c2.symbolId = "RadialPoints";  // reuse, no override
  SymbolChild c3; c3.id = 3; c3.symbolId = "DrawPoints";
  comp.children = {c1, c2, c3};
  comp.connections = {
      {1, "points", 3, "points"},                 // child1.points -> child3.points
      {3, "out", kSymbolBoundary, "image"},        // child3.out -> compound external output (sentinel)
  };
  lib.symbols[comp.id] = comp;
  lib.rootId = "TwoRings";

  const Symbol* tr = lib.find("TwoRings");
  const Symbol* rp = lib.find("RadialPoints");
  bool structureOk = tr && rp && tr->children.size() == 3 && rp->atomic && !tr->atomic;
  bool reuseSameDef = tr && tr->children[0].symbolId == tr->children[1].symbolId &&
                      tr->children[0].symbolId == "RadialPoints";

  float r0 = tr ? effectiveInput(lib, tr->children[0], "Radius") : -1.0f;   // override -> 2.0
  float r1 = tr ? effectiveInput(lib, tr->children[1], "Radius") : -1.0f;   // default  -> 1.0 (clean)
  float count0 = tr ? effectiveInput(lib, tr->children[0], "Count") : -1.0f;  // default -> 100
  bool overrideOk = (r0 == 2.0f);
  bool reuseIsolated = (r1 == 1.0f);  // BUG pollutes the def -> r1 == 2.0 -> FAILS
  bool defaultOk = (count0 == 100.0f);

  int boundaryOuts = 0;
  if (tr)
    for (const SymbolConnection& c : tr->connections)
      if (targetIsSymbolOutput(c)) ++boundaryOuts;
  bool sentinelOk = (boundaryOuts == 1);

  bool pass = structureOk && reuseSameDef && overrideOk && reuseIsolated && defaultOk && sentinelOk;
  printf("[selftest-compoundmodel] struct=%d reuse=%d override(r0=%.1f)=%d isolated(r1=%.1f)=%d "
         "default(%.0f)=%d sentinel=%d -> %s\n",
         structureOk, reuseSameDef, r0, overrideOk, r1, reuseIsolated, count0, defaultOk, sentinelOk,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
