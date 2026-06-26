// ui/editor_ui_layout — canvas position sync split from editor_ui.cpp (mechanical, rule 4).
// Moved verbatim; no behavior change. Zone: ui.
#include "ui/editor_ui_layout.h"

#include <map>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "ui/canvas_ids.h"   // edIdForInputDef / edIdForOutputDef
#include "ui/editor_ui.h"    // g_navPending (set by canvas gestures, consumed here)

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/graph.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {

namespace {
// 拖動暫存：child id -> 拖動開始時的座標。空 == 沒在拖。
std::map<int, ImVec2> g_dragStart;
}  // namespace

void syncCanvasLayout(sw::Symbol* cur, bool navThisFrame) {
  if (navThisFrame) {
    // skip layout/sync this frame (see above)
  } else if (sw::doc::g_relayout) {  // initial / after add / after load / composition switch
    for (const sw::SymbolChild& child : cur->children)
      ed::SetNodePosition(child.id, ImVec2(child.x, child.y));
    for (size_t i = 0; i < cur->inputDefs.size(); ++i)
      ed::SetNodePosition(edIdForInputDef((int)i),
                          ImVec2(cur->inputDefs[i].x, cur->inputDefs[i].y));
    for (size_t j = 0; j < cur->outputDefs.size(); ++j)
      ed::SetNodePosition(edIdForOutputDef((int)j),
                          ImVec2(cur->outputDefs[j].x, cur->outputDefs[j].y));
    sw::doc::g_relayout = false;
    if (g_navPending) {  // composition switched LAST frame; positions are seeded now
      ed::ClearSelection();           // TiXL clears selection on composition change
      ed::NavigateToContent(0.0f);    // = TiXL's saved-view fallback (frame the content)
      g_navPending = false;
    }
  } else {
    bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (dragging) {
      // 拖動中：記下還沒記過的 child 的起始座標，並即時把位置反映到 lib（畫面跟手）。
      for (sw::SymbolChild& child : cur->children) {
        ImVec2 p = ed::GetNodePosition(child.id);
        if (g_dragStart.find(child.id) == g_dragStart.end())
          g_dragStart[child.id] = ImVec2(child.x, child.y);
        child.x = p.x;
        child.y = p.y;
      }
    } else if (!g_dragStart.empty()) {
      // 放手：把真正有位移的 child 組成一個 MoveChildrenCommand。
      std::vector<sw::MoveChildrenCommand::Move> moves;
      for (auto& kv : g_dragStart) {
        ImVec2 now = ed::GetNodePosition(kv.first);
        if (now.x != kv.second.x || now.y != kv.second.y)
          moves.push_back({kv.first, kv.second.x, kv.second.y, now.x, now.y});
      }
      g_dragStart.clear();
      if (!moves.empty()) {
        // 命令的 doIt 會再設一次新座標（冪等），先把 lib 設回舊座標避免雙重記錄混亂。
        for (auto& m : moves)
          if (sw::SymbolChild* c = sw::childById(*cur, m.id)) { c->x = m.oldX; c->y = m.oldY; }
        sw::g_commands.push(std::make_unique<sw::MoveChildrenCommand>(sw::doc::g_lib(), cur->id, moves));
      }
    } else {
      // 沒拖動：照常把 editor 位置同步回 lib（例如程式性移動）。
      for (sw::SymbolChild& child : cur->children) {
        ImVec2 p = ed::GetNodePosition(child.id);
        child.x = p.x;
        child.y = p.y;
      }
    }
  }

  // Boundary items: positions sync straight back to the defs (movable + persisted like
  // TiXL IInputUi.PosOnCanvas; no undo step for def moves — named asymmetry vs child moves).
  if (!navThisFrame) {
    for (size_t i = 0; i < cur->inputDefs.size(); ++i) {
      ImVec2 p = ed::GetNodePosition(edIdForInputDef((int)i));
      cur->inputDefs[i].x = p.x;
      cur->inputDefs[i].y = p.y;
    }
    for (size_t j = 0; j < cur->outputDefs.size(); ++j) {
      ImVec2 p = ed::GetNodePosition(edIdForOutputDef((int)j));
      cur->outputDefs[j].x = p.x;
      cur->outputDefs[j].y = p.y;
    }
  }
}

}  // namespace sw::ui
