// runtime/t3_import_layout — the .t3ui CANVAS-POSITION SEAM (柏為 07-08 "鑽入複合子節點全擠在原點"
// fix). Split into its own TU (not t3_import.cpp/t3_import_collapse.cpp — both already at the
// ARCHITECTURE.md ≤400 line cap with zero headroom). TiXL serializes a Symbol's CHILD/PORT canvas
// positions into a SIBLING .t3ui file (SymbolChildUis[]/InputUis[]/OutputUis[], keyed by the SAME
// guids the .t3 uses), not the .t3 itself — sw's importer never read it, so every imported compound's
// children land on the default (0,0) (compound_graph.h SymbolChild::x/y / SlotDef::x/y). Coordinate
// systems match 1:1 (both Y-down, same units) — no conversion, straight copy; NavigateToContent
// absorbs the absolute offset on first dive-in.
//
// ZONE: runtime (純計算). Pure CPU: crude_json parse of the (optional) .t3ui text + position writes
// onto the ALREADY-BUILT Symbol via the caller's own t3-child-guid→childId map (no filesystem here —
// the .t3ui TEXT is handed in, same "text-in" contract as t3_import.h's importT3Symbol).
#include "runtime/t3_import_internal.h"

#include <map>
#include <string>

#include "crude_json.h"
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / SlotDef / childById

namespace sw {

using t3i::asStr;
using t3i::lc;
using t3i::stripT3Comments;

// Test-only injection seam (layout RED case): when true, applyT3uiPositions is a no-op even with a
// non-empty .t3ui text — every child/port position stays at the pre-seam default (0,0). Off in
// production; the layout golden flips this to measure RED (children collapse back onto the origin).
bool& t3LayoutDisable() {
  static bool flag = false;
  return flag;
}

namespace {
// One {X,Y} position field, however it is spelled (InputId/OutputId/ChildId all share "Position").
bool readXY(const crude_json::value& posObj, float& x, float& y) {
  if (!posObj.is_object()) return false;
  bool got = false;
  if (posObj["X"].is_number()) { x = (float)posObj["X"].get<crude_json::number>(); got = true; }
  if (posObj["Y"].is_number()) { y = (float)posObj["Y"].get<crude_json::number>(); got = true; }
  return got;
}
}  // namespace

// See t3_import_internal.h for the full contract.
void applyT3uiPositions(Symbol& sym, const std::string& t3uiJson,
                        const std::map<std::string, int>& childGuidToId) {
  if (t3LayoutDisable() || t3uiJson.empty()) return;
  crude_json::value root = crude_json::value::parse(stripT3Comments(t3uiJson));
  if (!root.is_object()) return;

  // SymbolChildUis[] { ChildId, Position:{X,Y} } → child.x/y (compound_graph.h SymbolChild::x/y).
  if (root["SymbolChildUis"].is_array())
    for (const crude_json::value& cv : root["SymbolChildUis"].get<crude_json::array>()) {
      if (!cv.is_object()) continue;
      auto it = childGuidToId.find(lc(asStr(cv, "ChildId")));
      if (it == childGuidToId.end()) continue;
      SymbolChild* ch = childById(sym, it->second);
      if (ch) readXY(cv["Position"], ch->x, ch->y);
    }

  // InputUis[]/OutputUis[] { InputId|OutputId, Position:{X,Y} } → boundary SlotDef.x/y (compound_graph.h
  // SlotDef::x/y) so a dived-in compound's own edge pins don't stack on the origin either.
  auto applySlots = [&](const char* arrKey, const char* idKey, std::vector<SlotDef>& defs) {
    if (!root[arrKey].is_array()) return;
    for (const crude_json::value& iv : root[arrKey].get<crude_json::array>()) {
      if (!iv.is_object()) continue;
      const std::string id = lc(asStr(iv, idKey));
      for (SlotDef& d : defs)
        if (d.id == id) { readXY(iv["Position"], d.x, d.y); break; }
    }
  };
  applySlots("InputUis", "InputId", sym.inputDefs);
  applySlots("OutputUis", "OutputId", sym.outputDefs);
}

}  // namespace sw
