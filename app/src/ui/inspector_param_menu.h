// ui/inspector_param_menu — the per-parameter right-click context menu (split from inspector.cpp
// for the ≤400-line ratchet, ARCHITECTURE rule 4). = TiXL InputValueUi ContextMenuForItem: a
// right-click on a parameter row offers "Reset to default" (when overridden), "Animate" (build
// curves + first keys at the current values), or — when already animated — "Remove Animation".
// All gestures go through undoable commands. Zone: ui (depends on app(command/document) + runtime).
#pragma once

#include <string>
#include <vector>

namespace sw::ui {

// One slot's pre-reset state: its id + (had override? old value) so undo restores exactly what was
// there. For a Vec head the caller collects one entry per component (each erased + restored
// together as ONE undo step). = the SetOverrideCommand undo contract, applied in reverse.
struct ResetSlot {
  std::string id;
  bool had;
  float old;
};

// The parameter context menu, attached to the LAST drawn Float widget (its DragFloat / DragScalarN
// row). `slotId` is the anim-GROUP HEAD's id (scalar = the port itself, Vec = the Widget::Vec
// head, graph.h 同源). `resets` (non-animated rows only) drives the TiXL "Reset to default"
// item — present-and-overridden entries erase via ResetOverrideCommand (one MacroCommand = one
// undo step); all-default / empty suppresses the item (TiXL greys it out when IsDefault).
void animateContextMenu(const std::string& symbolId, int childId, const std::string& slotId,
                        bool animated, const std::vector<ResetSlot>& resets = {});

}  // namespace sw::ui
