// ui/setparam_verb_selftest — --selftest-hand-setparam. Headless RED->GREEN proof that the agent's
// `setparam` HAND VERB drives a real param edit through the PRODUCTION path:
// hand::feedLine -> g_setParamHook -> ui::setParamByVerb -> g_commands.push(SetOverrideCommand) -> lib.
//
// The keystone contract this pins: setparam pushes the SAME SetOverrideCommand the Inspector slider
// does (inspector.cpp pushSet), so the value edit is undoable, saves, and marks the doc dirty exactly
// like a hand drag would — never a parallel path. So the teeth assert not just "the override changed"
// but "undo restores the pre-edit state" (proves the command, not a raw map write, carried the edit).
//
// Same harness as connect_verb_selftest: a tiny compound swapped into the live doc (doc::g_lib +
// g_compositionPath at root), restored on exit. No ImGui, no GPU — a pure lib mutation asserted on the
// child's overrides + effectiveInput read-back + the undo stack.
//
// Legs: set a Float input (read-back changed), reject a bad childId (no change), reject a non-Float /
// unknown slot (no change), undo restores the pre-edit value, re-set overwrites. injectBug feeds
// NOTHING through the verb so the "value changed" assertion goes RED — the hand is shown to do nothing
// before a PASS is trusted.
#include <cstdio>
#include <string>
#include <vector>

#include "app/command.h"             // g_commands (the verb pushes here) / undo
#include "app/document.h"            // doc::g_lib / g_compositionPath (the live document the verb reads)
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / effectiveInput / childById
#include "runtime/graph.h"           // findSpec (port lookup, to pick a real Float input slot id)
#include "ui/connection_ops.h"       // mountConnectionVerbs (install the hooks for the test)
#include "verify/hand/hand.h"        // feedLine / clearPending

namespace sw {
namespace {

// First port id of `type` matching (isInput, dataType). "" if none — lets the test address a slot by
// its REAL string id (the verb takes a string id, identical to the Inspector's slot ids).
std::string portId(const char* type, bool wantInput, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return "";
  for (const PortSpec& p : s->ports)
    if (p.isInput == wantInput && p.dataType == dataType) return p.id;
  return "";
}

}  // namespace

int runHandSetParamSelfTest(bool injectBug) {
  // --- swap in a tiny compound; restore the live doc on exit (connect-verb precedent) ---
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;

  // OrientPoints exposes at least one Float INPUT (translate/scale/rotation components). We address
  // its first Float input by real slot id. A non-Float slot id (its Points input) is the reject probe.
  const std::string floatIn = portId("OrientPoints", /*input=*/true, "Float");
  const std::string pointsIn = portId("OrientPoints", /*input=*/true, "Points");

  bool ok = !floatIn.empty();  // the op must expose the Float input we drive

  SymbolLibrary lib;
  Symbol comp; comp.id = "comp"; comp.name = "comp";
  { SymbolChild b; b.id = 2; b.symbolId = "OrientPoints"; comp.children.push_back(b); }
  comp.nextChildId = 3;
  lib.symbols[comp.id] = comp;
  lib.rootId = "comp";

  doc::g_lib() = lib;
  doc::g_compositionPath.clear();  // root scope -> currentSymbol() == "comp"
  g_commands.clear();
  ui::mountConnectionVerbs();      // installs setParamByVerb onto the hand (among the other verbs)
  hand::clearPending();

  auto cur = [&]() -> Symbol* { return doc::g_lib().find("comp"); };
  auto child = [&]() -> SymbolChild* { return childById(*cur(), 2); };
  // The effective (read-back) value of child 2's floatIn: override if set, else definition default.
  auto readBack = [&]() -> float { return effectiveInput(doc::g_lib(), *child(), floatIn, 0.0f); };

  const float preValue = readBack();     // the default before any edit
  const float target = preValue + 3.5f;  // a value guaranteed different from the default

  // (1) setparam: child 2, floatIn -> target. injectBug skips the verb line so the "value changed"
  //     leg goes RED (the read-back stays at the default).
  if (!injectBug) {
    std::string line = "setparam 2 " + floatIn + " " + std::to_string(target);
    hand::feedLine(line.c_str());
  }
  const float afterSet = readBack();
  const bool hasOverride = child()->overrides.count(floatIn) > 0;
  const bool changed = (afterSet == target) && hasOverride;
  ok = ok && changed;
  std::printf("[selftest-hand-setparam] (1) setparam verb -> read-back %.3f (want %.3f) override=%d %s\n",
              afterSet, target, hasOverride, changed ? "OK" : "RED");

  if (injectBug) {
    // Under bug nothing was fed: an override that materialized = the hand secretly acted -> the failure
    // we want surfaced. Restore + report.
    doc::g_lib() = saved; doc::g_compositionPath = savedPath; g_commands.clear();
    if (!ok) { std::printf("[selftest-hand-setparam] injectBug correctly RED\n"); return 1; }
    std::printf("[selftest-hand-setparam] FAIL: injectBug fed no setparam yet the value changed\n");
    return 1;
  }

  // (2) bad childId: setparam on a non-existent child 99 must be a TRUE no-op (the bad-id guard,
  //     mirrors connect/selectnode). child 2's override is untouched.
  {
    const float before = readBack();
    std::string line = "setparam 99 " + floatIn + " 123.0";
    hand::feedLine(line.c_str());
    const bool noop = (readBack() == before);
    ok = ok && noop;
    std::printf("[selftest-hand-setparam] (2) bad childId -> no change %s\n", noop ? "OK" : "RED");
  }

  // (3) non-Float / unknown slot reject: a Points input (not a Float knob) and a bogus slot id must
  //     both reject with no new override — the "is not a Float input" guard.
  {
    const size_t before = child()->overrides.size();
    if (!pointsIn.empty()) hand::feedLine(("setparam 2 " + pointsIn + " 1.0").c_str());
    hand::feedLine("setparam 2 __no_such_slot__ 1.0");
    const bool rejected = (child()->overrides.size() == before);
    ok = ok && rejected;
    std::printf("[selftest-hand-setparam] (3) non-Float/unknown slot reject -> override count stable %s\n",
                rejected ? "OK" : "RED");
  }

  // (4) undo restores the pre-edit value — proves the SAME SetOverrideCommand path (not a raw override
  //     write) carried the edit. After (1) the slot had NO prior override, so undo must ERASE it (the
  //     definition default re-emerges, never a 0-residue) and the read-back returns to preValue.
  {
    g_commands.undo();
    const bool restored = (readBack() == preValue) && (child()->overrides.count(floatIn) == 0);
    ok = ok && restored;
    std::printf("[selftest-hand-setparam] (4) undo -> read-back %.3f (want %.3f) override-erased=%d %s\n",
                readBack(), preValue, (int)(child()->overrides.count(floatIn) == 0),
                restored ? "OK" : "RED");
  }

  // (5) re-set after undo (redo path is separate — we drive the verb again): setting again lands the
  //     override once more, proving the verb is idempotent-usable across an undo.
  {
    std::string line = "setparam 2 " + floatIn + " " + std::to_string(target);
    hand::feedLine(line.c_str());
    const bool reset = (readBack() == target);
    ok = ok && reset;
    std::printf("[selftest-hand-setparam] (5) re-set after undo -> read-back %.3f %s\n",
                readBack(), reset ? "OK" : "RED");
  }

  // --- restore the live document ---
  doc::g_lib() = saved;
  doc::g_compositionPath = savedPath;
  g_commands.clear();
  hand::clearPending();

  std::printf("[selftest-hand-setparam] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
