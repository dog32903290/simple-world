// ui/node_draw — see node_draw.h. Zone: ui. The per-child body of the canvas loop, moved
// out of editor_ui so the TiXL node skin (style + ports + slots + face) lives in one place
// and editor_ui stays focused on canvas/toolbar/inspector wiring.
#include "ui/node_draw.h"

#include <string>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/document.h"    // residentPathFor (idle fade resident lookup)
#include "app/frame_cook.h"  // residentNodeLastUpdatePass / currentFrameIndex (idle fade signal)
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec / pinId — a compound child resolves like an atomic (N1)
#include "ui/canvas_ids.h"  // childPinId: ed-facing banded pin id
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

void drawChild(const sw::SymbolChild& child) {
  // findSpec resolves atomics from the registry AND compounds from the dynamic spec table
  // (批次 3 N1) — a compound child gets pins/title exactly like an atomic node.
  const sw::NodeSpec* spec = sw::findSpec(child.symbolId);
  // 刀A · TiXL-parity skin: tint this node's background/outline/title by its category
  // color (first output's dataType, via ui/node_style). ed style is per-node; pop after
  // EndNode. Rounding/border kept small + thin for TiXL's flat-rectangle legacy look.
  // Selection (white) + hover (soft white) outlines — TiXL UiColors.Selection, category-
  // independent so a lit node reads the same regardless of type. Always pushed.
  //
  // V3: rounding = 5*CanvasScale, 0 if scale<0.5x (TiXL DrawNode.cs:126).
  // GetCurrentZoom() = InvScale; tixlScale = ViewScale = 1/InvScale.
  float tixlScale = (ed::GetCurrentZoom() > 0.0001f) ? (1.0f / ed::GetCurrentZoom()) : 1.0f;
  float rounding = nodeRounding(tixlScale);

  ed::PushStyleColor(ed::StyleColor_SelNodeBorder, ImGui::ColorConvertU32ToFloat4(nodeSelectedBorderColor()));
  // V4: hover blink border is drawn manually below (not via ed's HovNodeBorder) to animate.
  // Keep ed's built-in hover color dim so the custom blink rect dominates.
  ed::PushStyleColor(ed::StyleColor_HovNodeBorder, ImVec4(0, 0, 0, 0));  // invisible: manual blink
  int nColor = 2;
  if (spec) {
    // Idle fade (TiXL DrawNode.cs:42-50, DirtyFlag.cs:48 "editor-specific"):
    //   framesSince = currentFrameIndex - max(lastUpdatePass across outputs)
    //   idleFadeFactor = RemapAndClamp(framesSince, 0, 60, 1.0, 0.6)
    // Nodes not in the resident graph (unsaved, first-frame) return currentFrameIndex as their
    // lastUpdatePass (誠實邊界: treat as just-updated, no fade).
    const std::string rpath = doc::residentPathFor(child.id);
    const uint32_t curPass  = framecook::currentFrameIndex();
    const uint32_t lastPass = framecook::residentNodeLastUpdatePass(rpath.c_str());
    // framesSince as float; guard unsigned underflow (lastPass may exceed curPass on first frame)
    const float framesSince = (curPass >= lastPass) ? (float)(curPass - lastPass) : 0.0f;
    // TiXL RemapAndClamp(framesSince, 0, 60, 1.0, 0.6): 0 frames=1.0(active), 60+frames=0.6(idle)
    float idleFadeFactor = 1.0f - (framesSince / 60.0f) * 0.4f;
    if (idleFadeFactor < 0.6f) idleFadeFactor = 0.6f;
    if (idleFadeFactor > 1.0f) idleFadeFactor = 1.0f;
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImGui::ColorConvertU32ToFloat4(nodeBgColorIdle(*spec, idleFadeFactor)));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImGui::ColorConvertU32ToFloat4(nodeBorderColor(*spec)));
    nColor += 2;
  }
  ed::PushStyleVar(ed::StyleVar_NodeRounding, rounding);  // V3: zoom-aware (was 3.0f)
  ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);          // V5: TiXL DrawNode.cs:121-127 body fill only, no AddRect outline on normal nodes
  ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, 1.0f);  // V5: TiXL DrawNode.cs:147 AddRect default thickness = 1.0px (was 2.5f)
  ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderWidth, 0.0f);  // V4: manual blink; suppress ed's border
  ed::BeginNode(child.id);
  if (spec) ImGui::PushStyleColor(ImGuiCol_Text, nodeLabelColor(*spec));
  // Title = the instance's custom name, else the definition title (spec->title = def name via
  // specFromSymbol), else the raw symbolId for an unresolved ref (= TiXL Symbol.Child.ReadableName).
  const std::string defTitle = spec ? spec->title : child.symbolId;
  ImGui::TextUnformatted(childReadableName(child, defTitle).c_str());
  if (spec) ImGui::PopStyleColor();
  if (spec) {
    // TiXL port columns: inputs (type-colored slot + label) on the LEFT, outputs
    // (label + slot) on the RIGHT. Each pin wrapped in a group so eye records the whole
    // pin rect (marker + label) for hand pin-dragging. (Link pivot stays at content
    // centre for now; edge-pinned pivots are a later refinement.)
    auto pinRow = [&](int i, const sw::PortSpec& p) {
      // ed id is BANDED (ui/canvas_ids childPinId) so a child pin can never hash-collide with
      // a child NODE id; the eye label below stays on raw sw::pinId (driver-facing, stable).
      ed::BeginPin(sw::ui::childPinId(child.id, i),
                   p.isInput ? ed::PinKind::Input : ed::PinKind::Output);
      ImGui::BeginGroup();
      if (p.isInput) { drawSlot(typeColor(p.dataType)); ImGui::SameLine();
                       ImGui::TextUnformatted(p.name.c_str()); }
      else           { ImGui::TextUnformatted(p.name.c_str()); ImGui::SameLine();
                       drawSlot(typeColor(p.dataType)); }
      ImGui::EndGroup();
      ImVec2 pa = ed::CanvasToScreen(ImGui::GetItemRectMin());
      ImVec2 pb = ed::CanvasToScreen(ImGui::GetItemRectMax());
      sw::eye::recordRect(("pin:" + std::to_string(sw::pinId(child.id, i))).c_str(),
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
  drawNodeFace(child);  // 資料驅動 custom faces (node_faces.cpp kFaces table)
  ed::EndNode();

  // V4: hover blink border (TiXL DrawNode.cs:156 — UiColors.ForegroundFull.Fade(Blink)).
  // ed's built-in HovNodeBorder is suppressed (alpha=0 above); we draw manually so the
  // border animates per-frame with Blink. GetNodeBackgroundDrawList draws into the node's
  // background layer (still inside ed::Begin/End scope, valid after EndNode).
  if ((int)ed::GetHoveredNode().Get() == child.id) {
    ImDrawList* bgdl = ed::GetNodeBackgroundDrawList(child.id);
    if (bgdl) {
      ImVec2 npos = ed::GetNodePosition(child.id);
      ImVec2 nsz2 = ed::GetNodeSize(child.id);
      ImVec2 pMin = ed::CanvasToScreen(npos);
      ImVec2 pMax = ed::CanvasToScreen(ImVec2(npos.x + nsz2.x, npos.y + nsz2.y));
      float blink = blinkValue();  // TiXL: MathF.Sin(time*10)*0.5f+0.5f
      ImU32 blinkCol = IM_COL32(255, 255, 255, (int)(blink * 255.0f));
      bgdl->AddRect(pMin, pMax, blinkCol, rounding, 0, 1.0f);  // V5: TiXL DrawNode.cs:156 AddRect default thickness = 1.0px (was 2.0f)
    }
  }

  ed::PopStyleVar(4);
  ed::PopStyleColor(nColor);
  // eye: node body SCREEN rect via the node-editor's own position/size + transform.
  ImVec2 na = ed::CanvasToScreen(ed::GetNodePosition(child.id));
  ImVec2 nsz = ed::GetNodeSize(child.id);
  sw::eye::recordRect(("node:" + std::to_string(child.id)).c_str(),
                      na.x, na.y, na.x + nsz.x, na.y + nsz.y);
}

void drawBoundaryDef(const sw::SlotDef& def, int edNodeId, int pinId, bool isSource) {
  // Dimmer than operator nodes — boundary items are plumbing, not operators (TiXL renders
  // them as plain labelled boxes). Type-colored slot marker matches the wires it accepts.
  ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.13f, 0.13f, 0.16f, 0.9f));
  ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.45f, 0.45f, 0.5f, 0.6f));
  ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
  ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f);
  ed::BeginNode(edNodeId);
  const std::string& label = def.name.empty() ? def.id : def.name;
  ed::BeginPin(pinId, isSource ? ed::PinKind::Output : ed::PinKind::Input);
  ImGui::BeginGroup();
  if (isSource) {  // inputDef: label, slot on the right (it feeds the subgraph)
    ImGui::TextDisabled("in:");
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    drawSlot(typeColor(def.dataType));
  } else {  // outputDef: slot on the left (children drain into it)
    drawSlot(typeColor(def.dataType));
    ImGui::SameLine();
    ImGui::TextDisabled("out:");
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
  }
  ImGui::EndGroup();
  ImVec2 pa = ed::CanvasToScreen(ImGui::GetItemRectMin());
  ImVec2 pb = ed::CanvasToScreen(ImGui::GetItemRectMax());
  sw::eye::recordRect(("pin:" + std::to_string(pinId)).c_str(), pa.x, pa.y, pb.x, pb.y);
  ed::EndPin();
  ed::EndNode();
  ed::PopStyleVar(2);
  ed::PopStyleColor(2);
  ImVec2 na = ed::CanvasToScreen(ed::GetNodePosition(edNodeId));
  ImVec2 nsz = ed::GetNodeSize(edNodeId);
  sw::eye::recordRect(("node:" + std::to_string(edNodeId)).c_str(),
                      na.x, na.y, na.x + nsz.x, na.y + nsz.y);
}

}  // namespace sw::ui
