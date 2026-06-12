// ui/canvas_ids — the node canvas's pin / link / boundary-item ID SCHEME, split mechanically
// out of editor_ui.cpp (ARCHITECTURE rule 4). INTERNAL ui header: only the canvas (editor_ui)
// includes it; nothing here is persisted — wires/save key off slot strings, these ids are
// per-frame imgui-node-editor identities. Zone: ui (resolves through app/document).
#pragma once
#include <cstdint>
#include <string>

#include "runtime/compound_graph.h"  // Symbol / SymbolConnection / kSymbolBoundary

namespace sw::ui {

// Boundary pins live in their OWN high integer band, disjoint from BOTH child node ids and
// child pins — because imgui-node-editor hashes node ids AND pin ids into one ImGui ID pool
// per canvas, so the old encoding (boundary pin = combinedIndex+1, i.e. 1..99) collided pin 1
// with child node id 1 → imgui's "conflicting ID" tooltip stole window focus → the Backspace
// delete gate (IsWindowFocused) went dead and boundary nodes couldn't be deleted. The band
// base is huge so a child id / child pin would have to reach ~1M to clash (the def cap keeps
// the boundary index tiny, so it never approaches the band edge). NOTE: boundary pins
// must stay POSITIVE — negative ids would collide with the negative boundary NODE id scheme
// (edIdForInputDef/edIdForOutputDef). Boundary pins are UI-only (per-frame); wires/save key off
// slot strings, link ids pack two pins, so moving this base touches nothing on disk.
constexpr int kBoundaryPinBase = 1 << 20;  // 1048576
inline int boundaryPinId(int combinedIndex) { return kBoundaryPinBase + combinedIndex; }
inline bool pinIsBoundary(int pin) { return pin >= kBoundaryPinBase; }

// pin id <-> (child id, port index). Boundary pins decode to the kSymbolBoundary sentinel +
// their combined index; child pins go through the sw::pinId scheme. See sw::pinId().
int pinChildId(int pin);
int pinPortIndex(int pin);

// What a pin IS, child or boundary. Child pins resolve through the spec; BOUNDARY pins
// (pinChildId == kSymbolBoundary == 0; ids in the high band above, disjoint from child node
// ids and child pins) resolve through the CURRENT symbol's own defs: combined index =
// inputDefs then outputDefs. Inside the symbol an inputDef is a SOURCE (isInput=false) and an
// outputDef is a SINK — TiXL's Input/Output boundary items exactly.
struct PinInfo {
  bool valid = false;
  bool isInput = false;  // canvas perspective: is this pin a sink?
  std::string slotId;
  std::string dataType;
};

PinInfo pinInfoOf(int pin);
bool pinIsInput(int pin);

// (childId, slotId) -> absolute pin id; childId 0 = the boundary (slot among the current
// symbol's own defs). -1 when unresolvable.
int pinOfSlot(int childId, const std::string& slotId, bool isInput);

// Link ids are STATELESS: both pins packed into one 64-bit id (SymbolConnection has no id —
// rightly, TiXL wires are bare 4-tuples). Decode gives back the exact wire endpoints, so
// delete-by-link-id needs no side table. Disjoint from node/pin id ranges (>= 2^32).
inline uint64_t linkIdOf(int srcPin, int dstPin) {
  return ((uint64_t)(uint32_t)srcPin << 32) | (uint32_t)dstPin;
}
inline int linkSrcPin(uint64_t id) { return (int)(id >> 32); }
inline int linkDstPin(uint64_t id) { return (int)(id & 0xffffffffu); }

// Reconstruct the 4-tuple a link id names. False if either pin no longer resolves.
// Boundary pins decode to the kSymbolBoundary sentinel side naturally.
bool wireOfLink(uint64_t id, sw::SymbolConnection& out);

// Boundary nodes carry NEGATIVE editor ids (child ids are >= 1; the boundary pin scheme
// uses childId 0, but ed needs one id PER def item — TiXL draws each input/output as its
// own movable canvas item). inputDef i -> -(i+1); outputDef j -> -(1001+j).
inline int edIdForInputDef(int i) { return -(i + 1); }
inline int edIdForOutputDef(int j) { return -(1001 + j); }
inline bool edIdIsBoundary(int id) { return id < 0; }
// Invert the boundary-id scheme: input defs occupy [-1000,-1], output defs <= -1001. Returns the
// def's slotId in the current symbol (empty if the index no longer resolves), and sets isInput.
std::string boundaryDefSlot(const sw::Symbol& cur, int edId, bool& isInput);

}  // namespace sw::ui
