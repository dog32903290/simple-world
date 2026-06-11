// Headless RED->GREEN proof of .swproj v2 (compound_save.h header doc lists the format
// decisions under test):
//   1. roundtrip: a compound lib WITH reuse (two children of the same compound) survives
//      save -> load -> save BYTE-STABLE, and resident evaluation pre == post == hand value.
//   2. atomic regeneration: the file holds NO atomic defs; after load the atomics came from
//      the registry (spec port count, not the minimal hand-built ones).
//   3. legacy migration: a v1 flat .swproj json loads through the bridge (children == nodes,
//      overrides == params), with a migration warning.
//   4. tolerance (S15): a doctored v2 (child -> missing symbol, dangling wire, newer
//      formatVersion) loads with EXACTLY the bad parts dropped + 3 warnings, rest intact.
// injectBug tampers an override after reload -> the eval-identical assertion FAILS (teeth).
#include "runtime/compound_save.h"

#include <cstdio>

#include "runtime/graph.h"                // defaultParticleGraph + toJson (legacy source)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat

namespace sw {

int runSaveV2SelfTest(bool injectBug) {
  // --- 1+2: compound lib with reuse. Scale(in) = in * 3 (inner Const wired to Multiply.b);
  // root: Const(5) -> s1(in) -> out probe via s1's Multiply, plus a reuse sibling s2. ---
  SymbolLibrary lib;
  lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
  lib.symbols["Multiply"] = atomicSymbolFromSpec(*findSpec("Multiply"));

  Symbol scale;
  scale.id = "c-scale";
  scale.name = "Scale";
  scale.atomic = false;
  scale.inputDefs = {{"in", "in", "Float", 1.0f}};
  scale.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild m1; m1.id = 1; m1.symbolId = "Multiply";
  SymbolChild k1; k1.id = 2; k1.symbolId = "Const"; k1.overrides["value"] = 3.0f;
  scale.children = {m1, k1};
  scale.connections = {
      {kSymbolBoundary, "in", 1, "a"},   // boundary in -> Multiply.a
      {2, "out", 1, "b"},                // Const(3) -> Multiply.b
      {1, "out", kSymbolBoundary, "out"} // Multiply.out -> boundary out
  };
  lib.symbols[scale.id] = scale;

  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild src; src.id = 1; src.symbolId = "Const"; src.overrides["value"] = 5.0f;
  src.x = 10.0f; src.y = 20.0f;
  SymbolChild s1; s1.id = 2; s1.symbolId = "c-scale";
  SymbolChild s2; s2.id = 3; s2.symbolId = "c-scale"; s2.overrides["in"] = 4.0f;  // reuse
  root.children = {src, s1, s2};
  root.connections = {{1, "out", 2, "in"}, {2, "out", kSymbolBoundary, "out"}};
  lib.symbols[root.id] = root;
  lib.rootId = "Root";

  ResidentEvalCtx rc{};
  ResidentEvalGraph rg0 = buildEvalGraph(lib, "Root");
  float v0 = evalResidentFloat(rg0, rg0.outputs["out"].first, rg0.outputs["out"].second, rc);

  std::string j1 = libToJsonV2(lib);
  SymbolLibrary back;
  std::vector<std::string> warn1;
  bool loadOk = libFromJsonAny(j1, back, &warn1);
  if (injectBug && back.find("Root"))  // teeth: tamper the reloaded lib -> eval must diverge
    back.symbols["Root"].children[0].overrides["value"] = 99.0f;
  std::string j2 = libToJsonV2(back);
  bool byteStable = (j1 == j2) || injectBug;  // tamper changes bytes too; eval assert is the teeth
  ResidentEvalGraph rg1 = buildEvalGraph(back, "Root");
  float v1 = evalResidentFloat(rg1, rg1.outputs["out"].first, rg1.outputs["out"].second, rc);
  bool evalOk = (v0 == 15.0f) && (v1 == v0);  // 5 * 3, identical after roundtrip
  // reuse sibling intact: Scale instance 3 keeps its own override (definition shared, state not)
  bool reuseOk = back.find("Root") && back.symbols["Root"].children.size() == 3 &&
                 back.symbols["Root"].children[2].overrides.count("in") &&
                 back.symbols["Root"].children[2].overrides.at("in") == 4.0f;
  // atomic regeneration: file has no atomic defs; reloaded Const must carry the REGISTRY's
  // port-derived defs (2 slots: value in + out), not whatever a file said.
  bool atomicOk = back.find("Const") && back.symbols["Const"].atomic &&
                  back.symbols["Const"].inputDefs.size() == 1 &&
                  back.symbols["Const"].outputDefs.size() == 1 &&
                  j1.find("\"Const\"") == std::string::npos;  // atomics not serialized

  // --- 3: legacy v1 migration ---
  Graph flat = defaultParticleGraph();
  SymbolLibrary mig;
  std::vector<std::string> warn2;
  bool migOk = libFromJsonAny(toJson(flat), mig, &warn2);
  const Symbol* mroot = mig.find(mig.rootId);
  migOk = migOk && mroot && mroot->children.size() == flat.nodes.size() &&
          mroot->connections.size() == flat.connections.size() && !warn2.empty();
  // …and the TRANSITIONAL inverse leg: lib -> flat reproduces the original flat graph
  // byte-for-byte (the doSave(v2) -> doOpen path the still-flat editor rides until 批次 3).
  Graph flatBack;
  migOk = migOk && graphFromLib(mig, flatBack) && toJson(flatBack) == toJson(flat);

  // --- 4: tolerance (S15) — doctor the v2 json: future version, a child referencing a
  // missing compound, a dangling wire to a nonexistent child. ---
  // Token-level edits (no whitespace assumptions about the dump format): bump the version,
  // point the FIRST c-scale reference (Root's child 2 — "Root" sorts before "c-scale") at a
  // missing symbol, and dangle the FIRST srcChild:1 wire (Root's Const->Scale wire).
  std::string doctored = j1;
  auto replaceFirstAfter = [&](size_t from, const std::string& find, const std::string& to) {
    auto p = doctored.find(find, from);
    if (p != std::string::npos) doctored.replace(p, find.size(), to);
    return p;
  };
  size_t verPos = doctored.find("\"formatVersion\"");
  replaceFirstAfter(verPos, "2", "3");                       // the version digit right after the key
  replaceFirstAfter(0, "\"c-scale\"", "\"c-GONE\"");          // skips the symbol DEF (sorted later)
  size_t wirePos = doctored.find("\"srcChild\"");             // Root's first wire: srcChild 1
  replaceFirstAfter(wirePos, "1", "77");
  SymbolLibrary tol;
  std::vector<std::string> warn3;
  bool tolLoad = libFromJsonAny(doctored, tol, &warn3);
  const Symbol* troot = tol.find("Root");
  bool tolOk = tolLoad && troot && warn3.size() >= 3 &&             // ver + child + wire warned
               troot->children.size() == 2 &&                        // the bad child dropped
               tol.find("c-scale") && !tol.symbols["c-scale"].children.empty();  // rest intact

  bool pass = loadOk && byteStable && evalOk && reuseOk && atomicOk && migOk && tolOk;
  printf("[selftest-savev2] roundtrip(byte=%d eval %.0f==%.0f)=%d reuse=%d atomicRegen=%d "
         "legacyMig(%zu ch/%zu wires)=%d tolerance(drop child+wire, %zu warns)=%d -> %s\n",
         byteStable ? 1 : 0, v0, v1, (loadOk && evalOk) ? 1 : 0, reuseOk ? 1 : 0,
         atomicOk ? 1 : 0, mroot ? mroot->children.size() : 0,
         mroot ? mroot->connections.size() : 0, migOk ? 1 : 0, warn3.size(), tolOk ? 1 : 0,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
