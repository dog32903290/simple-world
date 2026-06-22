// string_builder_golden — StringBuilder + StringBuilderToString golden legs (LEG 39/40), EXTRACTED
// from string_rail_golden.cpp to keep that file at-or-below its line-count cap (1605). Both legs run
// under --selftest-stringrail (called unconditionally from runStringRailSelfTest via
// runStringBuilderGolden below). Also self-registers as --selftest-stringbuilder for isolated runs.
//
// NOTE: LEG 37/38 are already taken in string_rail_golden_subseama.cpp (PickStringFromList/ZipStringList).
// New legs are numbered 39/40.
//
// LEG 39: StringBuilderToString FLAT + RESIDENT.
//   fork-stringbuilder-as-string: sw has no StringBuilder currency; InputBuffer declared as String
//   (passthrough). cook = inputStrings[0] or "" when unwired.
//   (A) FLAT unwired → "".  (B) FLAT wired → "3.14".  (C) RESIDENT wired → "3.14".
//   RED: injectBug drops last char → "3.1" ≠ "3.14".
//
// LEG 40: StringBuilder + StringBuilderToString canonical chain RESIDENT.
//   fork-stringbuilder-no-resident-state: StringBuilder is a stateless InitialString passthrough.
//   Chain: StringBuilder(InitialString="World") → StringBuilderToString → "World" via cookStringNodes.
//   RED: injectBug drops char of upstream cook → downstream also drops → "Wor" ≠ "World".
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

static int runStringBuilderSelftestImpl(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // LEG 39 — StringBuilderToString FLAT + RESIDENT.
  {
    // (A) FLAT unwired → "" (fork-null-is-empty positive assertion, no injectBug needed).
    {
      std::string gotEmpty = [&]() {
        Graph g;
        Node n; n.id = 1; n.type = "StringBuilderToString";
        g.nodes.push_back(n);
        EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
        pg.cook(g, ctx, nullptr, 1);
        const std::string* out = pg.debugCookedString(1);
        return out ? *out : std::string{"FAIL"};
      }();
      bool passEmpty = (gotEmpty == "");
      ok = ok && passEmpty;
      std::printf("[selftest-stringrail] LEG39A StringBuilderToString(unwired)=\"%s\" want=\"\" -> %s\n",
                  gotEmpty.c_str(), passEmpty ? "PASS" : "FAIL");
    }
    // (B) FLAT wired: FloatToString(3.14,"") → StringBuilderToString → "3.14". injectBug → "3.1" → RED.
    {
      stringInjectBug() = injectBug;
      std::string gotWired = [&]() {
        Graph g;
        Node n; n.id = 1; n.type = "StringBuilderToString";
        g.nodes.push_back(n);
        Node fts; fts.id = 2; fts.type = "FloatToString";
        fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14"
        g.nodes.push_back(fts);
        // FloatToString.Output (port 0) → StringBuilderToString.InputBuffer (port 1).
        g.connections.push_back({600, pinId(2, 0), pinId(1, 1)});
        g.nextId = 3;
        EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
        pg.cook(g, ctx, nullptr, 1);
        const std::string* out = pg.debugCookedString(1);
        return out ? *out : std::string{};
      }();
      stringInjectBug() = false;
      bool passWired = (gotWired == "3.14");
      ok = ok && passWired;
      std::printf("[selftest-stringrail] LEG39B StringBuilderToString(wired \"3.14\")=\"%s\" want=\"3.14\" -> %s\n",
                  gotWired.c_str(), passWired ? "PASS" : "FAIL");
    }
    // (C) RESIDENT (R-2 production path): buildEvalGraph → cookStringNodes → extStrOut. injectBug → RED.
    {
      stringInjectBug() = injectBug;
      std::string gotResident = [&]() {
        Graph g;
        Node n; n.id = 1; n.type = "StringBuilderToString";
        g.nodes.push_back(n);
        Node fts; fts.id = 2; fts.type = "FloatToString";
        fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";
        g.nodes.push_back(fts);
        g.connections.push_back({601, pinId(2, 0), pinId(1, 1)});
        g.nextId = 3;
        SymbolLibrary lib = libFromGraph(g);
        ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
        ResidentEvalCtx rc;
        rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
        cookStringNodes(rg, rc);
        const ResidentNode* nd = rg.node("1");
        if (!nd) return std::string{};
        auto it = nd->extStrOut.find(0);
        return it != nd->extStrOut.end() ? it->second : std::string{};
      }();
      stringInjectBug() = false;
      bool passResident = (gotResident == "3.14");
      ok = ok && passResident;
      std::printf("[selftest-stringrail] LEG39C StringBuilderToString RESIDENT(\"3.14\")=\"%s\" want=\"3.14\" -> %s\n",
                  gotResident.c_str(), passResident ? "PASS" : "FAIL");
    }
  }

  // LEG 40 — StringBuilder + StringBuilderToString canonical chain RESIDENT (R-2).
  //
  // fork-stringbuilder-no-resident-state: StringBuilder is a stateless InitialString passthrough in sw.
  // Chain: StringBuilder(InitialString="World") → StringBuilderToString → "World".
  // injectBug: StringBuilder drops last char → "Worl"; passthrough also drops → "Wor" ≠ "World" → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotChain = [&]() {
      // Graph: StringBuilderToString id=1 (terminal); StringBuilder id=2 (producer).
      // StringBuilder ports: [0]=Builder(out), [1]=InitialString(String), [2]=ClearTrigger(Float).
      // StringBuilderToString ports: [0]=String(out), [1]=InputBuffer(String).
      Graph g;
      Node sbt; sbt.id = 1; sbt.type = "StringBuilderToString";
      g.nodes.push_back(sbt);
      Node sb; sb.id = 2; sb.type = "StringBuilder";
      sb.strParams["InitialString"] = "World";
      g.nodes.push_back(sb);
      // StringBuilder.Builder (port 0) → StringBuilderToString.InputBuffer (port 1).
      g.connections.push_back({700, pinId(2, 0), pinId(1, 1)});
      g.nextId = 3;
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);
      const ResidentNode* nd = rg.node("1");
      if (!nd) return std::string{};
      auto it = nd->extStrOut.find(0);
      return it != nd->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;
    bool passChain = (gotChain == "World");
    ok = ok && passChain;
    std::printf("[selftest-stringrail] LEG40 StringBuilder->StringBuilderToString RESIDENT=\"%s\" want=\"World\" -> %s\n",
                gotChain.c_str(), passChain ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-stringbuilder] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register as --selftest-stringbuilder for isolated runs (does NOT clash with --selftest-stringrail
// which calls runStringBuilderGolden unconditionally from runStringRailSelfTest).
REGISTER_SELFTESTS(/*orderBase=*/240, {"--selftest-stringbuilder", runStringBuilderSelftestImpl});

// Called unconditionally from runStringRailSelfTest (string_rail_golden.cpp) so LEG 39/40 always run
// under --selftest-stringrail --bite without short-circuit. Returns true iff all pass.
bool runStringBuilderGolden(bool injectBug) {
  return runStringBuilderSelftestImpl(injectBug) == 0;
}

}  // namespace sw
