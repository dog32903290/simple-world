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

// CHILD pins get the same treatment, one band lower: the raw sw::pinId(childId, port) =
// childId*100+port+1 re-enters the CHILD NODE ID range as soon as a child id passes 100 —
// child node 109 == child 1's pin 109 (Cycles) — and imgui's "conflicting ID" error makes the
// stacked node unhittable: clicks still resolve but node drags die (批次9 D4 live: spawned
// nodes 107/109 froze while 104-106 moved fine — node 1 happens to own no pins 104-106).
// Only the ed-facing id is banded; sw::pinId itself stays raw because the legacy Graph json
// persists absolute pin ints and the eye labels ("pin:10901") key off it. Band:
// [kChildPinBase+101, kBoundaryPinBase) — decode breaks if a child id ever exceeds ~5242
// ((1<<19)/100), far past the per-symbol child counts this app sees.
constexpr int kChildPinBase = 1 << 19;  // 524288
int childPinId(int childId, int portIndex);  // kChildPinBase + sw::pinId(childId, portIndex)

// pin id <-> (child id, port index). Boundary pins decode to the kSymbolBoundary sentinel +
// their combined index; child pins subtract kChildPinBase then invert sw::pinId.
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

// --selftest-canvasids: band disjointness + decode inversion (the child-pin/child-node id
// collision regression, 批次9 D4). Pure int math, no doc/imgui needed.
int runCanvasIdsSelfTest(bool injectBug);

}  // namespace sw::ui
