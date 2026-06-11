// ui/combine_dialog — see combine_dialog.h.
#include "ui/combine_dialog.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/command.h"        // g_commands (rename push)
#include "app/document.h"
#include "app/graph_commands.h"  // RenameSymbolCommand / RenameChildCommand
#include "ui/copy_paste_ui.h"  // copy/paste 契約 4: context-menu Copy/Paste (guaranteed path)
#include "ui/editor_ui.h"   // g_selectedNode (single-selection capture)
#include "verify/eye/eye.h" // one-line hooks: menu/dialog rects for the hand

namespace ed = ax::NodeEditor;

namespace sw::ui {
namespace {

std::vector<int> g_combineIds;   // captured at menu time (selection may change after)
bool g_openDialog = false;       // armed by the menu item; consumed by drawCombineDialog
ImVec2 g_pasteAnchor(0, 0);      // canvas coords captured when a context menu opens (paste 落點)
char g_nameBuf[128] = "";        // the symbol display name 柏為 types (UTF-8; CJK persists
                                 // since the crude_json sw-patch — rendering CJK glyphs
                                 // needs a font atlas, a named separate cut)

// --- rename (契約 rename, 批次 6) ---
// A rename targets ONE node (the right-clicked child of the CURRENT symbol). Two modes:
//   instance — rename just this node (RenameChildCommand). Always available.
//   definition — rename the referenced compound def, all instances follow (RenameSymbolCommand).
//     Only when the child references a compound (atomic names == operator type, not renamable).
int g_renameChildId = 0;         // the child to rename (0 = none armed)
std::string g_renameSymbolId;    // the child's referenced symbol (for definition mode)
bool g_renameIsDef = false;      // true = rename definition; false = rename instance
bool g_openRename = false;       // armed by the menu item; consumed by drawRenameDialog
char g_renameBuf[128] = "";      // the new name 柏為 types (UTF-8, CJK ok)

// Arm a rename of `childId` (in the CURRENT symbol). isDef = rename the referenced definition
// (compound only) vs the instance. Seeds the input buffer with the current name so 柏為 edits,
// not retypes (= TiXL dialogs preload the existing name). Returns false if the child is gone.
bool armRename(int childId, bool isDef) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return false;
  const sw::SymbolChild* c = sw::childById(*cur, childId);
  if (!c) return false;
  g_renameChildId = childId;
  g_renameSymbolId = c->symbolId;
  g_renameIsDef = isDef;
  const std::string seed = isDef ? [&] {
    const sw::Symbol* def = sw::doc::g_lib.find(c->symbolId);
    return def ? def->name : c->symbolId;  // def title to edit
  }() : c->name;  // instance custom name (may be empty -> empty box, fallback shown as hint)
  std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", seed.c_str());
  g_openRename = true;
  return true;
}

// Is `childId`'s referenced symbol a (renamable) compound? Atomic names are operator types and
// must not be renamed (would desync the registry) — so "Rename Definition" hides for atomics.
bool childRefsCompound(int childId) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  const sw::SymbolChild* c = cur ? sw::childById(*cur, childId) : nullptr;
  const sw::Symbol* def = c ? sw::doc::g_lib.find(c->symbolId) : nullptr;
  return def && !def->atomic;
}

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
  // Capture the paste anchor (mouse -> canvas) BEFORE suspending: a context-menu Paste drops the
  // selection's upper-left here, the way TiXL pastes at the click target. ScreenToCanvas needs
  // the editor current, which it is in the canvas scope that calls us.
  const ImVec2 anchorOnOpen = ed::ScreenToCanvas(ImGui::GetMousePos());

  ed::Suspend();  // imgui popups live outside the canvas transform
  ed::NodeId ctxNode;
  if (ed::ShowNodeContextMenu(&ctxNode)) {
    g_combineIds = captureSelection((int)ctxNode.Get());
    g_pasteAnchor = anchorOnOpen;
    ImGui::OpenPopup("node_ctx");
  } else if (ed::ShowBackgroundContextMenu()) {
    // Right-click on empty canvas: a Paste-only menu (no node under cursor). Guaranteed-reachable
    // paste path even if Cmd+V is ever OS-intercepted (NSMenu trap, named in the report).
    g_pasteAnchor = anchorOnOpen;
    ImGui::OpenPopup("bg_ctx");
  }
  if (ImGui::BeginPopup("node_ctx")) {
    // Copy: the guaranteed Copy path (Cmd+C is the fast path). Captured selection = the ids the
    // combine menu already captured (right-clicked node or the whole selection).
    if (ImGui::MenuItem("Copy", nullptr, false, !g_combineIds.empty()))
      sw::ui::copyChildrenToClipboard(g_combineIds);
    sw::eye::recordItem("ctx:copy");
    if (ImGui::MenuItem("Paste"))
      sw::ui::pasteClipboardAt(g_pasteAnchor.x, g_pasteAnchor.y);
    sw::eye::recordItem("ctx:paste");
    ImGui::Separator();
    // Rename targets a SINGLE node (the right-clicked child / the lone selection). Rename Node =
    // instance name; Rename Definition = the referenced compound def (all instances follow), shown
    // only for compounds (atomic = operator type, not renamable).
    const int renameTarget = g_combineIds.size() == 1 ? g_combineIds[0] : 0;
    if (ImGui::MenuItem("Rename Node...", nullptr, false, renameTarget > 0))
      armRename(renameTarget, /*isDef=*/false);
    sw::eye::recordItem("ctx:rename_node");
    if (renameTarget > 0 && childRefsCompound(renameTarget)) {
      if (ImGui::MenuItem("Rename Definition..."))
        armRename(renameTarget, /*isDef=*/true);
      sw::eye::recordItem("ctx:rename_def");
    }
    ImGui::Separator();
    std::string label = "Combine " + std::to_string(g_combineIds.size()) +
                        " into new symbol...";
    if (ImGui::MenuItem(label.c_str(), nullptr, false, !g_combineIds.empty()))
      g_openDialog = true;
    sw::eye::recordItem("ctx:combine");
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopup("bg_ctx")) {
    if (ImGui::MenuItem("Paste"))
      sw::ui::pasteClipboardAt(g_pasteAnchor.x, g_pasteAnchor.y);
    sw::eye::recordItem("ctx:paste_bg");
    ImGui::EndPopup();
  }
  ed::Resume();
}

// The rename modal (契約 rename). Both modes share one dialog — the only difference is which
// command it pushes. Instance mode allows an EMPTY name (clears the custom name -> fallback to def
// name); definition mode requires non-empty (RenameSymbolCommand refuses empty anyway). CJK ok.
void drawRenameDialog() {
  if (g_openRename) {
    ImGui::OpenPopup("Rename");
    g_openRename = false;
  }
  if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextDisabled("%s", g_renameIsDef ? "Rename definition (all instances follow)"
                                            : "Rename this node");
    // Focus the field the first frame so 柏為 can type immediately (= TiXL dialog autofocus).
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool submit = ImGui::InputText("Name", g_renameBuf, sizeof(g_renameBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    sw::eye::recordItem("rename_name");
    if (!g_renameIsDef && g_renameBuf[0] == '\0')
      ImGui::TextDisabled("(empty = use definition name)");
    const bool ok = ImGui::Button("Rename") || submit;
    sw::eye::recordItem("rename_ok");
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel");
    sw::eye::recordItem("rename_cancel");
    if (ok) {
      const sw::Symbol* cur = sw::doc::currentSymbolConst();
      if (cur) {
        if (g_renameIsDef) {
          auto cmd = std::make_unique<sw::RenameSymbolCommand>(sw::doc::g_lib, g_renameSymbolId,
                                                               g_renameBuf);
          if (!cmd->refused()) sw::g_commands.push(std::move(cmd));  // refused -> no undo entry
        } else {
          auto cmd = std::make_unique<sw::RenameChildCommand>(sw::doc::g_lib, cur->id,
                                                              g_renameChildId, g_renameBuf);
          if (!cmd->refused()) sw::g_commands.push(std::move(cmd));
        }
      }
      g_renameChildId = 0;
      ImGui::CloseCurrentPopup();
    } else if (cancel) {
      g_renameChildId = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void drawCombineDialog() {
  drawRenameDialog();  // shares the same canvas-host scope; one call site (header API unchanged)
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
