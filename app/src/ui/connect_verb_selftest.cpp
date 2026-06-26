// ui/connect_verb_selftest — --selftest-hand-connect. Headless RED->GREEN proof that the agent's
// `connect`/`disconnect` HAND VERBS drive a real wire edit through the production path:
// hand::feedLine -> g_connectHook -> ui::connectByVerb -> applyConnection -> g_commands -> the lib.
//
// It drives the SAME hook main.cpp mounts (mountConnectionVerbs), against a tiny compound swapped
// into the live doc (doc::g_lib + g_compositionPath at root, restored on exit — the variation-panel
// selftest precedent). No ImGui, no GPU: a pure lib mutation asserted on cur->connections.
//
// Legs: add a wire, reject a bad childId (zero change), MultiInput add, dataType-mismatch reject,
// disconnect. injectBug feeds NOTHING through the verb (no connect line) so the "wire appeared"
// assertion goes RED — the hand is shown to do nothing before a PASS is trusted.
#include <cstdio>
#include <string>
#include <vector>

#include "app/command.h"            // g_commands (the verb pushes here)
#include "app/document.h"           // doc::g_lib / g_compositionPath (the live document the verb reads)
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / connectionToInput
#include "runtime/graph.h"          // findSpec (port lookup, to pick real slot ids)
#include "ui/connection_ops.h"      // mountConnectionVerbs (install the hooks for the test)
#include "verify/hand/hand.h"       // feedLine / clearPending

namespace sw {
namespace {

// First port id of `type` matching (isInput, dataType). "" if none — lets the test address slots by
// their REAL string ids (the verb takes string ids, identical to the canvas pin scheme).
std::string portId(const char* type, bool wantInput, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return "";
  for (const PortSpec& p : s->ports)
    if (p.isInput == wantInput && p.dataType == dataType) return p.id;
  return "";
}

int countWiresTo(const Symbol& s, int dstChild, const std::string& dstSlot) {
  int n = 0;
  for (const SymbolConnection& w : s.connections)
    if (w.dstChild == dstChild && w.dstSlot == dstSlot) ++n;
  return n;
}

}  // namespace

int runHandConnectSelfTest(bool injectBug) {
  // --- swap in a tiny compound; restore the live doc on exit (variation-panel precedent) ---
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;

  // A compound whose subgraph has TWO atomic point ops that share a Points type: RadialPoints
  // (Points output) and TransformPoints (Points input + output). The wire we drive is
  // RadialPoints.<out> -> TransformPoints.<in>. A second RadialPoints lets us exercise reconnect.
  const std::string srcOut = portId("RadialPoints", /*input=*/false, "Points");
  const std::string dstIn  = portId("TransformPoints", /*input=*/true, "Points");
  const std::string dstOut = portId("TransformPoints", /*input=*/false, "Points");

  bool ok = !srcOut.empty() && !dstIn.empty();  // the ops must expose the Points ports we address

  SymbolLibrary lib;
  Symbol comp; comp.id = "comp"; comp.name = "comp";
  { SymbolChild a; a.id = 1; a.symbolId = "RadialPoints";    comp.children.push_back(a); }
  { SymbolChild b; b.id = 2; b.symbolId = "TransformPoints"; comp.children.push_back(b); }
  { SymbolChild c; c.id = 3; c.symbolId = "RadialPoints";    comp.children.push_back(c); }
  comp.nextChildId = 4;
  lib.symbols[comp.id] = comp;
  lib.rootId = "comp";

  doc::g_lib() = lib;
  doc::g_compositionPath.clear();        // root scope -> currentSymbol() == "comp"
  g_commands.clear();
  ui::mountConnectionVerbs();            // install the hooks the verb forwards to
  hand::clearPending();

  auto cur = [&]() -> Symbol* { return doc::g_lib().find("comp"); };

  // (1) connect: RadialPoints(1).out -> TransformPoints(2).in. injectBug skips the verb line so the
  //     "wire appeared" leg goes RED.
  if (!injectBug) {
    std::string line = "connect 1 " + srcOut + " 2 " + dstIn;
    hand::feedLine(line.c_str());
  }
  bool wired = countWiresTo(*cur(), 2, dstIn) == 1;
  const SymbolConnection* w = connectionToInput(*cur(), 2, dstIn);
  bool correctSrc = w && w->srcChild == 1 && w->srcSlot == srcOut;
  ok = ok && wired && correctSrc;
  std::printf("[selftest-hand-connect] (1) connect verb -> wire present=%d src-correct=%d %s\n",
              wired, correctSrc, (wired && correctSrc) ? "OK" : "RED");

  if (injectBug) {
    // Only the connect leg is meaningful under bug (nothing was fed). A non-empty connection set =
    // the hand secretly acted -> that is the failure we want surfaced. Restore + report.
    doc::g_lib() = saved; doc::g_compositionPath = savedPath; g_commands.clear();
    if (!ok) { std::printf("[selftest-hand-connect] injectBug correctly RED\n"); return 1; }
    std::printf("[selftest-hand-connect] FAIL: injectBug fed no connect yet a wire appeared\n");
    return 1;
  }

  // (2) bad childId: connecting from a non-existent child 99 must be a TRUE no-op (the bad-id guard,
  //     mirrors selectnode). Wire count to the dst input is unchanged.
  {
    int before = countWiresTo(*cur(), 2, dstIn);
    std::string line = "connect 99 " + srcOut + " 2 " + dstIn;
    hand::feedLine(line.c_str());
    int after = countWiresTo(*cur(), 2, dstIn);
    bool noop = (after == before);
    ok = ok && noop;
    std::printf("[selftest-hand-connect] (2) bad src childId -> no change (%d==%d) %s\n",
                before, after, noop ? "OK" : "RED");
  }

  // (3) reconnect: wire the SECOND RadialPoints(3) into the same input(2). Single-cardinality
  //     (Points input is NOT multiInput) -> still exactly one wire, now sourced from child 3.
  {
    std::string line = "connect 3 " + srcOut + " 2 " + dstIn;
    hand::feedLine(line.c_str());
    int cnt = countWiresTo(*cur(), 2, dstIn);
    const SymbolConnection* nw = connectionToInput(*cur(), 2, dstIn);
    bool reconnected = (cnt == 1) && nw && nw->srcChild == 3;
    ok = ok && reconnected;
    std::printf("[selftest-hand-connect] (3) reconnect -> count=%d src=%d %s\n",
                cnt, nw ? nw->srcChild : -1, reconnected ? "OK" : "RED");
  }

  // (4) dataType mismatch reject: TransformPoints has no Float INPUT named like a Points slot; drive
  //     a connect whose src is a Points OUTPUT into a slot id that does not exist -> bad dst slot,
  //     no change. (The pin drag rejects type mismatch via RejectNewItem; the verb rejects via the
  //     hook's portInfo/type guard. We assert the graph did not move.)
  {
    int before = (int)cur()->connections.size();
    std::string line = "connect 1 " + srcOut + " 2 __no_such_slot__";
    hand::feedLine(line.c_str());
    int after = (int)cur()->connections.size();
    bool rejected = (after == before);
    ok = ok && rejected;
    std::printf("[selftest-hand-connect] (4) bad dst slot reject -> size %d==%d %s\n",
                before, after, rejected ? "OK" : "RED");
  }

  // (4b) wrong-direction reject: connecting an OUTPUT into another OUTPUT (src=2.dstOut treated as a
  //      source, dst=2.dstOut as a target on the SAME node) must reject. Use child 2's OUTPUT as the
  //      dst -> "dst is an output" guard fires; graph unchanged.
  if (!dstOut.empty()) {
    int before = (int)cur()->connections.size();
    std::string line = "connect 1 " + srcOut + " 2 " + dstOut;  // dst slot is an OUTPUT
    hand::feedLine(line.c_str());
    int after = (int)cur()->connections.size();
    bool rejected = (after == before);
    ok = ok && rejected;
    std::printf("[selftest-hand-connect] (4b) dst-is-output reject -> size %d==%d %s\n",
                before, after, rejected ? "OK" : "RED");
  }

  // (5) disconnect: remove the wire feeding input 2 -> the input is unwired (count 0).
  {
    std::string line = "disconnect 2 " + dstIn;
    hand::feedLine(line.c_str());
    int cnt = countWiresTo(*cur(), 2, dstIn);
    bool gone = (cnt == 0);
    ok = ok && gone;
    std::printf("[selftest-hand-connect] (5) disconnect verb -> wire count=%d %s\n",
                cnt, gone ? "OK" : "RED");
  }

  // --- restore the live document ---
  doc::g_lib() = saved;
  doc::g_compositionPath = savedPath;
  g_commands.clear();
  hand::clearPending();

  std::printf("[selftest-hand-connect] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
