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

// Headless RED->GREEN proof of the AddChild cycle gate (addChildWouldCycle). Builds a lib with a
// containment chain A ⊃ B ⊃ C (each compound holds ONE instance of the next) plus an atomic leaf,
// then asserts every self-nest the menu must refuse and every legal add it must allow:
//   - direct self-nest (X into X) -> refuse
//   - transitive self-nest (A into B, A into C) -> refuse (FORK vs TiXL's open-path-only filter)
//   - one-level self-nest (B into C) -> refuse; legal reverse (C into A) -> allow
//   - adding an atomic anywhere -> allow (atomics have no children, can't cycle)
// injectBug BLINDS the predicate (a stand-in that always answers "no cycle"); the refuse legs
// then read the wrong answer and the assertion FAILS — that's the tooth (a blind gate = self-
// nesting compounds silently swallowed by the resident builder, S14).
int runCycleGuardSelfTest(bool injectBug) {
  SymbolLibrary lib;
  // atomic leaf (no children -> never a cycle source)
  Symbol leaf; leaf.id = "Leaf"; leaf.name = "Leaf"; leaf.atomic = true;
  lib.symbols[leaf.id] = leaf;
  // C ⊃ Leaf, B ⊃ C, A ⊃ B — a 3-deep containment chain (all compounds).
  Symbol cc; cc.id = "C"; cc.name = "C"; cc.atomic = false;
  { SymbolChild k; k.id = 1; k.symbolId = "Leaf"; cc.children = {k}; }
  Symbol bb; bb.id = "B"; bb.name = "B"; bb.atomic = false;
  { SymbolChild k; k.id = 1; k.symbolId = "C"; bb.children = {k}; }
  Symbol aa; aa.id = "A"; aa.name = "A"; aa.atomic = false;
  { SymbolChild k; k.id = 1; k.symbolId = "B"; aa.children = {k}; }
  lib.symbols["C"] = cc; lib.symbols["B"] = bb; lib.symbols["A"] = aa;
  lib.rootId = "A";

  // The gate under test — blinded to always-"no cycle" when injectBug (= a broken predicate).
  auto wouldCycle = [&](const std::string& parentId, const std::string& symbolId) {
    if (injectBug) return false;
    return addChildWouldCycle(lib, parentId, symbolId);
  };

  // Refuse legs (must be TRUE; a blinded predicate makes them false -> FAIL). Note "X into Y"
  // means: add an instance of X inside symbol Y. It cycles when Y is inside X's subtree (or X==Y).
  bool selfA   = wouldCycle("A", "A");   // A into A: direct self-nest
  bool selfC   = wouldCycle("C", "C");   // C into C: direct self-nest, leaf-most compound
  bool aIntoB  = wouldCycle("B", "A");   // A into B: A already contains B (1 level)
  bool aIntoC  = wouldCycle("C", "A");   // A into C: A ⊃ B ⊃ C (2 levels — the deep leg)
  bool bIntoC  = wouldCycle("C", "B");   // B into C: B contains C (reverse-adjacent)
  bool refuseOk = selfA && selfC && aIntoB && aIntoC && bIntoC;

  // Allow legs (must be FALSE — these never cycle, regardless of injectBug):
  bool cIntoA   = wouldCycle("A", "C");     // C into A: C's subtree is {Leaf}; adding it under A
                                            // never reaches A -> legal (a 2nd C instance = reuse).
  bool leafIntoA = wouldCycle("A", "Leaf"); // atomic anywhere: no children, can't ever cycle.
  bool allowOk = !cIntoA && !leafIntoA;

  bool pass = refuseOk && allowOk;
  printf("[selftest-cycleguard] refuse(A>A=%d C>C=%d A>B=%d A>C=%d B>C=%d)=%d "
         "allow(C>A=%d Leaf>A=%d)=%d%s -> %s\n",
         selfA, selfC, aIntoB, aIntoC, bIntoC, refuseOk, !cIntoA, !leafIntoA, allowOk,
         injectBug ? "(bugged)" : "", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
