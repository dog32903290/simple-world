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

#include <cmath>
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

  // Particle-force lane: the new force types serialize via the same atomicUuidTable path as
  // TurbulenceForce (their params ride the generic child `overrides` map, already round-trip
  // proven above). The load-bearing new state is the UUID<->type mapping; assert it round-trips
  // for both new types (a rename of the C++ string must keep old files loading). distinct = no
  // accidental UUID collision with each other / TurbulenceForce.
  bool forceUuidOk =
      typeForAtomicUuid(atomicUuidForType("DirectionalForce")) == "DirectionalForce" &&
      typeForAtomicUuid(atomicUuidForType("VectorFieldForce")) == "VectorFieldForce" &&
      atomicUuidForType("DirectionalForce") != atomicUuidForType("VectorFieldForce") &&
      atomicUuidForType("DirectionalForce") != atomicUuidForType("TurbulenceForce");
  // The DirectionalForce child's edited params (Amount + a Vec component) survived the round-
  // trip — the force param serialization the lane delivers. children[3] is the df child above.
  // Force param round-trip — a standalone mini-lib (kept off the shared `root` lib so the
  // tolerance test's token-position assumptions stay intact). A DirectionalForce child with an
  // edited Amount + a Vec component (Direction.y) must survive save->reload via the generic
  // `overrides` map (the force param spine). On reload the atomic UUID resolves back to the type
  // string, so the symbolId is "DirectionalForce".
  bool forceParamOk = false;
  {
    SymbolLibrary fl;
    fl.symbols["DirectionalForce"] = atomicSymbolFromSpec(*findSpec("DirectionalForce"));
    Symbol fr; fr.id = "FRoot"; fr.name = "FRoot"; fr.atomic = false;
    SymbolChild df; df.id = 1; df.symbolId = "DirectionalForce";
    df.overrides["Amount"] = 0.42f; df.overrides["Direction.y"] = -0.5f;
    fr.children = {df};
    fl.symbols[fr.id] = fr; fl.rootId = "FRoot";
    SymbolLibrary fb; std::vector<std::string> fw;
    if (libFromJsonAny(libToJsonV2(fl), fb, &fw) && fb.find("FRoot") &&
        fb.symbols["FRoot"].children.size() == 1) {
      const SymbolChild& rc = fb.symbols["FRoot"].children[0];
      forceParamOk = rc.symbolId == "DirectionalForce" &&
                     rc.overrides.count("Amount") && rc.overrides.at("Amount") == 0.42f &&
                     rc.overrides.count("Direction.y") && rc.overrides.at("Direction.y") == -0.5f;
    }
  }

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

  // --- 5 (refuter-savev2 promoted repros) ---
  // 5a. NaN/inf override: the WRITER must clamp (a bare `nan` token is invalid JSON and
  // would make a file the app wrote unreadable — whole-load failure, S15 violation).
  SymbolLibrary nanLib = lib;
  nanLib.symbols["Root"].children[0].overrides["value"] = std::nanf("");
  SymbolLibrary nanBack;
  bool nanOk = libFromJsonAny(libToJsonV2(nanLib), nanBack, nullptr) &&
               nanBack.symbols["Root"].children[0].overrides.at("value") == 0.0f;
  // 5b. flat inverse with NON-standard conn ids: ids NORMALIZE (v2 stores no conn ids);
  // topology/params must survive exactly. (The old byte-equal golden was self-fulfilling —
  // defaultParticleGraph's ids happen to equal the regenerator's output.)
  Graph odd;
  Node oa; oa.id = 1; oa.type = "Const"; oa.params["value"] = 2.0f; odd.nodes.push_back(oa);
  Node ob; ob.id = 7; ob.type = "Multiply"; odd.nodes.push_back(ob);
  odd.connections.push_back({999, pinId(1, 1), pinId(7, 0)});  // Const.out -> Multiply.a, odd id
  odd.nextId = 1000;
  Graph oddBack;
  bool oddOk = graphFromLib(libFromGraph(odd), oddBack) && oddBack.nodes.size() == 2 &&
               oddBack.connections.size() == 1 &&
               oddBack.connections[0].fromPin == pinId(1, 1) &&
               oddBack.connections[0].toPin == pinId(7, 0) &&
               oddBack.node(1)->params.at("value") == 2.0f &&
               oddBack.connections[0].id != 999;  // normalized — pinned as EXPECTED behavior
  // 5c. a compound whose id sits in the atomic fallback namespace must NOT be hijacked
  // into an atomic reference (loader resolves compounds first).
  SymbolLibrary ns;
  Symbol nsc; nsc.id = "sw-type:Const"; nsc.name = "UserCompound"; nsc.atomic = false;
  nsc.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild inner; inner.id = 1; inner.symbolId = "Const"; inner.overrides["value"] = 8.0f;
  nsc.children = {inner};
  nsc.connections = {{1, "out", kSymbolBoundary, "out"}};
  ns.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
  ns.symbols[nsc.id] = nsc;
  Symbol nsRoot; nsRoot.id = "Root"; nsRoot.name = "Root"; nsRoot.atomic = false;
  SymbolChild nsu; nsu.id = 1; nsu.symbolId = "sw-type:Const";
  nsRoot.children = {nsu};
  ns.symbols["Root"] = nsRoot; ns.rootId = "Root";
  SymbolLibrary nsBack;
  bool nsOk = libFromJsonAny(libToJsonV2(ns), nsBack, nullptr) &&
              nsBack.find("Root") && nsBack.symbols["Root"].children.size() == 1 &&
              nsBack.symbols["Root"].children[0].symbolId == "sw-type:Const" &&
              nsBack.find("sw-type:Const") && !nsBack.symbols["sw-type:Const"].atomic;

  // 5d. CJK survives the roundtrip (批次 4 prerequisite: 柏為 WILL name a compound in
  // Chinese). The writer emits raw UTF-8; the PARSER needed the sw-patch(utf8) in vendored
  // crude_json (signed-char peek + the c<128 assert killed any non-ASCII at read time).
  // Both legs: raw UTF-8 bytes roundtrip byte-stable, and hand-written \uXXXX escapes
  // decode to the same UTF-8 string.
  SymbolLibrary cjk;
  Symbol cjkSym; cjkSym.id = "Compound-1"; cjkSym.name = "粒子發射器"; cjkSym.atomic = false;
  cjkSym.outputDefs = {{"out", "out", "Float", 0.0f}};
  cjk.symbols[cjkSym.id] = cjkSym;
  Symbol cjkRoot; cjkRoot.id = "Root"; cjkRoot.name = "Root";
  SymbolChild cjkChild; cjkChild.id = 1; cjkChild.symbolId = "Compound-1";
  cjkRoot.children = {cjkChild};
  cjk.symbols["Root"] = cjkRoot; cjk.rootId = "Root";
  const std::string cjkJson = libToJsonV2(cjk);
  SymbolLibrary cjkBack;
  bool cjkOk = libFromJsonAny(cjkJson, cjkBack, nullptr) &&
               cjkBack.find("Compound-1") &&
               cjkBack.symbols["Compound-1"].name == "粒子發射器" &&
               libToJsonV2(cjkBack) == cjkJson;  // byte-stable through the patched parser
  {
    SymbolLibrary esc;
    bool escLoaded = libFromJsonAny(
        "{\"formatVersion\": 2, \"rootSymbolId\": \"R\", \"symbols\": ["
        "{\"id\": \"R\", \"name\": \"\\u4e2d\\u6587\"}]}", esc, nullptr);
    cjkOk = cjkOk && escLoaded && esc.find("R") && esc.symbols["R"].name == "中文";
    // surrogate PAIR (how standard JSON tools escape non-BMP, e.g. python json.dumps):
    // must decode to 4-byte UTF-8, not kill the file (refuter 批次4 #2).
    SymbolLibrary emoji;
    bool emojiLoaded = libFromJsonAny(
        "{\"formatVersion\": 2, \"rootSymbolId\": \"R\", \"symbols\": ["
        "{\"id\": \"R\", \"name\": \"\\ud83d\\ude00\"}]}", emoji, nullptr);
    cjkOk = cjkOk && emojiLoaded && emoji.find("R") && emoji.symbols["R"].name == "😀";
  }

  bool pass = loadOk && byteStable && evalOk && reuseOk && atomicOk && forceUuidOk &&
              forceParamOk && migOk && tolOk && nanOk && oddOk && nsOk && cjkOk;
  printf("[selftest-savev2] roundtrip(byte=%d eval %.0f==%.0f)=%d reuse=%d atomicRegen=%d "
         "forceUuid=%d forceParam=%d legacyMig(%zu ch/%zu wires)=%d tolerance(drop child+wire, %zu warns)=%d "
         "nanClamp=%d oddIdSemantic=%d nsNoHijack=%d cjk=%d -> %s\n",
         byteStable ? 1 : 0, v0, v1, (loadOk && evalOk) ? 1 : 0, reuseOk ? 1 : 0,
         atomicOk ? 1 : 0, forceUuidOk ? 1 : 0, forceParamOk ? 1 : 0, mroot ? mroot->children.size() : 0,
         mroot ? mroot->connections.size() : 0, migOk ? 1 : 0, warn3.size(), tolOk ? 1 : 0,
         nanOk ? 1 : 0, oddOk ? 1 : 0, nsOk ? 1 : 0, cjkOk ? 1 : 0, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
