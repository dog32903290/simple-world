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
#include "runtime/graph.h"       // specTypes / addChildWouldCycle (Add Node submenu)
#include "ui/copy_paste_ui.h"  // copy/paste 契約 4: context-menu Copy/Paste (guaranteed path)
#include "ui/node_menu_actions.h"  // Tier1-B node-menu glue (Select connected / Align / Delete)
#include "ui/editor_ui.h"   // g_selectedNode + spawnNodeAt (single-selection / spawn)
#include "verify/eye/eye.h" // one-line hooks: menu/dialog rects for the hand
#include "verify/hand/hand.h" // gap-2 hook: drain selectnode requests (editor current here)

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
    const sw::Symbol* def = sw::doc::g_lib().find(c->symbolId);
    return def ? def->name : c->symbolId;  // def title to edit
  }() : c->name;  // instance custom name (may be empty -> empty box, fallback shown as hint)
  std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", seed.c_str());
  g_openRename = true;
  return true;
}

// Draw the "Add Node" sub-menu items, spawning at `anchor` (canvas coords).
// = TiXL GraphView.cs:857-861 "if (ImGui.BeginMenu("Add...")) / MenuItem("Add Node...") /
//   SymbolBrowser.OpenAt(InverseTransformPositionFloat(clickPosition), ...)".
// Called from bg_ctx so right-clicking the canvas background offers this path.
void drawAddNodeSubmenu(ImVec2 anchor) {
  const bool open = ImGui::BeginMenu("Add Node");
  sw::eye::recordItem("ctx:add_node_bg");  // eye: header item (always rendered, hand hovers it)
  if (!open) return;
  const std::string& curId = sw::doc::currentSymbolId();
  for (const std::string& t : sw::specTypes()) {
    if (ImGui::MenuItem(t.c_str())) sw::ui::spawnNodeAt(t, anchor.x, anchor.y);
    sw::eye::recordItem(("bgctx:add:" + t).c_str());
  }
  // Compound definitions (same list as toolbar popup, same cycle-guard styling).
  bool first = true;
  for (const auto& kv : sw::doc::g_lib().symbols) {
    const sw::Symbol& s = kv.second;
    if (s.atomic) continue;
    if (first) { ImGui::Separator(); first = false; }
    const bool cyclic = sw::addChildWouldCycle(sw::doc::g_lib(), curId, s.id);
    const std::string label = s.name.empty() ? s.id : s.name;
    ImGui::BeginDisabled(cyclic);
    if (ImGui::MenuItem(label.c_str())) sw::ui::spawnNodeAt(s.id, anchor.x, anchor.y);
    ImGui::EndDisabled();
    if (cyclic && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("would nest this composition inside itself");
    sw::eye::recordItem(("bgctx:add:" + s.id).c_str());
  }
  ImGui::EndMenu();
}

// Is `childId`'s referenced symbol a (renamable) compound? Atomic names are operator types and
// must not be renamed (would desync the registry) — so "Rename Definition" hides for atomics.
bool childRefsCompound(int childId) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  const sw::SymbolChild* c = cur ? sw::childById(*cur, childId) : nullptr;
  const sw::Symbol* def = c ? sw::doc::g_lib().find(c->symbolId) : nullptr;
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
  sw::hand::applyPendingSelectNodes();  // gap-2 hook: agent `selectnode <childId>` -> ed::SelectNode (editor current, canvas scope)
  // Capture the paste anchor (mouse -> canvas) BEFORE suspending: a context-menu Paste drops the
  // selection's upper-left here, the way TiXL pastes at the click target. ScreenToCanvas needs
  // the editor current, which it is in the canvas scope that calls us.
  const ImVec2 anchorOnOpen = ed::ScreenToCanvas(ImGui::GetMousePos());

  // Right-click ROUTING gate: node-editor's Show*ContextMenu fires for any right-click it saw,
  // INCLUDING ones that land on a floating imgui window (Inspector/Output/Timeline) hovering
  // above the canvas — which hijacked the Inspector's own per-param context menu (live drill:
  // right-click "Animate" came up as canvas "Paste"). imgui's hover resolution DOES account for
  // occlusion, so "is the canvas window the hovered one" is the discriminator. We still CALL
  // Show*ContextMenu (it consumes node-editor's internal click state) but only open our popups
  // when the canvas truly owns the mouse.
  const bool canvasOwnsMouse = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

  ed::Suspend();  // imgui popups live outside the canvas transform
  ed::NodeId ctxNode;
  if (ed::ShowNodeContextMenu(&ctxNode)) {
    if (canvasOwnsMouse) {
      g_combineIds = captureSelection((int)ctxNode.Get());
      g_pasteAnchor = anchorOnOpen;
      ImGui::OpenPopup("node_ctx");
    }
  } else if (ed::ShowBackgroundContextMenu()) {
    // Right-click on empty canvas: a Paste-only menu (no node under cursor). Guaranteed-reachable
    // paste path even if Cmd+V is ever OS-intercepted (NSMenu trap, named in the report).
    if (canvasOwnsMouse) {
      g_pasteAnchor = anchorOnOpen;
      ImGui::OpenPopup("bg_ctx");
    }
  }
  if (ImGui::BeginPopup("node_ctx")) {
    // Item set + order = TiXL MagGraph/Interaction/GraphContextMenu.cs (the modern node menu).
    // WIRED items reuse an existing command path; items whose backing data does NOT exist on
    // SymbolChild yet are shown DISABLED (greyed) — parity surface complete, no faked action.
    // Greyed (no backing): Disable (node-level IsDisabled; we only have per-output disable in the
    // Inspector), Add Comment (no SymbolChild.comment), Display as... (no SymbolChild.style).
    const bool hasSel = !g_combineIds.empty();
    const int renameTarget = g_combineIds.size() == 1 ? g_combineIds[0] : 0;

    // --- Undo / Redo (= GraphContextMenu.cs:35-42). Title shows the next command name in parens,
    // disabled when its stack is empty. Label format byte-faithful to TiXL: "Undo (Name)" / plain
    // "Undo". Click reuses the EXACT Cmd+Z / Cmd+Shift+Z path (g_commands.undo()/redo() +
    // g_relayout), no duplicated logic. The command NAME string is sw's own (RenameChildCommand
    // etc.) — TiXL command names differ, so the parenthesized text is sw's, not a faked TiXL name.
    // FORK (named): TiXL's GraphContextMenu has only Undo; the Redo row + its parenthesized redo
    // title (lastRedoName) are a sw parity extension, symmetric with Undo. ---
    {
      const bool canUndo = sw::g_commands.canUndo();
      std::string undoLabel =
          canUndo ? std::string("Undo (") + sw::g_commands.lastUndoName() + ")" : "Undo";
      if (ImGui::MenuItem(undoLabel.c_str(), nullptr, false, canUndo)) {
        sw::g_commands.undo();
        sw::doc::g_status = "undo";
        sw::doc::g_relayout = true;  // canvas re-seeds node positions from the restored lib
      }
      sw::eye::recordItem("ctx:undo");
      // Second eye row carries the FULL visible label text (parenthesized command name) so the
      // harness can assert the title parity, not just item presence (precedent: timeline_window).
      sw::eye::recordItem(("ctxlabel:" + undoLabel).c_str());

      const bool canRedo = sw::g_commands.canRedo();
      std::string redoLabel =
          canRedo ? std::string("Redo (") + sw::g_commands.lastRedoName() + ")" : "Redo";
      if (ImGui::MenuItem(redoLabel.c_str(), nullptr, false, canRedo)) {
        sw::g_commands.redo();
        sw::doc::g_status = "redo";
        sw::doc::g_relayout = true;
      }
      sw::eye::recordItem("ctx:redo");
      sw::eye::recordItem(("ctxlabel:" + redoLabel).c_str());
    }
    ImGui::Separator();

    // --- Select... (= GraphContextMenu.cs:48 "Select..." submenu) ---
    if (ImGui::BeginMenu("Select...")) {
      if (ImGui::MenuItem("Select connected", nullptr, false, hasSel))
        selectConnected(g_combineIds);
      sw::eye::recordItem("ctx:select_connected");
      ImGui::EndMenu();
    }
    sw::eye::recordItem("ctx:select");
    ImGui::Separator();

    // --- Disable (= GraphContextMenu.cs:161 ToggleDisabled). GREYED: SymbolChild has no node-level
    // IsDisabled field (only per-output disable, which lives in the Inspector). ---
    ImGui::MenuItem("Disable", nullptr, false, /*enabled=*/false);
    sw::eye::recordItem("ctx:disable");

    // --- Bypassed (= GraphContextMenu.cs:170 ToggleBypassed). WIRED (S2 批次7). Single bypassable
    // child; checkmark = state; command re-checks wiring/whitelist + refuses (no dead undo). ---
    if (renameTarget > 0) {
      const sw::Symbol* cur = sw::doc::currentSymbolConst();
      const sw::SymbolChild* c = cur ? sw::childById(*cur, renameTarget) : nullptr;
      const bool canBypass = c && sw::childIsBypassable(sw::doc::g_lib(), *c);
      const bool isBp = c && c->isBypassed;
      if (ImGui::MenuItem("Bypassed", nullptr, isBp, canBypass || isBp)) {
        auto cmd = std::make_unique<sw::SetBypassChildCommand>(sw::doc::g_lib(), cur->id, renameTarget,
                                                              !isBp);
        if (!cmd->refused()) {
          sw::g_commands.push(std::move(cmd));
          sw::doc::bumpLibRevision();  // projection rebuilds the resident graph next frame
        }
      }
      sw::eye::recordItem("ctx:bypass");
    } else {
      ImGui::MenuItem("Bypassed", nullptr, false, /*enabled=*/false);
      sw::eye::recordItem("ctx:bypass");
    }

    // --- Rename (= GraphContextMenu.cs:178). WIRED. Targets a SINGLE node; Rename Definition shows
    // only for compounds (atomic = operator type, not renamable). ---
    if (ImGui::MenuItem("Rename Node...", nullptr, false, renameTarget > 0))
      armRename(renameTarget, /*isDef=*/false);
    sw::eye::recordItem("ctx:rename_node");
    if (renameTarget > 0 && childRefsCompound(renameTarget)) {
      if (ImGui::MenuItem("Rename Definition..."))
        armRename(renameTarget, /*isDef=*/true);
      sw::eye::recordItem("ctx:rename_def");
    }

    // --- Add Comment (= GraphContextMenu.cs:184 EditCommentDialog). GREYED: no SymbolChild.comment
    // field (canvas Annotations exist, but the per-node comment is a separate model field). ---
    ImGui::MenuItem("Add Comment", nullptr, false, /*enabled=*/false);
    sw::eye::recordItem("ctx:add_comment");

    // --- Align select left (= GraphContextMenu.cs:197 AlignSelectionToLeft). WIRED via
    // MoveChildrenCommand. Needs >1 selected nodes. ---
    if (ImGui::MenuItem("Align select left", nullptr, false, g_combineIds.size() > 1))
      alignSelectionLeft(g_combineIds);
    sw::eye::recordItem("ctx:align_left");

    // --- Display as... (= GraphContextMenu.cs:254). GREYED: SymbolChild has no `style` field yet,
    // so Small / Resizable / Expanded have nothing to write. ---
    if (ImGui::BeginMenu("Display as...")) {
      ImGui::MenuItem("Small", nullptr, false, /*enabled=*/false);
      sw::eye::recordItem("ctx:display_small");
      ImGui::MenuItem("Resizable", nullptr, false, /*enabled=*/false);
      sw::eye::recordItem("ctx:display_resizable");
      ImGui::MenuItem("Expanded", nullptr, false, /*enabled=*/false);
      sw::eye::recordItem("ctx:display_expanded");
      ImGui::EndMenu();
    }
    sw::eye::recordItem("ctx:display_as");
    ImGui::Separator();

    // --- Copy / Paste (= GraphContextMenu.cs:313). WIRED (the guaranteed clipboard path). ---
    if (ImGui::MenuItem("Copy", nullptr, false, hasSel))
      sw::ui::copyChildrenToClipboard(g_combineIds);
    sw::eye::recordItem("ctx:copy");
    if (ImGui::MenuItem("Paste"))
      sw::ui::pasteClipboardAt(g_pasteAnchor.x, g_pasteAnchor.y);
    sw::eye::recordItem("ctx:paste");

    // --- Delete (= GraphContextMenu.cs:338 DeleteSelectedElements). WIRED: right-click form of the
    // keyboard-Delete path (DeleteChildrenCommand), drops nodes + incident wires undoably. ---
    if (ImGui::MenuItem("Delete", "Del", false, hasSel))
      deleteCaptured(g_combineIds);
    sw::eye::recordItem("ctx:delete");

    // --- Duplicate (= GraphContextMenu.cs:352 Copy then Paste). WIRED via the same copy+paste
    // commands; pastes at a small cascade offset from the captured anchor. ---
    if (ImGui::MenuItem("Duplicate", nullptr, false, hasSel)) {
      sw::ui::copyChildrenToClipboard(g_combineIds);
      sw::ui::pasteClipboardAt(g_pasteAnchor.x + 20.0f, g_pasteAnchor.y + 20.0f);
    }
    sw::eye::recordItem("ctx:duplicate");
    ImGui::Separator();

    // --- Combine into new symbol... (= GraphContextMenu.cs:391 "Combine into new type..."). ---
    std::string label = "Combine " + std::to_string(g_combineIds.size()) +
                        " into new symbol...";
    if (ImGui::MenuItem(label.c_str(), nullptr, false, hasSel))
      g_openDialog = true;
    sw::eye::recordItem("ctx:combine");
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopup("bg_ctx")) {
    if (ImGui::MenuItem("Paste"))
      sw::ui::pasteClipboardAt(g_pasteAnchor.x, g_pasteAnchor.y);
    sw::eye::recordItem("ctx:paste_bg");
    ImGui::Separator();
    drawAddNodeSubmenu(g_pasteAnchor);
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
          auto cmd = std::make_unique<sw::RenameSymbolCommand>(sw::doc::g_lib(), g_renameSymbolId,
                                                               g_renameBuf);
          if (!cmd->refused()) sw::g_commands.push(std::move(cmd));  // refused -> no undo entry
        } else {
          auto cmd = std::make_unique<sw::RenameChildCommand>(sw::doc::g_lib(), cur->id,
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
