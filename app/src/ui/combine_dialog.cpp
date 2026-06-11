// ui/combine_dialog — see combine_dialog.h.
#include "ui/combine_dialog.h"

#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/document.h"
#include "ui/editor_ui.h"   // g_selectedNode (single-selection capture)
#include "verify/eye/eye.h" // one-line hooks: menu/dialog rects for the hand

namespace ed = ax::NodeEditor;

namespace sw::ui {
namespace {

std::vector<int> g_combineIds;   // captured at menu time (selection may change after)
bool g_openDialog = false;       // armed by the menu item; consumed by drawCombineDialog
char g_nameBuf[128] = "";        // the symbol display name 柏為 types (UTF-8; CJK persists
                                 // since the crude_json sw-patch — rendering CJK glyphs
                                 // needs a font atlas, a named separate cut)

// The child ids the combine should take: every selected node (positive = child; negative
// boundary items excluded), else the right-clicked node itself.
std::vector<int> captureSelection(int ctxNodeId) {
  std::vector<int> ids;
  ed::NodeId sel[64];
  int n = ed::GetSelectedNodes(sel, 64);
  for (int i = 0; i < n; ++i)
    if ((int)sel[i].Get() > 0) ids.push_back((int)sel[i].Get());
  if (ids.empty() && ctxNodeId > 0) ids.push_back(ctxNodeId);
  return ids;
}

}  // namespace

void drawCanvasContextMenu() {
  ed::Suspend();  // imgui popups live outside the canvas transform
  ed::NodeId ctxNode;
  if (ed::ShowNodeContextMenu(&ctxNode)) {
    g_combineIds = captureSelection((int)ctxNode.Get());
    ImGui::OpenPopup("node_ctx");
  }
  if (ImGui::BeginPopup("node_ctx")) {
    std::string label = "Combine " + std::to_string(g_combineIds.size()) +
                        " into new symbol...";
    if (ImGui::MenuItem(label.c_str(), nullptr, false, !g_combineIds.empty()))
      g_openDialog = true;
    sw::eye::recordItem("ctx:combine");
    ImGui::EndPopup();
  }
  ed::Resume();
}

void drawCombineDialog() {
  if (g_openDialog) {
    ImGui::OpenPopup("Combine into new symbol");
    g_openDialog = false;
  }
  static std::string s_lastError;
  if (ImGui::BeginPopupModal("Combine into new symbol", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextDisabled("%zu nodes -> one compound (not undoable)", g_combineIds.size());
    ImGui::InputText("Name", g_nameBuf, sizeof(g_nameBuf));
    sw::eye::recordItem("combine_name");
    if (!s_lastError.empty())
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", s_lastError.c_str());
    if (ImGui::Button("Combine")) {
      if (sw::doc::doCombine(g_combineIds, g_nameBuf)) {
        g_nameBuf[0] = '\0';
        s_lastError.clear();
        // ed still holds the MOVED (now nonexistent) ids selected — an immediate second
        // right-click would offer a stale count (refuter 批次4 #7d).
        ed::SetCurrentEditor(g_NodeEditor);
        ed::ClearSelection();
        ed::SetCurrentEditor(nullptr);
        ImGui::CloseCurrentPopup();
      } else {
        s_lastError = sw::doc::g_status;  // keep the dialog (and the typed name) alive
      }
    }
    sw::eye::recordItem("combine_ok");
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      s_lastError.clear();
      ImGui::CloseCurrentPopup();
    }
    sw::eye::recordItem("combine_cancel");
    ImGui::EndPopup();
  }
}

}  // namespace sw::ui
