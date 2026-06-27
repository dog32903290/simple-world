// stringctxvar_golden — --selftest-stringctxvar. The String context-variable seam (sub-seam C) pin:
// SetStringVar + GetStringVar on the typed stringVars channel, driven through the PRODUCTION resident
// String cook (cookStringNodes' writer-first 2-pass) + the PRODUCTION per-frame clear
// (framecook::cookStatefulValueNodes pass-0, the SAME clear run() drives every frame). The string twin of
// frame_cook_contextvar_selftest.cpp (float/int/vec3 channel), but on the String currency: GetStringVar
// emits a Slot<string>, so the value crosses writer→ContextVarMap.stringVars→reader→extStrOut[0] (NOT extOut).
//
// Goldens (every expected value hand-derived from the TiXL .cs/.t3 sources, NOT self-consistent):
//   A roundtrip    — SetStringVar("k","hello") → GetStringVar("k") reads "hello" (writer→stringVars→reader).
//                    injectBug aims the writer at "k_BUG" so the REAL cook writes the wrong key → the reader
//                    misses the map → reads the "" fallback → FAIL (a real seam defect, not a flipped expect).
//   B unset default— GetStringVar("missing", fallback "fb") reads "fb" (TryGetValue miss → FallbackDefault).
//   C ordering     — a GetStringVar declared BEFORE its SetStringVar writer in g.nodes STILL reads the set
//                    value (the writer-first 2-pass runs Set before Get). injectBug flips stringCtxVarOrderBug()
//                    → cookStringNodes collapses to one build-order loop → the early Get reads "" → FAIL.
//   H per-frame    — frame1 Set present → "k"="hello"; frame2 with the Set node REMOVED → GetStringVar("k")
//     reset (the     reads the fallback (the top-of-frame stringVars clear wiped the stale value). The clear
//     vec3-clear     is the PRODUCTION framecook::cookStatefulValueNodes pass-0 (the EXACT site that gained
//     bug, on the    `vars.stringVars.clear()`). injectBug uses ctxVarBug=2 (skip clear) → frame2 leaks the
//     string chan)   stale "hello" → FAIL (parity with the float-channel D golden + the vec3 G golden).
#include "app/frame_cook.h"  // framecook::cookStatefulValueNodes (the PRODUCTION per-frame clear)

#include <cstdio>
#include <map>
#include <string>

#include "runtime/compound_graph.h"       // SymbolLibrary
#include "runtime/graph.h"                // Graph / Node / Connection / pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary, paths == ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / cookStringNodes
#include "runtime/stateful_value_ops.h"   // ContextVarMap / StatefulValueState
#include "runtime/string_op_registry.h"   // stringInjectBug / stringCtxVarOrderBug
#include "runtime/transport.h"            // Transport

namespace sw {
namespace {

// Read a String op's cooked host string off extStrOut[Result port idx 0] (the production channel a
// downstream resident String consumer reads). "" on a structural miss.
std::string extStr0(const ResidentEvalGraph& g, const char* path) {
  const ResidentNode* n = g.node(path);
  if (!n) return {};
  auto it = n->extStrOut.find(0);
  return it != n->extStrOut.end() ? it->second : std::string{};
}

// Build the resident graph for a flat Graph (paths == flat node ids as strings), with the resident cache
// initialised — the SAME pipeline the production resident path uses.
ResidentEvalGraph buildResident(const Graph& g, SymbolLibrary& outLib) {
  outLib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(outLib, "Root");
  initResidentCache(rg);
  return rg;
}

// Drive ONE production frame: pass-0 CLEAR (framecook::cookStatefulValueNodes — clears EVERY ctx-var channel
// incl. stringVars; ctxVarBug=2 skips it), then cookStringNodes (the writer-first 2-pass that runs SetStringVar
// then GetStringVar). `vars`/`svState` carry across frames (the production contract: one map, cleared each frame).
void cookFrame(SymbolLibrary& lib, ResidentEvalGraph& g, ContextVarMap& vars,
               std::map<std::string, StatefulValueState>& svState, uint32_t frame, int ctxVarBug) {
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  // The production clear: cookStatefulValueNodes pass-0 wipes vars.stringVars (no stateful FLOAT op present →
  // it only does the clear here). ctxVarBug=2 skips the clear (the H -bug leg). The String ops are cooked next.
  framecook::cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, frame, &lib, svState, vars, ctxVarBug);
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = frame; rc.lib = &lib;
  cookStringNodes(g, rc, /*state=*/nullptr, &vars);  // PRODUCTION String cook: writer-first 2-pass + stringVars
}

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; std::printf("  [stringctxvar] FAIL %s\n", what); }
  else std::printf("  [stringctxvar] ok   %s\n", what);
}

// One atomic SetStringVar/GetStringVar node. VariableName / StringValue / FallbackDefault ride strParams
// (the String sub-seam → libFromGraph projects them into resident strInputs → cookResidentString gathers them).
Node makeNode(int id, const char* type) { Node n; n.id = id; n.type = type; return n; }

}  // namespace

int runStringCtxVarSelfTest(bool injectBug) {
  g_fail = 0;
  std::printf("[selftest] stringctxvar (String ctx-var seam sub-seam C: typed stringVars + writer-first 2-pass)\n");

  // ===== A roundtrip + B unset default (one graph). Children: 1=SetStringVar("k","hello"), =============
  //   2=GetStringVar("k", fb ""), 3=GetStringVar("missing", fb "fb").
  {
    Graph g;
    Node sk = makeNode(1, "SetStringVar");
    sk.strParams["VariableName"] = injectBug ? "k_BUG" : "k";  // injectBug → writer aims at the wrong key
    sk.strParams["StringValue"] = "hello";
    g.nodes.push_back(sk);
    Node gk = makeNode(2, "GetStringVar");
    gk.strParams["VariableName"] = "k"; gk.strParams["FallbackDefault"] = "";
    g.nodes.push_back(gk);
    Node gm = makeNode(3, "GetStringVar");
    gm.strParams["VariableName"] = "missing"; gm.strParams["FallbackDefault"] = "fb";
    g.nodes.push_back(gm);
    g.nextId = 4;

    SymbolLibrary lib; ResidentEvalGraph rg = buildResident(g, lib);
    ContextVarMap vars; std::map<std::string, StatefulValueState> sv;
    cookFrame(lib, rg, vars, sv, 0, /*ctxVarBug=*/0);

    // A: GetStringVar("k") == "hello". injectBug wrote "k_BUG" → the real reader misses → "" ≠ "hello" → FAIL.
    expect("A roundtrip: GetStringVar(\"k\")==\"hello\" (writer→stringVars→reader→extStrOut[0])",
           extStr0(rg, "2") == "hello");
    // echo: SetStringVar.Output echoes the written value (NAMED FORK — golden probe).
    expect("A echo: SetStringVar.Output echoes \"hello\"", extStr0(rg, "1") == "hello");
    // B: unset → FallbackDefault "fb" (independent of injectBug — "missing" is never written).
    expect("B unset default: GetStringVar(\"missing\")==\"fb\" (TryGetValue miss → FallbackDefault)",
           extStr0(rg, "3") == "fb");
  }

  // ===== C ordering: GetStringVar declared BEFORE its SetStringVar writer. =============================
  // Children: 1=GetStringVar("k", fb "early"), 2=SetStringVar("k","late"). The Get is FIRST in g.nodes; the
  // writer-first 2-pass still runs the Set (pass 1) before the Get (pass 2) → the Get reads "late". injectBug
  // flips stringCtxVarOrderBug() → cookStringNodes collapses to one build-order loop → the Get runs first →
  // reads its "early" fallback → FAIL (proves the 2-pass is load-bearing).
  {
    Graph g;
    Node gk = makeNode(1, "GetStringVar");
    gk.strParams["VariableName"] = "k"; gk.strParams["FallbackDefault"] = "early";
    g.nodes.push_back(gk);
    Node sk = makeNode(2, "SetStringVar");
    sk.strParams["VariableName"] = "k"; sk.strParams["StringValue"] = "late";
    g.nodes.push_back(sk);
    g.nextId = 3;

    SymbolLibrary lib; ResidentEvalGraph rg = buildResident(g, lib);
    // Premise: the Get really is earlier in g.nodes than the Set.
    expect("C premise: GetStringVar(idx) < SetStringVar(idx) in g.nodes (out-of-order declaration)",
           rg.byPath.at("1") < rg.byPath.at("2"));

    ContextVarMap vars; std::map<std::string, StatefulValueState> sv;
    stringCtxVarOrderBug() = injectBug;  // ★bug: collapse the 2-pass → the early Get reads its fallback
    cookFrame(lib, rg, vars, sv, 0, /*ctxVarBug=*/0);
    stringCtxVarOrderBug() = false;

    // Expectation stays CORRECT ("late") → the collapsed cook reads "early" → FAIL for a real reason.
    expect("C ordering: out-of-order Get reads \"late\" via writer-before-reader 2-pass",
           extStr0(rg, "1") == "late");
  }

  // ===== H per-frame reset (the vec3-clear-bug regression test, on the STRING channel). ================
  // frame1: 1=SetStringVar("k","hello"), 2=GetStringVar("k", fb "miss") → "hello".
  // frame2: the Set node is GONE — only GetStringVar("k", fb "miss") remains → must read "miss" (the
  // top-of-frame clear wiped the stale "hello"). Same vars+svState across frames (production contract).
  // injectBug uses ctxVarBug=2 → the production clear is SKIPPED → frame2 leaks the stale "hello" → FAIL.
  {
    // frame 1.
    Graph g1;
    Node sk = makeNode(1, "SetStringVar");
    sk.strParams["VariableName"] = "k"; sk.strParams["StringValue"] = "hello";
    g1.nodes.push_back(sk);
    Node gk = makeNode(2, "GetStringVar");
    gk.strParams["VariableName"] = "k"; gk.strParams["FallbackDefault"] = "miss";
    g1.nodes.push_back(gk);
    g1.nextId = 3;
    SymbolLibrary lib1; ResidentEvalGraph rg1 = buildResident(g1, lib1);

    ContextVarMap vars; std::map<std::string, StatefulValueState> sv;
    cookFrame(lib1, rg1, vars, sv, 0, /*ctxVarBug=*/injectBug ? 2 : 0);
    expect("H frame1: GetStringVar(\"k\")==\"hello\" (Set present)", extStr0(rg1, "2") == "hello");

    // frame 2: the SetStringVar is REMOVED — only GetStringVar("k", fb "miss") remains (child id 2).
    Graph g2;
    Node gk2 = makeNode(2, "GetStringVar");
    gk2.strParams["VariableName"] = "k"; gk2.strParams["FallbackDefault"] = "miss";
    g2.nodes.push_back(gk2);
    g2.nextId = 3;
    SymbolLibrary lib2; ResidentEvalGraph rg2 = buildResident(g2, lib2);

    cookFrame(lib2, rg2, vars, sv, 1, /*ctxVarBug=*/injectBug ? 2 : 0);
    // Expectation stays CORRECT ("miss") → if the clear is skipped, frame2 leaks "hello" → FAIL.
    expect("H per-frame reset: frame2 Get reads fallback \"miss\" (top-of-frame clear wiped stale \"hello\")",
           extStr0(rg2, "2") == "miss");
  }

  std::printf("[selftest] stringctxvar %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  // injectBug MUST trip at least one assertion (the seam is actually load-bearing).
  if (injectBug && g_fail == 0) {
    std::printf("[selftest] stringctxvar FAIL: injectBug still passed (the seam is not actually "
                "writing/ordering/clearing the var)\n");
    return 1;
  }
  return g_fail ? 1 : 0;
}

}  // namespace sw
