// ui/connection_ops — see connection_ops.h. The single wire-edit applier (drag + verb share it)
// plus the app-owned `connect`/`disconnect` hand-verb hooks (leaf inversion: verify/hand holds
// only a fn-ptr; this fills it with the real doc-driven mutation).
#include "ui/connection_ops.h"

#include <memory>
#include <string>
#include <vector>

#include "app/command.h"          // g_commands / MacroCommand
#include "app/document.h"         // doc::g_lib / currentSymbol / g_status
#include "app/graph_commands.h"   // AddWireCommand / DeleteWiresCommand
#include "runtime/graph.h"        // findSpec (atomic child ports)
#include "verify/hand/hand.h"     // setConnectHook / setDisconnectHook (mount the verbs)

namespace sw::ui {

void applyConnection(Symbol* cur, const SymbolConnection& nw) {
  // Is the destination a MultiInput slot (批次25 seam)? Then a new wire ADDS (the slot keeps N
  // sources) instead of reconnecting. Look up the dst child's NodeSpec port.multiInput. (Identical
  // to the inline drag logic that lived in editor_ui — extracted verbatim, one copy now.)
  bool dstMulti = false;
  for (const SymbolChild& ch : cur->children)
    if (ch.id == nw.dstChild) {
      if (const NodeSpec* sp = findSpec(ch.symbolId))
        for (const PortSpec& p : sp->ports)
          if (p.id == nw.dstSlot) { dstMulti = p.multiInput; break; }
      break;
    }
  const SymbolConnection* old = connectionToInput(*cur, nw.dstChild, nw.dstSlot);
  if (dstMulti) {
    // MultiInput: allow many wires; only skip an EXACT duplicate (same src + dst).
    bool dup = false;
    for (const SymbolConnection& w : cur->connections)
      if (w.dstChild == nw.dstChild && w.dstSlot == nw.dstSlot &&
          w.srcChild == nw.srcChild && w.srcSlot == nw.srcSlot) { dup = true; break; }
    if (!dup) {
      g_commands.push(std::make_unique<AddWireCommand>(sw::doc::g_lib(), cur->id, nw));
      sw::doc::g_status = "linked (multi)";
    }
  } else if (old && old->srcChild == nw.srcChild && old->srcSlot == nw.srcSlot) {
    // already wired to this exact source — nothing to do
  } else if (old) {
    // reconnect: remove the input's old wire, add the new one, as one undo unit
    auto macro = std::make_unique<MacroCommand>("Reconnect");
    macro->add(std::make_unique<DeleteWiresCommand>(
        sw::doc::g_lib(), cur->id, std::vector<SymbolConnection>{*old}));
    macro->add(std::make_unique<AddWireCommand>(sw::doc::g_lib(), cur->id, nw));
    g_commands.push(std::move(macro));
    sw::doc::g_status = "reconnected";
  } else {
    g_commands.push(std::make_unique<AddWireCommand>(sw::doc::g_lib(), cur->id, nw));
    sw::doc::g_status = "linked";
  }
}

void applyDisconnection(Symbol* cur, int dstChild, const std::string& dstSlot) {
  const SymbolConnection* old = connectionToInput(*cur, dstChild, dstSlot);
  if (!old) { sw::doc::g_status = "disconnect: input not wired"; return; }
  g_commands.push(std::make_unique<DeleteWiresCommand>(
      sw::doc::g_lib(), cur->id, std::vector<SymbolConnection>{*old}));
  sw::doc::g_status = "disconnected";
}

namespace {

// Resolve a (childId, slotId) to its port dataType + direction within `cur`. Handles BOTH an
// atomic child (ports from findSpec(symbolId)) and a compound child (ports from the referenced
// Symbol's input/output defs). The boundary sentinel (childId==0) refers to `cur`'s OWN slot
// defs: a boundary SOURCE is an inputDef (feeds inward), a boundary TARGET is an outputDef.
// Returns false if the child/slot doesn't exist. On success sets *dataType and *isInput.
bool portInfo(const Symbol& cur, const SymbolLibrary& lib, int childId, const std::string& slot,
              bool boundaryAsInput, std::string* dataType, bool* isInput) {
  if (childId == kSymbolBoundary) {
    // The parent's own external slot. A wire's SOURCE side crosses an inputDef inward; its TARGET
    // side crosses an outputDef. `boundaryAsInput` tells us which list to scan.
    const std::vector<SlotDef>& defs = boundaryAsInput ? cur.inputDefs : cur.outputDefs;
    for (const SlotDef& d : defs)
      if (d.id == slot) { *dataType = d.dataType; *isInput = boundaryAsInput; return true; }
    return false;
  }
  const SymbolChild* ch = childById(cur, childId);
  if (!ch) return false;
  // Atomic op: ports live on its NodeSpec (symbolId == op type).
  if (const NodeSpec* sp = findSpec(ch->symbolId)) {
    for (const PortSpec& p : sp->ports)
      if (p.id == slot) { *dataType = p.dataType; *isInput = p.isInput; return true; }
    return false;
  }
  // Compound instance: ports are the referenced Symbol's external defs.
  if (const Symbol* def = lib.find(ch->symbolId)) {
    for (const SlotDef& d : def->inputDefs)
      if (d.id == slot) { *dataType = d.dataType; *isInput = true; return true; }
    for (const SlotDef& d : def->outputDefs)
      if (d.id == slot) { *dataType = d.dataType; *isInput = false; return true; }
  }
  return false;
}

// `connect <srcChild> <srcSlot> <dstChild> <dstSlot>` verb. Resolves the current compound, VALIDATES
// (both endpoints exist; src is an output, dst is an input; dataTypes match) — exactly the guards the
// canvas drag enforces via pins — then applies. Any failure = NO-OP + a g_status error (mirrors the
// selectnode bad-id guard: an addressing typo must never silently corrupt the graph).
void connectByVerb(int srcChild, const char* srcSlot, int dstChild, const char* dstSlot) {
  Symbol* cur = sw::doc::currentSymbol();
  if (!cur) { sw::doc::g_status = "connect: no current compound"; return; }
  const SymbolLibrary& lib = sw::doc::g_lib();
  std::string srcType, dstType;
  bool srcIsInput = false, dstIsInput = false;
  // src side: a boundary source crosses an inputDef (feeds inward) -> boundaryAsInput=true.
  if (!portInfo(*cur, lib, srcChild, srcSlot, /*boundaryAsInput=*/true, &srcType, &srcIsInput)) {
    sw::doc::g_status = "connect: bad src slot"; return;
  }
  // dst side: a boundary target crosses an outputDef -> boundaryAsInput=false.
  if (!portInfo(*cur, lib, dstChild, dstSlot, /*boundaryAsInput=*/false, &dstType, &dstIsInput)) {
    sw::doc::g_status = "connect: bad dst slot"; return;
  }
  // Direction: a child source must be an OUTPUT; a child target must be an INPUT. (Boundary ports
  // invert — an inputDef SOURCES inward, an outputDef SINKS — portInfo already set isInput to the
  // role this side plays, so the same "src=output, dst=input" check holds for boundaries too.)
  if (srcIsInput) { sw::doc::g_status = "connect: src is an input"; return; }
  if (!dstIsInput) { sw::doc::g_status = "connect: dst is an output"; return; }
  if (srcType != dstType) { sw::doc::g_status = "connect: type mismatch"; return; }
  if (srcChild == dstChild) { sw::doc::g_status = "connect: same node"; return; }

  SymbolConnection nw;
  nw.srcChild = srcChild; nw.srcSlot = srcSlot;
  nw.dstChild = dstChild; nw.dstSlot = dstSlot;
  applyConnection(cur, nw);
}

// `disconnect <dstChild> <dstSlot>` verb. Validates the dst input exists, then removes its wire.
void disconnectByVerb(int dstChild, const char* dstSlot) {
  Symbol* cur = sw::doc::currentSymbol();
  if (!cur) { sw::doc::g_status = "disconnect: no current compound"; return; }
  const SymbolLibrary& lib = sw::doc::g_lib();
  std::string dstType;
  bool dstIsInput = false;
  if (!portInfo(*cur, lib, dstChild, dstSlot, /*boundaryAsInput=*/false, &dstType, &dstIsInput)) {
    sw::doc::g_status = "disconnect: bad dst slot"; return;
  }
  if (!dstIsInput) { sw::doc::g_status = "disconnect: dst is an output"; return; }
  applyDisconnection(cur, dstChild, dstSlot);
}

}  // namespace

void mountConnectionVerbs() {
  sw::hand::setConnectHook(&connectByVerb);
  sw::hand::setDisconnectHook(&disconnectByVerb);
}

}  // namespace sw::ui
