// ui/deletenode_verb_selftest — --selftest-hand-deletenode. Headless RED->GREEN proof that the agent's
// `deletenode` HAND VERB deletes a node AND cleans up every wire incident on it through the production
// path: hand::feedLine -> g_deleteNodeHook -> ui::deleteNodeByVerb -> g_commands.push(DeleteChildrenCommand)
// -> the lib.
//
// The keystone contract this pins: deletenode pushes the SAME DeleteChildrenCommand the canvas Delete key
// does (node_menu_actions deleteCaptured), whose doIt erases every wire where src==child ‖ dst==child
// BEFORE erasing the child. The LOAD-BEARING half is the wire sweep: a leftover dangling wire (src/dst to
// a gone child) would crash cook or leave a ghost node. So the teeth assert not just "the child is gone"
// but "every wire touching it is gone too" — separately for the child as a wire SOURCE and as a wire SINK.
// And an undo leg proves the command (not a raw children.erase) carried the edit: undo restores the child
// AND all its incident wires.
//
// Same harness as connect_verb_selftest: a tiny compound swapped into the live doc (doc::g_lib +
// g_compositionPath at root), restored on exit. No ImGui, no GPU — a pure lib mutation asserted on
// cur->children + cur->connections + the undo stack.
//
// Legs: (1) deletenode removes the child; (1w) its incident wires (as src AND as dst) are gone while an
// UNRELATED wire survives; (2) bad childId is a true no-op; (3) undo restores the child + all its wires;
// (4) re-delete after undo works again. injectBug feeds NOTHING through the verb so the "child gone" +
// "wires gone" legs go RED (the child and its wires are still there) — the hand is shown to do nothing
// before a PASS is trusted.
#include <cstdio>
#include <string>
#include <vector>

#include "app/command.h"             // g_commands (the verb pushes here) / undo
#include "app/document.h"            // doc::g_lib / g_compositionPath (the live document the verb reads)
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / SymbolConnection / childById
#include "runtime/graph.h"           // findSpec (port lookup, to pick real Points slot ids)
#include "ui/connection_ops.h"       // mountConnectionVerbs (install the hooks for the test)
#include "verify/hand/hand.h"        // feedLine / clearPending

namespace sw {
namespace {

// First port id of `type` matching (isInput, dataType). "" if none.
std::string portId(const char* type, bool wantInput, const char* dataType) {
  const NodeSpec* s = findSpec(type);
  if (!s) return "";
  for (const PortSpec& p : s->ports)
    if (p.isInput == wantInput && p.dataType == dataType) return p.id;
  return "";
}

// Count wires in `s` that touch child `id` on EITHER endpoint (the incident set DeleteChildrenCommand
// sweeps). 0 = the child is fully unwired.
int incidentWires(const Symbol& s, int id) {
  int n = 0;
  for (const SymbolConnection& w : s.connections)
    if (w.srcChild == id || w.dstChild == id) ++n;
  return n;
}

// Does an EXACT wire (src,slot -> dst,slot) exist? Used to assert an unrelated wire survives the delete.
bool hasWire(const Symbol& s, int sc, const std::string& ss, int dc, const std::string& ds) {
  for (const SymbolConnection& w : s.connections)
    if (w.srcChild == sc && w.srcSlot == ss && w.dstChild == dc && w.dstSlot == ds) return true;
  return false;
}

}  // namespace

int runHandDeleteNodeSelfTest(bool injectBug) {
  // --- swap in a tiny compound; restore the live doc on exit (connect-verb precedent) ---
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;

  // Three atomic point ops sharing the Points type so we can build a chain of wires:
  //   child 1 RadialPoints    (Points out)
  //   child 2 OrientPoints (Points in + Points out)  <- the DELETE target (a wire SINK for 1->2 AND
  //                                                        a wire SOURCE for 2->3)
  //   child 3 OrientPoints (Points in + Points out)
  // Wires: 1.out -> 2.in  (child 2 is the dst)   [incident on 2]
  //        2.out -> 3.in  (child 2 is the src)   [incident on 2]
  //        1.out -> 3.in  is NOT built; instead we ALSO keep an unrelated survivor below.
  // Unrelated survivor wire: 1.out -> 3.in (touches neither... it touches 1 and 3, both survive) — this
  // proves the sweep is SCOPED to child 2, not a blanket wipe.
  const std::string srcOut = portId("RadialPoints", /*input=*/false, "Points");
  const std::string tin    = portId("OrientPoints", /*input=*/true, "Points");
  const std::string tout   = portId("OrientPoints", /*input=*/false, "Points");

  bool ok = !srcOut.empty() && !tin.empty() && !tout.empty();  // ops must expose the Points ports

  SymbolLibrary lib;
  Symbol comp; comp.id = "comp"; comp.name = "comp";
  { SymbolChild a; a.id = 1; a.symbolId = "RadialPoints";    comp.children.push_back(a); }
  { SymbolChild b; b.id = 2; b.symbolId = "OrientPoints"; comp.children.push_back(b); }
  { SymbolChild c; c.id = 3; c.symbolId = "OrientPoints"; comp.children.push_back(c); }
  comp.nextChildId = 4;
  // Wires (built directly in the lib, not via the connect verb — this test is about DELETE, not connect).
  { SymbolConnection w; w.srcChild = 1; w.srcSlot = srcOut; w.dstChild = 2; w.dstSlot = tin;
    comp.connections.push_back(w); }                                   // 1->2 (2 is dst)
  { SymbolConnection w; w.srcChild = 2; w.srcSlot = tout;   w.dstChild = 3; w.dstSlot = tin;
    comp.connections.push_back(w); }                                   // 2->3 (2 is src)
  { SymbolConnection w; w.srcChild = 1; w.srcSlot = srcOut; w.dstChild = 3; w.dstSlot = tin;
    comp.connections.push_back(w); }                                   // 1->3 survivor (no child 2)
  // NOTE: 3.in now has two incoming wires (2->3 and 1->3). OrientPoints' Points input is NOT
  // multiInput, but we bypass the connect-verb single-cardinality guard by building wires directly —
  // that is fine here: the test only asserts DELETE's incident-sweep math, not connect legality.
  lib.symbols[comp.id] = comp;
  lib.rootId = "comp";

  doc::g_lib() = lib;
  doc::g_compositionPath.clear();  // root scope -> currentSymbol() == "comp"
  g_commands.clear();
  ui::mountConnectionVerbs();      // installs deleteNodeByVerb onto the hand (among the other verbs)
  hand::clearPending();

  auto cur = [&]() -> Symbol* { return doc::g_lib().find("comp"); };

  const int wiresBefore = (int)cur()->connections.size();  // 3
  const int incidentBefore = incidentWires(*cur(), 2);     // 2 (1->2 and 2->3)

  // (1) deletenode 2: the child AND both its incident wires vanish; the unrelated 1->3 survivor stays.
  //     injectBug skips the verb line so the "child gone" + "wires gone" legs go RED.
  if (!injectBug) hand::feedLine("deletenode 2");

  const bool childGone = (childById(*cur(), 2) == nullptr);
  const bool incidentGone = (incidentWires(*cur(), 2) == 0);
  const bool survivorKept = hasWire(*cur(), 1, srcOut, 3, tin);
  const int wiresNow = (int)cur()->connections.size();
  // After deleting child 2 both incident wires go; only the 1->3 survivor remains -> exactly 1 wire.
  const bool wireCountRight = (wiresNow == wiresBefore - incidentBefore);  // 3 - 2 == 1
  const bool deleted = childGone && incidentGone && survivorKept && wireCountRight;
  ok = ok && deleted;
  std::printf("[selftest-hand-deletenode] (1) deletenode -> child-gone=%d incident-gone=%d survivor-kept=%d "
              "wires %d->%d(want %d) %s\n",
              childGone, incidentGone, survivorKept, wiresBefore, wiresNow, wiresBefore - incidentBefore,
              deleted ? "OK" : "RED");

  if (injectBug) {
    // Under bug nothing was fed: the child (and its wires) still present = the correct RED. If the child
    // vanished, the hand secretly acted -> surface that. Restore + report.
    doc::g_lib() = saved; doc::g_compositionPath = savedPath; g_commands.clear();
    if (!ok) { std::printf("[selftest-hand-deletenode] injectBug correctly RED\n"); return 1; }
    std::printf("[selftest-hand-deletenode] FAIL: injectBug fed no deletenode yet the node vanished\n");
    return 1;
  }

  // (2) bad childId: deleting a non-existent child 99 must be a TRUE no-op (the bad-id guard, mirrors
  //     connect/setparam). Neither children nor connections move.
  {
    const int childCountBefore = (int)cur()->children.size();
    const int wireCountBefore = (int)cur()->connections.size();
    hand::feedLine("deletenode 99");
    const bool noop = (int)cur()->children.size() == childCountBefore &&
                      (int)cur()->connections.size() == wireCountBefore;
    ok = ok && noop;
    std::printf("[selftest-hand-deletenode] (2) bad childId -> no change %s\n", noop ? "OK" : "RED");
  }

  // (3) undo restores the child AT ITS ORIGINAL INDEX + every incident wire — proves the SAME
  //     DeleteChildrenCommand path (not a raw children.erase) carried the edit. After undo the whole
  //     graph is byte-identical to pre-delete: child 2 back, both incident wires back, survivor intact.
  {
    g_commands.undo();
    const bool childBack = (childById(*cur(), 2) != nullptr);
    const bool incidentBack = (incidentWires(*cur(), 2) == incidentBefore);  // 2
    const bool allWiresBack = (int)cur()->connections.size() == wiresBefore;  // 3
    // original index: child 2 was at children[1] (between 1 and 3) — undo inserts at the snapshot index.
    const bool orderRight = cur()->children.size() == 3 && cur()->children[1].id == 2;
    const bool restored = childBack && incidentBack && allWiresBack && orderRight;
    ok = ok && restored;
    std::printf("[selftest-hand-deletenode] (3) undo -> child-back=%d incident-back=%d all-wires=%d "
                "order-right=%d %s\n",
                childBack, incidentBack, allWiresBack, orderRight, restored ? "OK" : "RED");
  }

  // (4) re-delete after undo: the verb is usable again across an undo (drives a fresh command).
  {
    hand::feedLine("deletenode 2");
    const bool gone = (childById(*cur(), 2) == nullptr) && incidentWires(*cur(), 2) == 0;
    ok = ok && gone;
    std::printf("[selftest-hand-deletenode] (4) re-delete after undo -> gone=%d %s\n",
                gone, gone ? "OK" : "RED");
  }

  // --- restore the live document ---
  doc::g_lib() = saved;
  doc::g_compositionPath = savedPath;
  g_commands.clear();
  hand::clearPending();

  std::printf("[selftest-hand-deletenode] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
