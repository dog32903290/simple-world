// app/frame_cook_contextvar_selftest — context-var YELLOW seam pin (--selftest-contextvar, block #1
// of 柏為's visual-load-bearing-root directive). Drives the REAL production seam
// (framecook::cookStatefulValueNodes — the exact 3-phase clear→writers→readers cook run() drives
// every frame), NOT a mock, through five goldens:
//   A roundtrip      — SetFloatVar("x",3.5) → map → GetFloatVar("x") reads 3.5 (anti-orphan: the
//                      value crosses the seam writer→ContextVarMap→reader→extOut[0]). injectBug
//                      aims the writer at "x_BUG" so the real cook writes the wrong key → the
//                      reader misses the map → reads the 0.0 fallback → FAIL (real defect).
//   B unset default  — GetFloatVar("missing", fallback 9.0) reads 9.0 (TryGetValue miss → fallback).
//   C ordering tooth — a GetFloatVar node declared EARLIER in g.nodes than its SetFloatVar writer
//                      still reads 3.5 (the writer pass runs first). injectBug 1 collapses the 2
//                      passes into one in-order loop → the early Get reads the fallback → FAIL.
//   D per-frame reset— frame1 with the Set present → "x"=3.5; frame2 with the Set node REMOVED →
//                      GetFloatVar("x") reads the fallback (the top-of-frame clear wiped the stale
//                      value). injectBug 2 skips the clear → reads the stale 3.5 → FAIL.
//   E int map        — SetIntVar("n",7) → GetIntVar("n")==7; GetIntVar("unset", fallback 4)==4
//                      (the Int map is independent of the Float map). injectBug aims the Int writer
//                      at "n_BUG" so the real cook writes the wrong int key → the reader misses the
//                      int map → reads the fallback → FAIL (real defect, not a flipped expectation).
//
// The var NAME flows the real string sub-seam: SymbolChild.strOverrides → flatten projection
// (effectiveStrInput) → ResidentNode.strInputs → cookStatefulValueNodes reads strInputs["VariableName"].
// It is NOT smuggled through a float port — a refuter can confirm by grepping for a float carrier.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"                // findSpec (var-op NodeSpecs)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // ContextVarMap / StatefulValueState
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [contextvar] FAIL %s\n", what); }
  else printf("  [contextvar] ok   %s\n", what);
}

// One atomic var-op child. name = the String VariableName override (the string sub-seam);
// floatVal = the FloatValue/Value override; fbVal = the FallbackDefault/FallbackValue override.
SymbolChild makeChild(int id, const char* type, const std::string& name) {
  SymbolChild c;
  c.id = id;
  c.symbolId = type;
  if (!name.empty()) c.strOverrides["VariableName"] = name;  // real string sub-seam channel
  return c;
}

// Register the var-op atomic symbols into the lib (regenerate from the NodeSpec, like the bridge).
void ensureVarSymbols(SymbolLibrary& lib) {
  for (const char* t : {"SetFloatVar", "GetFloatVar", "SetIntVar", "GetIntVar"})
    if (const NodeSpec* s = findSpec(t)) lib.symbols[t] = atomicSymbolFromSpec(*s);
}

// Build + flatten + cook one frame through the REAL seam; returns the cooked graph (read extOut by
// path). `bug` is the teeth hook (0 normal / 1 collapse-passes / 2 skip-clear). `vars`+`state` are
// carried so a multi-frame golden (D) keeps the same map/state across frames.
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
               std::map<std::string, StatefulValueState>& state, uint32_t frame, int bug) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, state, vars, bug);
}

float extOut0(const ResidentEvalGraph& g, const char* path) {
  const ResidentNode* n = g.node(path);
  return n ? n->extOut[0] : -999.0f;
}

}  // namespace

int runContextVarSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] contextvar (context-var YELLOW seam: flat var map + writer-before-reader 2-pass)\n");

  // ===== A roundtrip + B unset default (one graph: Set "x"=3.5, Get "x", Get "missing" fb 9.0). ====
  // Children in declaration order: 1=SetFloatVar("x"), 2=GetFloatVar("x"), 3=GetFloatVar("missing").
  {
    SymbolLibrary lib; ensureVarSymbols(lib);
    Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
    // injectBug A: the writer aims at a DIFFERENT var name than the reader queries (the real defect —
    // a broken seam where writer→map and map→reader rendezvous on mismatched keys). The production
    // SetFloatVar then writes "x_BUG" while GetFloatVar still reads "x" → the value never crosses the
    // seam → the real cook's roundtrip MISSES the map → reads the 0.0 fallback. Expectation stays
    // CORRECT (3.5) → FAIL reflects a real defect, NOT a flipped expectation.
    SymbolChild setX = makeChild(1, "SetFloatVar", injectBug ? "x_BUG" : "x");
    setX.overrides["FloatValue"] = 3.5f;
    SymbolChild getX = makeChild(2, "GetFloatVar", "x");   getX.overrides["FallbackDefault"] = 0.0f;
    SymbolChild getM = makeChild(3, "GetFloatVar", "missing"); getM.overrides["FallbackDefault"] = 9.0f;
    root.children = {setX, getX, getM};
    root.nextChildId = 4;
    lib.symbols["R"] = root; lib.rootId = "R";

    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    // Prove the string sub-seam projected the var name into strInputs (NOT a float hack). The name
    // tracks the (possibly bug-injected) writer key — the projection itself is what's under test here.
    const char* setName = injectBug ? "x_BUG" : "x";
    const ResidentNode* gn = g.node("1");
    expect("string sub-seam: SetFloatVar.strInputs[\"VariableName\"] projected via strOverrides",
           gn && gn->strInputs.count("VariableName") && gn->strInputs.at("VariableName") == setName);

    ContextVarMap vars; std::map<std::string, StatefulValueState> state;
    cookFrame(lib, g, vars, state, 0, /*bug=*/0);

    float roundtrip = extOut0(g, "2");
    // Expectation stays CORRECT (3.5). The bug corrupts the REAL cook (writer aimed at "x_BUG" above)
    // so the real GetFloatVar("x") misses the map and reads its 0.0 fallback → FAIL for a real reason.
    expect("A roundtrip: GetFloatVar(\"x\") == 3.5 (writer→map→reader)", std::fabs(roundtrip - 3.5f) < 1e-5f);
    expect("A echo: SetFloatVar.Output echoes 3.5", std::fabs(extOut0(g, "1") - 3.5f) < 1e-5f);

    float unset = extOut0(g, "3");
    expect("B unset default: GetFloatVar(\"missing\") == 9.0 (TryGetValue miss → fallback)",
           std::fabs(unset - 9.0f) < 1e-5f);
  }

  // ===== C ordering tooth: GetFloatVar declared BEFORE its SetFloatVar writer. ====================
  // Children: 1=GetFloatVar("x") fb -1, 2=SetFloatVar("x")=3.5. In g.nodes the Get is FIRST; the
  // 2-pass still runs the Set (pass 1) before the Get (pass 2), so the Get reads 3.5. injectBug 1
  // collapses to one in-order loop → the Get runs before the Set → reads the -1 fallback → FAIL.
  {
    SymbolLibrary lib; ensureVarSymbols(lib);
    Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
    SymbolChild getX = makeChild(1, "GetFloatVar", "x"); getX.overrides["FallbackDefault"] = -1.0f;
    SymbolChild setX = makeChild(2, "SetFloatVar", "x"); setX.overrides["FloatValue"] = 3.5f;
    root.children = {getX, setX};  // GET first in declaration order → first in g.nodes
    root.nextChildId = 3;
    lib.symbols["R"] = root; lib.rootId = "R";

    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    // Confirm the Get really is earlier in g.nodes than the Set (the premise of the tooth).
    expect("C premise: GetFloatVar(idx) < SetFloatVar(idx) in g.nodes (out-of-order declaration)",
           g.byPath.at("1") < g.byPath.at("2"));

    ContextVarMap vars; std::map<std::string, StatefulValueState> state;
    // injectBug 1 COLLAPSES the 2 passes into one in-order loop (the real defect). Expectation stays
    // CORRECT (3.5) → the broken cook reads the -1 fallback → FAIL. Proves the 2-pass is load-bearing.
    cookFrame(lib, g, vars, state, 0, /*bug=*/injectBug ? 1 : 0);

    float got = extOut0(g, "1");
    expect("C ordering: out-of-order Get reads 3.5 via writer-before-reader 2-pass",
           std::fabs(got - 3.5f) < 1e-5f);
  }

  // ===== D per-frame reset: frame1 Set present → 3.5; frame2 Set REMOVED → fallback (clear wiped). =
  // Same vars+state carried across two frames (the production contract: one map, cleared each frame).
  // injectBug 2 skips the top-of-frame clear → frame2's Get reads the STALE 3.5 → FAIL.
  {
    // frame 1 lib: 1=SetFloatVar("x")=3.5, 2=GetFloatVar("x") fb 7.0.
    SymbolLibrary lib1; ensureVarSymbols(lib1);
    Symbol r1; r1.id = "R"; r1.name = "R"; r1.atomic = false;
    SymbolChild setX = makeChild(1, "SetFloatVar", "x"); setX.overrides["FloatValue"] = 3.5f;
    SymbolChild getX = makeChild(2, "GetFloatVar", "x"); getX.overrides["FallbackDefault"] = 7.0f;
    r1.children = {setX, getX}; r1.nextChildId = 3;
    lib1.symbols["R"] = r1; lib1.rootId = "R";
    ResidentEvalGraph g1 = buildEvalGraph(lib1, lib1.rootId); initResidentCache(g1);

    ContextVarMap vars; std::map<std::string, StatefulValueState> state;
    cookFrame(lib1, g1, vars, state, 0, /*bug=*/injectBug ? 2 : 0);
    expect("D frame1: GetFloatVar(\"x\") == 3.5 (Set present)", std::fabs(extOut0(g1, "2") - 3.5f) < 1e-5f);

    // frame 2 lib: the SetFloatVar node is GONE — only GetFloatVar("x") fb 7.0 remains (child id 2).
    SymbolLibrary lib2; ensureVarSymbols(lib2);
    Symbol r2; r2.id = "R"; r2.name = "R"; r2.atomic = false;
    SymbolChild getX2 = makeChild(2, "GetFloatVar", "x"); getX2.overrides["FallbackDefault"] = 7.0f;
    r2.children = {getX2}; r2.nextChildId = 3;
    lib2.symbols["R"] = r2; lib2.rootId = "R";
    ResidentEvalGraph g2 = buildEvalGraph(lib2, lib2.rootId); initResidentCache(g2);

    // injectBug 2 SKIPS the top-of-frame clear (the real defect). Expectation stays CORRECT (7.0) →
    // the broken cook leaks the stale 3.5 across frames → FAIL. Proves the per-frame clear is needed.
    cookFrame(lib2, g2, vars, state, 1, /*bug=*/injectBug ? 2 : 0);
    float got2 = extOut0(g2, "2");
    expect("D per-frame reset: frame2 Get reads fallback 7.0 (top-of-frame clear wiped stale 3.5)",
           std::fabs(got2 - 7.0f) < 1e-5f);
  }

  // ===== E int map (independent of the Float map): SetIntVar("n",7)→GetIntVar("n")==7; unset→4. ====
  // Also proves the int truncation (Value 7.9 → 7) toward zero, NOT rounding.
  {
    SymbolLibrary lib; ensureVarSymbols(lib);
    Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
    // injectBug E: the Int writer aims at a DIFFERENT key ("n_BUG") than the reader queries ("n") —
    // the real defect where the int value never carries across the seam. The production SetIntVar
    // then writes intVars["n_BUG"] while GetIntVar still reads "n" → the real cook misses the int map
    // → reads its 4-equivalent fallback (FallbackValue 0 here gives 0, see want below) → FAIL for a
    // real reason. Expectation stays CORRECT (7) — NOT a flipped expectation.
    SymbolChild setN = makeChild(1, "SetIntVar", injectBug ? "n_BUG" : "n");
    setN.overrides["Value"] = 7.9f;  // truncate→7
    SymbolChild getN = makeChild(2, "GetIntVar", "n"); getN.overrides["FallbackValue"] = 0.0f;
    SymbolChild getU = makeChild(3, "GetIntVar", "unset"); getU.overrides["FallbackValue"] = 4.0f;
    // A Float var named "n" too — proves the Int map is a SEPARATE namespace (no collision).
    SymbolChild setFN = makeChild(4, "SetFloatVar", "n"); setFN.overrides["FloatValue"] = 99.0f;
    SymbolChild getFN = makeChild(5, "GetFloatVar", "n"); getFN.overrides["FallbackDefault"] = 0.0f;
    root.children = {setN, getN, getU, setFN, getFN}; root.nextChildId = 6;
    lib.symbols["R"] = root; lib.rootId = "R";

    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId); initResidentCache(g);
    ContextVarMap vars; std::map<std::string, StatefulValueState> state;
    cookFrame(lib, g, vars, state, 0, /*bug=*/0);

    float intGet = extOut0(g, "2");
    // Expectation stays CORRECT (7). The bug corrupts the REAL cook (Int writer aimed at "n_BUG"
    // above) so the real GetIntVar("n") misses the int map and reads its fallback → FAIL for a real
    // reason.
    expect("E int map: SetIntVar(\"n\",7.9)→GetIntVar(\"n\")==7 (truncate toward zero)",
           std::fabs(intGet - 7.0f) < 1e-5f);
    expect("E int unset: GetIntVar(\"unset\") == 4 (independent fallback)",
           std::fabs(extOut0(g, "3") - 4.0f) < 1e-5f);
    expect("E namespaces split: Float var \"n\"==99 ≠ Int var \"n\"==7 (separate maps)",
           std::fabs(extOut0(g, "5") - 99.0f) < 1e-5f && std::fabs(intGet - 7.0f) < 1e-5f);
  }

  printf("[selftest] contextvar %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
