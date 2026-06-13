// ui/copy_paste_ui — see copy_paste_ui.h.
#include "ui/copy_paste_ui.h"

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/document.h"
#include "app/command.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/copy_paste.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {

void copyChildrenToClipboard(const std::vector<int>& childIds) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return;
  if (childIds.empty()) { sw::doc::g_status = "nothing selected to copy"; return; }
  sw::ClipboardData clip = sw::extractClipboard(*cur, childIds);
  if (clip.children.empty()) { sw::doc::g_status = "nothing to copy"; return; }
  ImGui::SetClipboardText(sw::clipboardToJson(clip).c_str());
  sw::doc::g_status = "copied " + std::to_string(clip.children.size());
}

void copySelectionToClipboard() {
  ed::NodeId sel[256];
  int n = ed::GetSelectedNodes(sel, 256);
  std::vector<int> ids;
  for (int i = 0; i < n; ++i) {
    int id = (int)sel[i].Get();
    if (id > 0) ids.push_back(id);  // positive = real child; negative = boundary item (skip)
  }
  copyChildrenToClipboard(ids);
}

void pasteClipboardAt(float canvasX, float canvasY) {
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (!cur) return;
  const char* text = ImGui::GetClipboardText();
  if (!text) { sw::doc::g_status = "clipboard empty"; return; }
  sw::ClipboardData clip;
  if (!sw::clipboardFromJson(text, clip)) {
    sw::doc::g_status = "clipboard has no nodes to paste";
    return;
  }
  sw::PastePlan plan = sw::planPaste(sw::doc::g_lib, cur->id, clip, canvasX, canvasY);
  if (plan.children.empty() && plan.annotations.empty()) {
    // Every child was cycle-dropped (e.g. pasting a compound into its own ancestor) or the
    // target vanished, and no annotation rode along. SAY so — a silent paste reads as broken
    // (柏為). ASCII only.
    sw::doc::g_status = "nothing pasted (cycle or empty)";
    return;
  }
  const size_t dropped = clip.children.size() - plan.children.size();
  auto cmd = std::make_unique<sw::CopyPasteChildrenCommand>(sw::doc::g_lib, cur->id, std::move(plan));
  sw::g_commands.push(std::move(cmd));
  sw::doc::g_relayout = true;  // re-seed positions for the newly added children
  sw::doc::g_status = dropped ? "pasted (some dropped: would cycle)" : "pasted";
}

}  // namespace sw::ui
