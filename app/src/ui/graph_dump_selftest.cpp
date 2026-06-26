// ui/graph_dump_selftest — --selftest-graphdump. Headless RED->GREEN proof that req_graph dumps the
// CURRENT compound faithfully: build a known compound, run the LIVE dump path (eye::writeGraphDump via
// the mounted hook), read graph.json back, and assert children/ports/connection counts + ids.
//
// It drives the SAME hook main.cpp mounts (mountGraphDump) against a tiny compound swapped into the
// live doc (doc::g_lib + g_compositionPath at root, restored on exit — connect_verb_selftest precedent).
// No JSON lib: the assertions are substring/count checks over the serialized text (the dump format is
// ours and stable). injectBug DROPS a child from the build so the parsed dump is missing it AND missing
// its wire -> the count assertions fire RED (the dump is shown to under-report before a PASS is trusted).
#include "ui/graph_dump.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/document.h"            // doc::g_lib / g_compositionPath (the live doc the dump reads)
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"           // findSpec (pick REAL slot ids for the wire + assertions)
#include "verify/eye/eye.h"          // writeGraphDump (runs the mounted hook -> graph.json)

#ifndef SW_EYE_DIR
#define SW_EYE_DIR "/tmp/sw_eye"
#endif

namespace sw::ui {
namespace {

// First port id of `type` matching (isInput, dataType). "" if none.
std::string portId(const char* type, bool wantInput, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return "";
  for (const PortSpec& p : s->ports)
    if (p.isInput == wantInput && p.dataType == dataType) return p.id;
  return "";
}

// Count non-overlapping occurrences of `needle` in `hay`.
int countOccurrences(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return 0;
  int n = 0;
  for (size_t pos = 0; (pos = hay.find(needle, pos)) != std::string::npos; pos += needle.size()) ++n;
  return n;
}

}  // namespace

int runGraphDumpSelfTest(bool injectBug) {
  // --- swap in a tiny compound; restore the live doc on exit ---
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;

  const std::string srcOut = portId("RadialPoints", /*input=*/false, "Points");
  const std::string dstIn  = portId("TransformPoints", /*input=*/true, "Points");
  bool ok = !srcOut.empty() && !dstIn.empty();

  // Known compound: RadialPoints(1) -> TransformPoints(2), plus a third child (3) so the count
  // assertion has a child to lose under injectBug. The wire connects 1.out -> 2.in.
  SymbolLibrary lib;
  Symbol comp; comp.id = "comp"; comp.name = "comp";
  { SymbolChild a; a.id = 1; a.symbolId = "RadialPoints";    comp.children.push_back(a); }
  { SymbolChild b; b.id = 2; b.symbolId = "TransformPoints"; comp.children.push_back(b); }
  if (!injectBug) { SymbolChild c; c.id = 3; c.symbolId = "RadialPoints"; comp.children.push_back(c); }
  { SymbolConnection w; w.srcChild = 1; w.srcSlot = srcOut; w.dstChild = 2; w.dstSlot = dstIn;
    comp.connections.push_back(w); }
  comp.nextChildId = 4;
  lib.symbols[comp.id] = comp;
  lib.rootId = "comp";

  doc::g_lib() = lib;
  doc::g_compositionPath.clear();  // root scope -> currentSymbol() == "comp"
  mountGraphDump();                // install the hook the live req_graph path forwards to

  // Run the LIVE dump path (eye::writeGraphDump -> the mounted hook -> graph.json on disk).
  eye::writeGraphDump("graph.json");

  // Read graph.json back.
  std::string path = std::string(SW_EYE_DIR) + "/graph.json";
  std::ifstream f(path);
  std::stringstream buf;
  buf << f.rdbuf();
  const std::string json = buf.str();

  // The EXPECTED truth (3 children + 1 wire normally; 2 children + 1 wire under bug).
  const int expectChildren = injectBug ? 2 : 3;
  const int childCount = countOccurrences(json, "\"childId\":");
  const int connCount = countOccurrences(json, "\"srcChild\":");
  const bool child3Present = json.find("\"childId\": 3") != std::string::npos;

  // Ids/ports must be addressable (what the agent reads before `connect`).
  const bool hasChild1 = json.find("\"childId\": 1") != std::string::npos;
  const bool hasChild2 = json.find("\"childId\": 2") != std::string::npos;
  const bool hasOpType = json.find("\"opType\": \"RadialPoints\"") != std::string::npos;
  const bool hasSrcSlot = !srcOut.empty() &&
                          json.find("\"srcSlot\": \"" + srcOut + "\"") != std::string::npos;
  const bool hasDstSlot = !dstIn.empty() &&
                          json.find("\"dstSlot\": \"" + dstIn + "\"") != std::string::npos;
  const bool hasPortMeta = json.find("\"isInput\":") != std::string::npos &&
                           json.find("\"multiInput\":") != std::string::npos &&
                           json.find("\"dataType\": \"Points\"") != std::string::npos;
  const bool hasCompound = json.find("\"compound\": {\"id\": \"comp\"") != std::string::npos;

  const bool countsRight = (childCount == 3) && (connCount == 1) && child3Present;
  ok = ok && hasChild1 && hasChild2 && hasOpType && hasSrcSlot && hasDstSlot && hasPortMeta &&
       hasCompound && countsRight;

  std::printf("[selftest-graphdump] children=%d(expect %d) conns=%d child3=%d compound=%d "
              "src/dst-slot=%d/%d ports-meta=%d -> %s\n",
              childCount, expectChildren, connCount, (int)child3Present, (int)hasCompound,
              (int)hasSrcSlot, (int)hasDstSlot, (int)hasPortMeta, ok ? "OK" : "RED");

  // --- restore the live document ---
  doc::g_lib() = saved;
  doc::g_compositionPath = savedPath;

  if (injectBug) {
    // Under bug a child (and its addressability) is missing: childCount==2, child3 absent ->
    // the "3 children" truth is violated -> ok is FALSE -> RED is the correct outcome.
    if (!ok) { std::printf("[selftest-graphdump] injectBug correctly RED\n"); return 1; }
    std::printf("[selftest-graphdump] FAIL: injectBug dropped a child yet the dump still matched\n");
    return 1;
  }

  std::printf("[selftest-graphdump] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::ui
