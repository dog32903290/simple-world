// ui/node_draw — see node_draw.h. Zone: ui. The per-child body of the canvas loop, moved
// out of editor_ui so the TiXL node skin (style + ports + slots + face) lives in one place
// and editor_ui stays focused on canvas/toolbar/inspector wiring.
#include "ui/node_draw.h"

#include <string>
#include <unordered_map>

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

// Per-frame map: ed input-pin id -> wire-terminus anchor in SCREEN space (left-edge midpoint of
// the input pin row, where the connection bezier arrives). Filled as pins are laid out, read by
// the canvas wire loop to place the connection arrow. Rebuilt every frame (clear before children).
std::unordered_map<int, ImVec2> g_inputPinAnchor;

// Record an input pin's wire terminus from its already-screen-space rect (pa=min, pb=max).
void recordInputPinAnchor(int edPinId, const ImVec2& pa, const ImVec2& pb) {
  g_inputPinAnchor[edPinId] = ImVec2(pa.x, (pa.y + pb.y) * 0.5f);
}

// A type-colored anchor marker. TiXL draws INPUT anchors as small triangles pointing into
// the node (DrawNode.cs:822-825: horizontal input, apex toward node, anchorWidth=3 half-height,
// anchorHeight=4 depth, ×CanvasScale) and OUTPUT anchors as filled circles with an outline ring
// (DrawNode.cs:918-920: radius (2+2*hoverFactor)*CanvasScale, non-hover hoverFactor=1 → r=4px).
// Our pins are inline markers inside the node body (not edge anchors like TiXL's MagGraph), so
// we keep the same reserved box (= the existing hit/label layout) and only swap the SHAPE; the
// geometry is faithful at TiXL's scale-1 size, centered in the box. `isInput` picks triangle vs
// circle; the triangle of an input points RIGHT (into the node, toward its label).
void drawSlot(ImU32 col, bool isInput) {
  const float s = 9.0f;  // reserved box (unchanged: hit area + label layout depend on it)
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 outline = IM_COL32(0, 0, 0, 130);
  if (isInput) {
    // TiXL input triangle: depth=anchorHeight=4, half-height=anchorWidth=3 (scale 1). Point RIGHT
    // into the node. Center the 4×6 triangle in the 9px box.
    const float halfH = 3.0f, depth = 4.0f;
    float cy = p.y + s * 0.5f;
    float x0 = p.x + (s - depth) * 0.5f;  // left edge of triangle
    ImVec2 a(x0, cy - halfH);             // top-left
    ImVec2 b(x0, cy + halfH);             // bottom-left
    ImVec2 c(x0 + depth, cy);             // right apex (points into node)
    dl->AddTriangleFilled(a, c, b, col);
    dl->AddTriangle(a, c, b, outline, 1.0f);
  } else {
    // TiXL output circle: r=4 (scale 1) filled (r-1) + outline ring (r).
    const float r = 4.0f;
    ImVec2 ctr(p.x + s * 0.5f, p.y + s * 0.5f);
    dl->AddCircleFilled(ctr, r - 1.0f, col, 12);
    dl->AddCircle(ctr, r, outline, 12, 1.0f);
  }
  ImGui::Dummy(ImVec2(s, s));
}

// L-G required-input indicator (TiXL MagGraphCanvas.DrawNode.cs:388/450 isMissing +
// :1142-1161 DrawMissingInputIndicator): a required input with NO incoming connection paints a
// small filled downward-pointing triangle in UiColors.StatusAttention to the LEFT of the pin row.
// `pa`/`pb` = the pin row's screen rect (min/max). TiXL places it at the node's left edge, vertically
// offset onto the input line; our pins are inline so we anchor just left of the row, centered on it.
void drawRequiredIndicator(const ImVec2& pa, const ImVec2& pb) {
  const ImU32 attention = IM_COL32(203, 19, 113, 255);  // TiXL UiColors.StatusAttention (#CB1371)
  ImDrawList* dl = ImGui::GetWindowDrawList();  // caller is inside the canvas; screen space
  const float h = 7.0f;                         // triangle base width (~TiXL s*0.4 at scale 1)
  float cx = pa.x - h * 0.6f;                   // just left of the pin row
  float cy = (pa.y + pb.y) * 0.5f;              // centered on the input line
  // Downward-pointing triangle (apex at bottom), matching TiXL's DrawMissingInputIndicator shape.
  dl->AddTriangleFilled(ImVec2(cx - h * 0.5f, cy - h * 0.5f),
                        ImVec2(cx + h * 0.5f, cy - h * 0.5f),
                        ImVec2(cx, cy + h * 0.5f), attention);
}

}  // namespace

void drawChild(const sw::SymbolChild& child, const sw::Symbol* parent) {
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
      if (p.isInput) { drawSlot(typeColor(p.dataType), /*isInput=*/true); ImGui::SameLine();
                       ImGui::TextUnformatted(p.name.c_str()); }
      else           { ImGui::TextUnformatted(p.name.c_str()); ImGui::SameLine();
                       drawSlot(typeColor(p.dataType), /*isInput=*/false); }
      ImGui::EndGroup();
      ImVec2 pa = ed::CanvasToScreen(ImGui::GetItemRectMin());
      ImVec2 pb = ed::CanvasToScreen(ImGui::GetItemRectMax());
      sw::eye::recordRect(("pin:" + std::to_string(sw::pinId(child.id, i))).c_str(),
                          pa.x, pa.y, pb.x, pb.y);
      if (p.isInput) recordInputPinAnchor(sw::ui::childPinId(child.id, (int)i), pa, pb);
      // L-G: a REQUIRED input with no incoming wire is "missing" → paint the StatusAttention marker
      // (TiXL isMissing = Relevancy.Required && ConnectionIn == null). connectionToInput is the SSOT
      // "is this input wired?" — null parent (no graph context) skips the test.
      if (p.isInput && p.required && parent &&
          !sw::connectionToInput(*parent, child.id, p.id))
        drawRequiredIndicator(pa, pb);
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
  if (isSource) {  // inputDef: label, slot on the right (it feeds the subgraph = a source/output anchor)
    ImGui::TextDisabled("in:");
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    drawSlot(typeColor(def.dataType), /*isInput=*/false);  // source feeds out → circle
  } else {  // outputDef: slot on the left (children drain into it = a sink/input anchor)
    drawSlot(typeColor(def.dataType), /*isInput=*/true);   // sink receives → triangle
    ImGui::SameLine();
    ImGui::TextDisabled("out:");
    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
  }
  ImGui::EndGroup();
  ImVec2 pa = ed::CanvasToScreen(ImGui::GetItemRectMin());
  ImVec2 pb = ed::CanvasToScreen(ImGui::GetItemRectMax());
  sw::eye::recordRect(("pin:" + std::to_string(pinId)).c_str(), pa.x, pa.y, pb.x, pb.y);
  if (!isSource) recordInputPinAnchor(pinId, pa, pb);  // outputDef sink = a wire target
  ed::EndPin();
  ed::EndNode();
  ed::PopStyleVar(2);
  ed::PopStyleColor(2);
  ImVec2 na = ed::CanvasToScreen(ed::GetNodePosition(edNodeId));
  ImVec2 nsz = ed::GetNodeSize(edNodeId);
  sw::eye::recordRect(("node:" + std::to_string(edNodeId)).c_str(),
                      na.x, na.y, na.x + nsz.x, na.y + nsz.y);
}

void clearConnectionArrowAnchors() { g_inputPinAnchor.clear(); }

void drawConnectionArrow(int inputPinId, unsigned int color) {
  auto it = g_inputPinAnchor.find(inputPinId);
  if (it == g_inputPinAnchor.end()) return;  // pin not drawn this frame (collapsed/unresolved)
  const ImVec2& t = it->second;              // wire terminus, screen space
  // TiXL MagGraphCanvas.DrawConnection.cs:226-231 (RightToLeft): anchorWidth=1.5*2=3,
  // anchorHeight=2*2=4; triangle = target + {(0,-aw),(ah,0),(0,+aw)} * CanvasScale * 1,
  // apex pointing RIGHT (+x) along the wire into the input slot. CanvasScale = view scale.
  const float scale = (ed::GetCurrentZoom() > 0.0001f) ? (1.0f / ed::GetCurrentZoom()) : 1.0f;
  const float aw = 3.0f * scale;  // half-height
  const float ah = 4.0f * scale;  // depth (apex reach)
  ImDrawList* dl = ImGui::GetWindowDrawList();  // caller wraps in ed::Suspend/Resume (screen space)
  dl->AddTriangleFilled(ImVec2(t.x, t.y - aw), ImVec2(t.x + ah, t.y), ImVec2(t.x, t.y + aw), color);
}

}  // namespace sw::ui
