// ui/connection_ops — the ONE place that turns a resolved SymbolConnection into a wire edit.
//
// WHY: the canvas pin-drag (editor_ui BeginCreate/AcceptNewItem) and the agent's `connect`/
// `disconnect` hand verbs both want the SAME mutation: build a wire, honour MultiInput /
// reconnect / exact-dup rules, push it onto g_commands as an undo unit. That logic used to live
// inline in the drag branch only. It is extracted here so the verb hook reuses ONE copy — a
// single behaviour, two entry points (drag + verb). Zero behaviour change for the drag path is
// the contract (the canvas/connection selftest stays green as proof).
//
// ZONE: ui (pushes app::g_commands, reads runtime compound types) — same tier as editor_ui.
#pragma once
#include <string>

#include "runtime/compound_graph.h"  // Symbol / SymbolConnection

namespace sw::ui {

// Apply a fully-resolved wire `nw` into compound `cur`, pushing the edit onto g_commands:
//   - dst is a MultiInput slot  -> ADD the wire (skip only an EXACT duplicate src+dst).
//   - dst already wired, same src -> no-op.
//   - dst already wired, diff src -> Reconnect macro (delete old + add new, one undo unit).
//   - dst unwired -> add.
// Sets doc::g_status to "linked"/"linked (multi)"/"reconnected". Caller has ALREADY validated
// child/slot existence + direction + dataType (the drag branch via pins, the verb via the hook).
// `cur` must be non-null.
void applyConnection(Symbol* cur, const SymbolConnection& nw);

// Remove whatever wire feeds (dstChild, dstSlot) in `cur`, pushing a DeleteWiresCommand. No-op
// (no command pushed) when that input is unwired. Sets doc::g_status. `cur` must be non-null.
void applyDisconnection(Symbol* cur, int dstChild, const std::string& dstSlot);

// Install the `connect`/`disconnect` hand verbs onto verify/hand (leaf inversion). Call once at
// editor init (main.cpp), beside the other hand-hook mounts. The verb impls resolve the current
// compound from doc, validate child/slot/direction/dataType, then drive applyConnection/
// applyDisconnection — so the agent's text verb walks the SAME mutation as a canvas pin drag.
void mountConnectionVerbs();

}  // namespace sw::ui
