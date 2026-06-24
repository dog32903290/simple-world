// string_ops_wrapstring_golden — WrapString golden leg (LEG 41), a NEW golden file (kept ≤400 lines,
// new-file-safe) rather than appended to string_rail_golden.cpp (at its ratchet cap). Runs under
// --selftest-stringrail (called UNCONDITIONALLY from runStringRailSelfTest via runWrapStringGolden
// below — NOT short-circuited by `ok &&`, so it always runs and bites under --bite). Also self-registers
// as --selftest-wrapstring for isolated runs.
//
// TiXL authority: Operators/Lib/string/transform/WrapString.cs. Modes: DontWrap=0, WrapAtWords=1,
// WrapAtCharacters=2 (deprecated→passthrough), WrapToFillBlock=3, SolidBlock=4. WrapColumn clamped
// [1,10000]. Expected strings HAND-DERIVED by stepping the verbatim InsertLineWraps algorithm:
//
//   (A) SolidBlock("abcdefg", col=3)        → "abc\ndef\ng"   (insert \n every 3 chars)
//   (B) WrapAtWords("hello world foo", col=5) → "hello\nworld\nfoo" (break at spaces when line>col)
//   (C) WrapToFillBlock("aa bb cc dd", col=5) → "aa bb\ncc dd"  (fill block; existing spaces are breaks)
//   (D) DontWrap("hello world", col=5)      → "hello world"   (no branch → passthrough)
//   (E) WrapAtCharacters("hello world", col=3) → "hello world" (deprecated → passthrough, no mutation)
//   (F) WrapColumn clamp floor: SolidBlock("abc", col=0→clamped 1) → "a\nb\nc\n"  (TiXL inserts \n
//       AFTER every char incl. the last when line len hits col → trailing newline is faithful)
//
//   RESIDENT (G): FloatToString(12345,"") → "12345" wired into WrapString.InputText, SolidBlock col=2 →
//     "12\n34\n5". Proves the resident String-wire feeds InputText AND the resident Float-param spine
//     supplies WrapColumn/Mode to a String producer's cook.
//
// RED (injectBug): stringInjectBug() corrupts the REAL cook (WrapString drops its last char); on the
// resident path FloatToString ALSO drops its last char before WrapString runs. No want-inversion — the
// bug bites the actual cook path (each non-empty expected string loses its tail → mismatch → RED).
#include <cstdio>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (resident leg)
#include "runtime/eval_context.h"          // EvaluationContext
#include "runtime/graph.h"                 // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/point_graph.h"           // PointGraph::cook + debugCookedString
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / cookStringNodes
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"   // stringInjectBug

namespace sw {

static int runWrapStringSelftestImpl(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // FLAT helper: WrapString(strDef=inputText, WrapColumn, Mode) as a single terminal node → host string.
  auto cookWrap = [&](const std::string& inputText, int wrapColumn, int mode) -> std::string {
    Graph g;
    Node n; n.id = 1; n.type = "WrapString";
    n.strParams["InputText"] = inputText;  // unwired → strDef const carries the string
    n.params["WrapColumn"] = static_cast<float>(wrapColumn);
    n.params["Mode"] = static_cast<float>(mode);
    g.nodes.push_back(n);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    const std::string* out = pg.debugCookedString(1);
    return out ? *out : std::string{};
  };

  // FLAT sub-cases (A)-(F).
  {
    stringInjectBug() = injectBug;
    std::string gA = cookWrap("abcdefg",        3, /*SolidBlock=*/4);
    std::string gB = cookWrap("hello world foo", 5, /*WrapAtWords=*/1);
    std::string gC = cookWrap("aa bb cc dd",     5, /*WrapToFillBlock=*/3);
    std::string gD = cookWrap("hello world",     5, /*DontWrap=*/0);
    std::string gE = cookWrap("hello world",     3, /*WrapAtCharacters=*/2);
    std::string gF = cookWrap("abc",             0, /*SolidBlock=*/4);  // col clamps 0→1
    stringInjectBug() = false;

    bool passA = (gA == "abc\ndef\ng");
    bool passB = (gB == "hello\nworld\nfoo");
    bool passC = (gC == "aa bb\ncc dd");
    bool passD = (gD == "hello world");
    bool passE = (gE == "hello world");
    bool passF = (gF == "a\nb\nc\n");  // SolidBlock col=1 → \n after every char incl. last (TiXL-faithful)
    bool pass = passA && passB && passC && passD && passE && passF;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG41 WrapString FLAT "
                "(A SolidBlock)=\"%s\" want=\"abc\\ndef\\ng\"; "
                "(B WrapAtWords)=\"%s\" want=\"hello\\nworld\\nfoo\"; "
                "(C WrapToFillBlock)=\"%s\" want=\"aa bb\\ncc dd\"; "
                "(D DontWrap)=\"%s\"; (E WrapAtChars)=\"%s\"; (F col-clamp)=\"%s\" want=\"a\\nb\\nc\\n\" -> %s\n",
                gA.c_str(), gB.c_str(), gC.c_str(), gD.c_str(), gE.c_str(), gF.c_str(),
                pass ? "PASS" : "FAIL");
  }

  // RESIDENT (G): FloatToString(12345,"") → "12345" wired into WrapString.InputText, SolidBlock col=2.
  {
    // Graph: WrapString id=1 (terminal), FloatToString id=2 (String producer for InputText).
    // WrapString ports: [0]=Result(out), [1]=InputText(String in), [2]=WrapColumn(Float), [3]=Mode(Float).
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format.
    Graph g;
    Node ws; ws.id = 1; ws.type = "WrapString";
    ws.params["WrapColumn"] = 2.0f;
    ws.params["Mode"] = 4.0f;  // SolidBlock
    // InputText is WIRED (no strParams["InputText"] — the wire drives it on the resident path).
    g.nodes.push_back(ws);

    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 12345.0f; fts.strParams["Format"] = "";  // → "12345"
    g.nodes.push_back(fts);
    // FloatToString.Output (port 0) → WrapString.InputText (port 1).
    g.connections.push_back({600, pinId(2, /*out*/ 0), pinId(1, /*InputText*/ 1)});
    g.nextId = 3;

    stringInjectBug() = injectBug;
    std::string gotResident = [&]() -> std::string {
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut["2"][0], THEN WrapString reads it
      const ResidentNode* n = rg.node("1");  // WrapString resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Result out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;

    bool pass = (gotResident == "12\n34\n5");
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG41 WrapString RESIDENT FloatToString(12345)→InputText SolidBlock"
                "col=2=\"%s\" want=\"12\\n34\\n5\" -> %s\n",
                gotResident.c_str(), pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-wrapstring] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register as --selftest-wrapstring for isolated runs (does NOT clash with --selftest-stringrail
// which calls runWrapStringGolden unconditionally from runStringRailSelfTest).
REGISTER_SELFTESTS(/*orderBase=*/260, {"wrapstring", runWrapStringSelftestImpl});

// Called unconditionally from runStringRailSelfTest (string_rail_golden.cpp) so LEG 41 always runs under
// --selftest-stringrail --bite without short-circuit. Returns true iff all pass.
bool runWrapStringGolden(bool injectBug) {
  return runWrapStringSelftestImpl(injectBug) == 0;
}

}  // namespace sw
