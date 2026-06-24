// ui/node_menu_actions — the node right-click context menu's command-path glue (Tier1-B). Zone: ui.
// Pure command-glue for the WIRED items of the node context menu (combine_dialog.cpp's node_ctx
// popup), kept out of combine_dialog.cpp so that file stays focused on imgui drawing + its modals
// (ARCHITECTURE rule 4: one file one job). Each function maps 1:1 to a TiXL
// MagGraph/Interaction/GraphContextMenu.cs item. All take the captured child-id selection (the
// right-clicked node, or the whole selection) and are no-ops on empty/missing input. The node
// editor must be CURRENT at the call site (selectConnected/deleteCaptured touch ed selection).
#pragma once

#include <vector>

namespace sw::ui {

// "Select connected" (= GraphContextMenu.cs:85): grow the selection to the whole connected
// component(s) of `seedIds` (any in/out wire, transitive) and re-select it in the node editor.
void selectConnected(const std::vector<int>& seedIds);

// "Align select left" (= GraphContextMenu.cs:197): snap every selected node's x to the leftmost
// selected x via one undoable MoveChildrenCommand. No-op for <2 nodes or if already aligned.
void alignSelectionLeft(const std::vector<int>& ids);

// "Delete" (= GraphContextMenu.cs:338): the right-click form of the keyboard-Delete path — drops
// the captured ids + their incident wires undoably (DeleteChildrenCommand), clears the selection.
void deleteCaptured(const std::vector<int>& ids);

}  // namespace sw::ui
