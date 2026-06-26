// ui/editor_ui_canvas_edit — canvas mutation handlers split from editor_ui.cpp (mechanical,
// rule 4). Keyboard shortcuts + the delete macro, moved verbatim; no behavior change. Zone: ui.
#include "ui/editor_ui_canvas_edit.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "ui/canvas_ids.h"      // pin/link/boundary id scheme
#include "ui/copy_paste_ui.h"   // copySelectionToClipboard / pasteClipboardAt

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/graph.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {

// Undo / Redo: Cmd+Z / Cmd+Shift+Z (macOS).
// IMPORTANT: imgui's ConfigMacOSXBehaviors (default on __APPLE__) swaps
// Cmd->Ctrl inside AddKeyEvent, so a physical Cmd press lands in io.KeyCtrl,
// NOT io.KeySuper. Detect Cmd via io.KeyCtrl. (Verified by --selftest-hand.)
void processCanvasKeyboard() {
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsWindowFocused() && io.KeyCtrl && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      if (io.KeyShift) sw::g_commands.redo();
      else             sw::g_commands.undo();
      sw::doc::g_status = io.KeyShift ? "redo" : "undo";
      sw::doc::g_relayout = true;  // canvas re-seeds node positions from the restored lib
    }
    // Cmd+C / Cmd+V (copy/paste 契約 4). Same io.KeyCtrl detection as Cmd+Z (ConfigMacOSX
    // swaps Cmd->Ctrl). NSMenu has NO Edit menu here so these aren't OS-intercepted like Cmd+S
    // was — but the context-menu Copy/Paste below is the guaranteed-reachable path regardless.
    else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
      sw::ui::copySelectionToClipboard();
    } else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
      // Keyboard paste anchors at the canvas point under the mouse (TiXL pastes at the target
      // position); ed::ScreenToCanvas needs the editor current, which it is inside ed::Begin.
      ImVec2 c = ed::ScreenToCanvas(ImGui::GetMousePos());
      sw::ui::pasteClipboardAt(c.x, c.y);
    }
  }
}

// Delete links / nodes (select + Delete key, or Backspace routed above).
void processCanvasDeletions(sw::Symbol* cur) {
  if (ed::BeginDelete()) {
    std::vector<sw::SymbolConnection> delWires;
    std::vector<int> delNodes;
    std::vector<std::pair<std::string, bool>> delDefs;  // (slotId, isInput) boundary defs to remove (S13)
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid))
      if (ed::AcceptDeletedItem()) {
        sw::SymbolConnection w;
        if (wireOfLink(lid.Get(), w)) delWires.push_back(w);
      }
    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      // Boundary items = the symbol's own input/output defs. Deleting one = the S13 收屍 contract
      // edit (removes the def + every wire/override across the lib that referenced it). Decode which
      // def, ACCEPT so ed drops the canvas item; the macro below applies the lib surgery.
      int edId = (int)nid.Get();
      if (edIdIsBoundary(edId)) {
        bool isInput = false;
        std::string slot = boundaryDefSlot(*cur, edId, isInput);
        if (!slot.empty() && ed::AcceptDeletedItem()) delDefs.push_back({slot, isInput});
        else ed::RejectDeletedItem();  // unresolved index (mid-frame retype): keep it alive
        continue;
      }
      if (ed::AcceptDeletedItem()) delNodes.push_back(edId);
    }
    ed::EndDelete();

    // 入射於被刪 child 的連線交給 DeleteChildrenCommand 處理，從 delWires 去重，
    // 否則同一條線會被刪兩次（undo 也會重複還原）。
    auto incidentToDeletedNode = [&](const sw::SymbolConnection& w) {
      return std::find(delNodes.begin(), delNodes.end(), w.srcChild) != delNodes.end() ||
             std::find(delNodes.begin(), delNodes.end(), w.dstChild) != delNodes.end();
    };
    std::vector<sw::SymbolConnection> standaloneWires;
    for (const sw::SymbolConnection& w : delWires)
      if (!incidentToDeletedNode(w)) standaloneWires.push_back(w);

    if (!delNodes.empty() || !standaloneWires.empty() || !delDefs.empty()) {
      auto macro = std::make_unique<sw::MacroCommand>("Delete");
      if (!standaloneWires.empty())
        macro->add(std::make_unique<sw::DeleteWiresCommand>(sw::doc::g_lib(), cur->id,
                                                            standaloneWires));
      if (!delNodes.empty())
        macro->add(std::make_unique<sw::DeleteChildrenCommand>(sw::doc::g_lib(), cur->id, delNodes));
      // Def removals LAST (mirror TiXL Modifications.cs:184-191: children deleted first so the def
      // scrub only touches connections still present — though our snapshot captures whatever it hits).
      for (const auto& [slot, isInput] : delDefs)
        macro->add(std::make_unique<sw::DeleteInputOrOutputDefCommand>(sw::doc::g_lib(), cur->id, slot,
                                                                       isInput));
      sw::g_commands.push(std::move(macro));
      // Removing a def is a contract change — SAY so (柏為: silent edits read as broken). ASCII only.
      sw::doc::g_status = delDefs.empty() ? "deleted" : "removed boundary def (def edit, broadcasts)";
    }
  } else {
    ed::EndDelete();
  }
}

}  // namespace sw::ui
