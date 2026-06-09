// ui/node_draw — see node_draw.h. Zone: ui. The per-node body of the canvas loop, moved
// out of editor_ui so the TiXL node skin (style + ports + slots + face) lives in one place
// and editor_ui stays focused on canvas/toolbar/inspector wiring.
#include "ui/node_draw.h"

#include <string>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "runtime/graph.h"
#include "ui/node_faces.h"
#include "ui/node_style.h"
#include "verify/eye/eye.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {
namespace {

// A type-colored slot marker (TiXL's left/right port bars). Draws a small filled square in
// the node's draw list and reserves its box so SameLine+label sit beside it.
void drawSlot(ImU32 col) {
  const float s = 9.0f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), col, 2.0f);
  dl->AddRect(p, ImVec2(p.x + s, p.y + s), IM_COL32(0, 0, 0, 130), 2.0f);
  ImGui::Dummy(ImVec2(s, s));
}

}  // namespace

void drawNode(const sw::Node& node) {
  const sw::NodeSpec* spec = sw::findSpec(node.type);
  // 刀A · TiXL-parity skin: tint this node's background/outline/title by its category
  // color (first output's dataType, via ui/node_style). ed style is per-node; pop after
  // EndNode. Rounding/border kept small + thin for TiXL's flat-rectangle legacy look.
  if (spec) {
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImGui::ColorConvertU32ToFloat4(nodeBgColor(*spec)));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImGui::ColorConvertU32ToFloat4(nodeBorderColor(*spec)));
  }
  ed::PushStyleVar(ed::StyleVar_NodeRounding, 3.0f);
  ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);
  ed::BeginNode(node.id);
  if (spec) ImGui::PushStyleColor(ImGuiCol_Text, nodeLabelColor(*spec));
  ImGui::TextUnformatted(spec ? spec->title.c_str() : node.type.c_str());
  if (spec) ImGui::PopStyleColor();
  if (spec) {
    // TiXL port columns: inputs (type-colored slot + label) on the LEFT, outputs
    // (label + slot) on the RIGHT. Each pin wrapped in a group so eye records the whole
    // pin rect (marker + label) for hand pin-dragging. (Link pivot stays at content
    // centre for now; edge-pinned pivots are a later refinement.)
    auto pinRow = [&](int i, const sw::PortSpec& p) {
      ed::BeginPin(sw::pinId(node.id, i), p.isInput ? ed::PinKind::Input : ed::PinKind::Output);
      ImGui::BeginGroup();
      if (p.isInput) { drawSlot(typeColor(p.dataType)); ImGui::SameLine();
                       ImGui::TextUnformatted(p.name.c_str()); }
      else           { ImGui::TextUnformatted(p.name.c_str()); ImGui::SameLine();
                       drawSlot(typeColor(p.dataType)); }
      ImGui::EndGroup();
      ImVec2 pa = ed::CanvasToScreen(ImGui::GetItemRectMin());
      ImVec2 pb = ed::CanvasToScreen(ImGui::GetItemRectMax());
      sw::eye::recordRect(("pin:" + std::to_string(sw::pinId(node.id, i))).c_str(),
                          pa.x, pa.y, pb.x, pb.y);
      ed::EndPin();
    };
    int nInputs = 0;
    for (const sw::PortSpec& p : spec->ports) if (!p.pinless && p.isInput) ++nInputs;
    ImGui::BeginGroup();  // left = inputs
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (!spec->ports[i].pinless && spec->ports[i].isInput) pinRow((int)i, spec->ports[i]);
    ImGui::EndGroup();
    if (nInputs > 0) ImGui::SameLine(0.0f, 28.0f);
    ImGui::BeginGroup();  // right = outputs
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (!spec->ports[i].pinless && !spec->ports[i].isInput) pinRow((int)i, spec->ports[i]);
    ImGui::EndGroup();
  }
  drawNodeFace(node);  // 資料驅動 custom faces (node_faces.cpp kFaces table)
  ed::EndNode();
  ed::PopStyleVar(2);
  if (spec) ed::PopStyleColor(2);
  // eye: node body SCREEN rect via the node-editor's own position/size + transform.
  ImVec2 na = ed::CanvasToScreen(ed::GetNodePosition(node.id));
  ImVec2 nsz = ed::GetNodeSize(node.id);
  sw::eye::recordRect(("node:" + std::to_string(node.id)).c_str(),
                      na.x, na.y, na.x + nsz.x, na.y + nsz.y);
}

}  // namespace sw::ui
