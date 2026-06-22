// string_rail_golden_subseama — Sub-seam A goldens: LEG 35 (FloatListToString, the FloatList-into-string
// BRIDGE) + LEG 36 (JoinStringList, the StringList currency + the genuinely-WIRED SplitString producer)
// + LEG 34 (FilePathParts MULTI-OUTPUT, EXTRACTED from string_rail_golden.cpp so that file stays
// at-or-below its line-count cap when the main golden gained the runStringRailSubseamA call). All three
// run under --selftest-stringrail (the main golden ANDs runStringRailSubseamA into its result).
//
// Each leg proves BOTH the FLAT path (PointGraph::cook → debugCooked*) and the RESIDENT path (the
// PRODUCTION leg: libFromGraph → buildEvalGraph → cookStringNodes → read extStrOut), with a genuinely
// WIRED list producer feeding the list input (R-2 rule: not a const stand-in). RED bites BOTH legs via
// the per-op inject hooks corrupting the REAL cook output (not an inverted expected value).
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (resident leg)
#include "runtime/eval_context.h"          // EvaluationContext
#include "runtime/graph.h"                 // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"           // PointGraph::cook + debugCooked*
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / cookStringNodes / ResidentEvalGraph
#include "runtime/string_op_registry.h"   // stringInjectBug
#include "runtime/stringlist_op_registry.h"  // stringListInjectBug

namespace sw {

namespace {

// ---- LEG 35: FloatListToString (the FloatList-into-string bridge) ----
// FLAT: FloatsToList(2,4,8) → FloatListToString(Format "F1", Separator "-").  Each value → "2.0"/"4.0"/
// "8.0" (F1 = 1 fixed digit), trailing separator after EACH (fork-floatlist-trailing-separator) →
// "2.0-4.0-8.0-".  RESIDENT mirrors the SAME wired graph (FloatsToList is a genuine FloatList producer →
// the bridge crosses the resident FloatList currency into the string cook). RED: stringInjectBug drops
// the FloatListToString output's last char on BOTH legs (and on resident, the upstream too — but the
// String op's own teeth already bite).
bool legFloatListToString(PointGraph& pg, bool injectBug, bool& ok) {
  // Shared graph: FloatsToList id=2 (Input multiInput) → FloatListToString id=1 (Value = FloatList input).
  // FloatListToString ports: [0]=Output(out), [1]=Value(FloatList in), [2]=Format(String), [3]=Separator.
  auto makeGraph = []() {
    Graph g;
    Node fls; fls.id = 1; fls.type = "FloatListToString";
    fls.strParams["Format"] = "F1";     // unwired String const → "F1"
    fls.strParams["Separator"] = "-";   // unwired String const → "-"
    g.nodes.push_back(fls);
    Node src; src.id = 2; src.type = "FloatsToList";  // genuine FloatList producer
    g.nodes.push_back(src);
    // FloatsToList.Input is a scalar Float MultiInput (port 0). Wire three Const(2,4,8) producers — the
    // SAME genuine scalar-source shape the FloatList golden uses (Const port 0 = "value" input, port 1 =
    // "out" Float output; wire its OUT into FloatsToList.Input, in value order = wire-declaration order).
    int connId = 100, nid = 3;
    for (float v : {2.0f, 4.0f, 8.0f}) {
      Node c; c.id = nid; c.type = "Const"; c.params["value"] = v; g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*Const out port*/ 1), pinId(2, /*FloatsToList.Input port 0*/ 0)});
      ++nid;
    }
    // FloatsToList.out (port 1) → FloatListToString.Value (port 1).
    g.connections.push_back({200, pinId(2, /*FloatsToList.out port 1*/ 1), pinId(1, /*Value port 1*/ 1)});
    g.nextId = nid;
    return g;
  };

  // --- FLAT leg ---
  stringInjectBug() = injectBug;
  std::string flat;
  {
    Graph g = makeGraph();
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    const std::string* o = pg.debugCookedString(1);
    flat = o ? *o : std::string{};
  }
  stringInjectBug() = false;

  // --- RESIDENT leg (production path) ---
  stringInjectBug() = injectBug;
  std::string res;
  {
    Graph g = makeGraph();
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
    cookStringNodes(rg, rc);  // PRODUCTION: gathers FloatsToList via the FloatList currency → bridge → String
    const ResidentNode* n = rg.node("1");
    if (n) { auto it = n->extStrOut.find(0); res = it != n->extStrOut.end() ? it->second : std::string{}; }
  }
  stringInjectBug() = false;

  // Assert the CORRECT value on BOTH legs (incumbent convention): green → both match → pass; -bug → the
  // REAL cook output is corrupted (last char dropped) → flat/res != want → FAIL → the tooth bites both.
  const std::string want = "2.0-4.0-8.0-";  // F1 each + trailing '-' after each value
  bool pass = (flat == want && res == want);
  ok = ok && pass;
  std::printf("[selftest-stringrail] LEG35 FloatListToString FLAT=\"%s\" RESIDENT=\"%s\" want=\"%s\" -> %s\n",
              flat.c_str(), res.c_str(), want.c_str(), pass ? "PASS" : "FAIL");
  return pass;
}

// ---- LEG 36: JoinStringList (StringList currency + wired SplitString producer) ----
// FLAT: SplitString(String "a,b,c", Split ",") → ["a","b","c"] → JoinStringList(Separator "-") → "a-b-c".
// The SplitString→JoinStringList wire is a GENUINE StringList wire (the new currency). The join honours
// LIST ORDER (a-b-c, not sorted) — the wire-order assertion. RESIDENT mirrors the SAME wired graph (the
// resident StringList gather crosses cookResidentStringList inline from JoinStringList's cook). RED:
// stringListInjectBug drops SplitString's last fragment → list ["a","b"] → join "a-b" ≠ "a-b-c"
// (the StringList producer's REAL output is corrupted, biting both legs); stringInjectBug also bites the
// JoinStringList output directly.
bool legJoinStringList(PointGraph& pg, bool injectBug, bool& ok) {
  // Shared graph: SplitString id=2 (String input + Split) → JoinStringList id=1 (Input = StringList in).
  // JoinStringList ports: [0]=Result(out), [1]=Input(StringList multiInput), [2]=Separator.
  // SplitString ports: [0]=Fragments(StringList out), [1]=String(String in), [2]=Split(String in).
  auto makeGraph = []() {
    Graph g;
    Node jn; jn.id = 1; jn.type = "JoinStringList";
    jn.strParams["Separator"] = "-";  // unwired String const → "-"
    g.nodes.push_back(jn);
    Node sp; sp.id = 2; sp.type = "SplitString";
    sp.strParams["String"] = "a,b,c";  // unwired String const → the text to split
    sp.strParams["Split"] = ",";       // unwired String const → split on comma
    g.nodes.push_back(sp);
    // SplitString.Fragments (port 0, StringList) → JoinStringList.Input (port 1, StringList).
    g.connections.push_back({100, pinId(2, /*Fragments port 0*/ 0), pinId(1, /*Input port 1*/ 1)});
    g.nextId = 3;
    return g;
  };

  // --- FLAT leg ---
  stringInjectBug() = injectBug;
  stringListInjectBug() = injectBug;
  std::string flat;
  {
    Graph g = makeGraph();
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    const std::string* o = pg.debugCookedString(1);
    flat = o ? *o : std::string{};
  }
  stringInjectBug() = false;
  stringListInjectBug() = false;

  // --- RESIDENT leg (production path) ---
  stringInjectBug() = injectBug;
  stringListInjectBug() = injectBug;
  std::string res;
  {
    Graph g = makeGraph();
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
    cookStringNodes(rg, rc);  // PRODUCTION: JoinStringList gathers the StringList wire via cookResidentStringList
    const ResidentNode* n = rg.node("1");
    if (n) { auto it = n->extStrOut.find(0); res = it != n->extStrOut.end() ? it->second : std::string{}; }
  }
  stringInjectBug() = false;
  stringListInjectBug() = false;

  // Assert the CORRECT value on BOTH legs (incumbent convention): green → both match → pass; -bug → the
  // REAL cook output is corrupted (SplitString drops a fragment / JoinStringList drops a char) → FAIL →
  // the tooth bites both. The wire-ORDER assertion: "a-b-c" (list order), NOT a sorted re-ordering.
  const std::string want = "a-b-c";  // join ["a","b","c"] with "-", IN ORDER (not sorted)
  bool pass = (flat == want && res == want);
  ok = ok && pass;
  std::printf("[selftest-stringrail] LEG36 JoinStringList FLAT=\"%s\" RESIDENT=\"%s\" want=\"%s\" "
              "(wire-order, not sorted) -> %s\n",
              flat.c_str(), res.c_str(), want.c_str(), pass ? "PASS" : "FAIL");
  return pass;
}

// ---- LEG 34: FilePathParts MULTI-OUTPUT (Sub-seam B) — EXTRACTED from string_rail_golden.cpp. ----
// THREE String outputs (Directory[0] + FilenameWithoutExtension[1] + Extension[2]) + FileExists(bool→
// Float, port 3) in ONE cook. The load-bearing multi-output proof is reading all THREE String outputs
// back as DISTINCT correct values. FileExists is HERMETIC (environment-dependent) → NOT asserted.
bool legFilePathParts(PointGraph& pg, bool injectBug, bool& ok) {
  // --- FLAT leg ---
  stringInjectBug() = injectBug;
  std::string flatDir, flatName, flatExt;
  {
    Graph g;
    Node n; n.id = 1; n.type = "FilePathParts";
    n.strParams["FilePath"] = "/home/user/project/scene.tixl.scn";  // unwired → strDef const
    g.nodes.push_back(n);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    const std::string* d = pg.debugCookedString(1);          // Directory (port 0)
    const std::string* f = pg.debugCookedStringPort(1, 1);   // FilenameWithoutExtension (port 1)
    const std::string* e = pg.debugCookedStringPort(1, 2);   // Extension (port 2)
    flatDir  = d ? *d : std::string{};
    flatName = f ? *f : std::string{};
    flatExt  = e ? *e : std::string{};
  }
  stringInjectBug() = false;
  bool flatDirOk  = (flatDir  == "/home/user/project");
  bool flatNameOk = (flatName == "scene.tixl");
  bool flatExtOk  = (flatExt  == ".scn");
  bool flatDistinct = (flatDir != flatName) && (flatName != flatExt) && (flatDir != flatExt);

  // --- RESIDENT leg (R-2 production path) ---
  stringInjectBug() = injectBug;
  std::string resDir, resName, resExt;
  {
    Graph g;
    Node n; n.id = 1; n.type = "FilePathParts";  // FilePath WIRED (resident String wire drives it)
    g.nodes.push_back(n);
    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14"
    g.nodes.push_back(fts);
    g.connections.push_back({1100, pinId(2, /*out*/ 0), pinId(1, /*FilePath port idx*/ 4)});
    g.nextId = 3;

    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
    cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut, THEN FilePathParts fans the 3 Strings
    const ResidentNode* nd = rg.node("1");
    if (nd) {
      auto rd = nd->extStrOut.find(0);  resDir  = rd != nd->extStrOut.end() ? rd->second : std::string{};
      auto rf = nd->extStrOut.find(1);  resName = rf != nd->extStrOut.end() ? rf->second : std::string{};
      auto re = nd->extStrOut.find(2);  resExt  = re != nd->extStrOut.end() ? re->second : std::string{};
    }
  }
  stringInjectBug() = false;
  bool resDirOk  = (resDir  == "");      // no separator → ""
  bool resNameOk = (resName == "3");     // "3.14" before last '.' → "3"
  bool resExtOk  = (resExt  == ".14");   // from last '.' → ".14"
  bool resDistinct = (resName != resExt);

  bool pass = flatDirOk && flatNameOk && flatExtOk && flatDistinct &&
              resDirOk && resNameOk && resExtOk && resDistinct;
  ok = ok && pass;
  std::printf("[selftest-stringrail] LEG34 FilePathParts FLAT dir=\"%s\"|name=\"%s\"|ext=\"%s\" "
              "want=/home/user/project|scene.tixl|.scn (distinct=%s); RESIDENT dir=\"%s\"|name=\"%s\"|"
              "ext=\"%s\" want=|3|.14 -> %s\n",
              flatDir.c_str(), flatName.c_str(), flatExt.c_str(), flatDistinct ? "Y" : "N",
              resDir.c_str(), resName.c_str(), resExt.c_str(), pass ? "PASS" : "FAIL");
  return pass;
}

}  // namespace

// Sub-seam A legs (LEG 34/35/36), invoked from runStringRailSelfTest so they run under
// --selftest-stringrail. Returns true iff all three pass (green) — or, under injectBug, iff all three
// BITE (the -bug teeth). The main golden ANDs this into its `ok` accumulator.
bool runStringRailSubseamA(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;
  legFilePathParts(pg, injectBug, ok);        // LEG 34 (extracted)
  legFloatListToString(pg, injectBug, ok);    // LEG 35
  legJoinStringList(pg, injectBug, ok);       // LEG 36

  q->release();
  dev->release();
  pool->release();
  return ok;
}

}  // namespace sw
