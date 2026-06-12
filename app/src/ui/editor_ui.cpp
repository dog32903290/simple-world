// ui/editor_ui — toolbar / node canvas (imgui draw); the Inspector lives in ui/inspector.cpp.
// Zone: ui. Depends on app(document) + runtime + verify(thin hook). Never the reverse.
//
// Lib-native canvas (批次 3, 照 TiXL GraphCanvas): the canvas renders the CURRENT Symbol's
// children/connections straight off doc::g_lib — no flat Graph, no projection layer.
// Composition switch (N3) = same canvas, different symbol.
#include "ui/editor_ui.h"
#include "ui/canvas_ids.h"  // pin/link/boundary id scheme (mechanical split, rule 4)
#include "ui/combine_dialog.h"
#include "ui/copy_paste_ui.h"
#include "ui/keymap.h"
#include "ui/node_draw.h"
#include "ui/node_style.h"  // V1 grid color helpers, V2 connection line colors

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec / pinId / pinNode (the int pin scheme rides on child ids)

namespace ed = ax::NodeEditor;

namespace sw::ui {

ax::NodeEditor::EditorContext* g_NodeEditor = nullptr;
int g_selectedNode = 0;
int g_pinnedNode = 0;       // view ⊥ graph (see editor_ui.h); set by ui/output_window.cpp
bool g_navPending = false;  // see editor_ui.h (set by canvas gestures + toolbar breadcrumbs)

namespace {
// (pin/link/boundary id scheme moved to ui/canvas_ids.{h,cpp} — mechanical split, rule 4)

// 拖動暫存：child id -> 拖動開始時的座標。空 == 沒在拖。
std::map<int, ImVec2> g_dragStart;

// V1: TiXL MagGraphCanvas.Drawing.cs:398-426 — draw 1px AddRectFilled lines on a
// screen-aligned grid snapped to canvas coords. Must be called AFTER ed::SetCurrentEditor
// but BEFORE ed::Begin (draw list not yet split, so our draws go to the base channel
// which the editor's own layers sit on top of).
//
// gridSizeCanvas: canvas-unit grid cell size (35 for fine, 175 for coarse).
// color: already-computed ImU32 (CanvasGrid alpha pre-multiplied by zoom ramp).
void drawBackgroundGridLayer(ImDrawList* dl, float gridSizeCanvas, ImU32 color) {
  if (!dl) return;
  ImVec2 winPos  = ImGui::GetWindowPos();
  ImVec2 winSize = ImGui::GetWindowSize();

  // Canvas-space top-left of the visible window, snapped to grid.
  ImVec2 tlCanvas = ed::ScreenToCanvas(winPos);
  float alignedX = std::floor(tlCanvas.x / gridSizeCanvas) * gridSizeCanvas;
  float alignedY = std::floor(tlCanvas.y / gridSizeCanvas) * gridSizeCanvas;

  // Screen-space position of the snapped top-left grid line.
  ImVec2 tlScreen = ed::CanvasToScreen(ImVec2(alignedX, alignedY));

  // Scale: screen pixels per canvas unit. GetCurrentZoom() = InvScale = 1/ViewScale.
  float zoom = ed::GetCurrentZoom();
  float screenCell = (zoom > 0.0001f) ? (gridSizeCanvas / zoom) : gridSizeCanvas;

  // Vertical lines (x-axis).
  float countX = winSize.x / screenCell + 2.0f;
  for (int ix = 0; ix < 200 && ix <= (int)countX; ++ix) {
    float x = std::floor(tlScreen.x + ix * screenCell);
    dl->AddRectFilled(ImVec2(x, winPos.y), ImVec2(x + 1, winPos.y + winSize.y), color);
  }

  // Horizontal lines (y-axis).
  float countY = winSize.y / screenCell + 2.0f;
  for (int iy = 0; iy < 200 && iy <= (int)countY; ++iy) {
    float y = std::floor(tlScreen.y + iy * screenCell);
    dl->AddRectFilled(ImVec2(winPos.x, y), ImVec2(winPos.x + winSize.x, y + 1), color);
  }
}

// RemapAndClamp: maps value in [inMin,inMax] → [outMin,outMax], clamped.
// TiXL MathUtils.RemapAndClamp equivalent.
float remapClamp(float v, float inMin, float inMax, float outMin, float outMax) {
  if (inMax == inMin) return outMin;
  float t = (v - inMin) / (inMax - inMin);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return outMin + t * (outMax - outMin);
}

// V1: Draw fine + coarse background grids (TiXL DrawBackgroundGrids, Drawing.cs:377-396).
// MagGraphItem.GridSize = (140,35), minSize = 35.
// UiColors.CanvasGrid = (0,0,0,0.15), Fade(rampVal) multiplies alpha by rampVal.
// fineGrid ramp:  Scale.X in [0.5,2.0] → opacity [0, 0.25]
// roughGrid ramp: Scale.X in [0.1,2.0] → opacity [0, 0.25]
void drawCanvasBackgroundGrids() {
  // tixlScale = ViewScale = 1/InvScale; GetCurrentZoom() returns InvScale.
  float invScale = ed::GetCurrentZoom();
  float tixlScale = (invScale > 0.0001f) ? (1.0f / invScale) : 1.0f;
  const float kGridCanvas = 35.0f;   // min(140,35) canvas units per fine cell
  const float kMaxOpacity = 0.25f;
  const float kCanvasGridAlpha = 0.15f;  // UiColors.CanvasGrid base alpha

  ImDrawList* dl = ImGui::GetWindowDrawList();

  // Fine grid (TiXL: Scale.X remapped [0.5,2.0] → [0,0.25])
  float fineRamp = remapClamp(tixlScale, 0.5f, 2.0f, 0.0f, kMaxOpacity);
  if (fineRamp > 0.01f) {
    float alpha = kCanvasGridAlpha * fineRamp;  // Fade(): new alpha = base.alpha * f
    ImU32 col = IM_COL32(0, 0, 0, (int)(alpha * 255.0f));
    drawBackgroundGridLayer(dl, kGridCanvas, col);
  }

  // Coarse grid (TiXL: Scale.X remapped [0.1,2.0] → [0,0.25])
  float roughRamp = remapClamp(tixlScale, 0.1f, 2.0f, 0.0f, kMaxOpacity);
  if (roughRamp > 0.01f) {
    float alpha = kCanvasGridAlpha * roughRamp;
    ImU32 col = IM_COL32(0, 0, 0, (int)(alpha * 255.0f));
    drawBackgroundGridLayer(dl, kGridCanvas * 5.0f, col);
  }
}

}  // namespace

void drawNodeCanvas() {
  // The node canvas IS the main workspace: a borderless host window pinned to
  // fill the whole viewport, node editor drawn inside it. Named windows (e.g.
  // Inspector) float on top because this host uses NoBringToFrontOnFocus.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool hostOpen = ImGui::Begin("##canvas_host", nullptr, hostFlags);
  ImGui::PopStyleVar();

  sw::Symbol* cur = sw::doc::currentSymbol();

  ed::SetCurrentEditor(g_NodeEditor);
  if (hostOpen && cur) {
    // V1: canvas background grid (TiXL DrawBackgroundGrids, Drawing.cs:377-396).
    // Called BEFORE ed::Begin so our draws land on the base draw-list channel; the
    // node-editor splits channels on top starting from Begin().
    drawCanvasBackgroundGrids();

    ed::Begin("canvas");

  // Keyboard shortcut dispatch (K0 table: Space/L/J/K/Shift+←→/Cmd+D/F).
  // Must be called with the node editor current (NavigateToSelection etc. need it)
  // and inside the canvas host window (focus/hover queries read its state).
  // Guard: processFrame() skips everything when io.WantTextInput (text widgets).
  sw::ui::km::processFrame();

  // Draw every child of the CURRENT symbol (= TiXL GraphCanvas renders Symbol children).
  // The TiXL skin (category color, port columns, type-colored slots, custom face) + eye
  // rects live in ui/node_draw. The live preview is NOT welded to any node body — it lives
  // in the Output window (ui/output_window.cpp), which shows ANY pinned node's output
  // (view ⊥ graph; OUTPUT_PIN_VIEWER_CONTRACT §4-A).
  for (const sw::SymbolChild& child : cur->children) sw::ui::drawChild(child);

  // The symbol's OWN external ports as movable boundary items (TiXL Input/Output canvas
  // items): inputDefs are sources inside, outputDefs are sinks. node_draw renders them.
  for (size_t i = 0; i < cur->inputDefs.size(); ++i)
    sw::ui::drawBoundaryDef(cur->inputDefs[i], edIdForInputDef((int)i),
                            boundaryPinId((int)i), /*isSource=*/true);
  for (size_t j = 0; j < cur->outputDefs.size(); ++j)
    sw::ui::drawBoundaryDef(cur->outputDefs[j], edIdForOutputDef((int)j),
                            boundaryPinId((int)(cur->inputDefs.size() + j)),
                            /*isSource=*/false);

  // Wires — boundary-sentinel sides resolve to the boundary items' pins.
  // V2: TiXL DrawConnection.cs:32-42 — link color = ConnectionLines variation (b1,s1,op0.8)
  // of the type's base color; selected/hovered uses OperatorLabel variation (brighter) + 2px.
  for (const sw::SymbolConnection& w : cur->connections) {
    int from = pinOfSlot(w.srcChild, w.srcSlot, /*isInput=*/false);
    int to = pinOfSlot(w.dstChild, w.dstSlot, /*isInput=*/true);
    if (from < 0 || to < 0) continue;  // unresolvable wire: cook tolerance, canvas tolerance
    uint64_t lid = linkIdOf(from, to);
    bool sel = ed::IsLinkSelected(lid) || (int)ed::GetHoveredLink().Get() == (int)lid;
    std::string dt = pinInfoOf(from).dataType;
    ImU32 lcol = sw::ui::connectionLineColor(dt, sel);
    float lthick = sel ? 3.0f : 1.5f;  // TiXL DrawConnection.cs:170 — hover/sel +2px
    ed::Link(lid, from, to, ImGui::ColorConvertU32ToFloat4(lcol), lthick);
  }

  // Create links by dragging pin -> pin (one input + one output, different nodes).
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b) && a && b) {
      int pa = (int)a.Get(), pb = (int)b.Get();
      bool ia = pinIsInput(pa), ib = pinIsInput(pb);
      // NOTE: both boundary item kinds share pin-childId 0, so this same-node guard also
      // blocks a direct inputDef->outputDef passthrough wire (legal in TiXL, rare; named
      // limitation until someone needs it).
      if (pa != pb && ia != ib && pinChildId(pa) != pinChildId(pb)) {
        // Reject if the two pins' dataType differ (Float↔Float, buffer↔same-type).
        auto portTypeOf = [](int pin) -> std::string {
          PinInfo p = pinInfoOf(pin);
          return p.valid ? p.dataType : "";
        };
        if (portTypeOf(pa) != portTypeOf(pb)) {
          ed::RejectNewItem();
        } else if (ed::AcceptNewItem()) {
          int from = ia ? pb : pa;  // output pin
          int to = ia ? pa : pb;    // input pin
          sw::SymbolConnection nw;
          nw.srcChild = pinChildId(from);
          nw.srcSlot = pinInfoOf(from).slotId;
          nw.dstChild = pinChildId(to);
          nw.dstSlot = pinInfoOf(to).slotId;
          const sw::SymbolConnection* old =
              sw::connectionToInput(*cur, nw.dstChild, nw.dstSlot);
          if (old && old->srcChild == nw.srcChild && old->srcSlot == nw.srcSlot) {
            // already wired to this exact source — nothing to do
          } else if (old) {
            // reconnect: remove the input's old wire, add the new one, as one undo unit
            auto macro = std::make_unique<sw::MacroCommand>("Reconnect");
            macro->add(std::make_unique<sw::DeleteWiresCommand>(
                sw::doc::g_lib, cur->id, std::vector<sw::SymbolConnection>{*old}));
            macro->add(std::make_unique<sw::AddWireCommand>(sw::doc::g_lib, cur->id, nw));
            sw::g_commands.push(std::move(macro));
            sw::doc::g_status = "reconnected";
          } else {
            sw::g_commands.push(std::make_unique<sw::AddWireCommand>(sw::doc::g_lib, cur->id, nw));
            sw::doc::g_status = "linked";
          }
        }
      } else {
        ed::RejectNewItem();
      }
    }
    ed::EndCreate();
  }

  // macOS: the key labelled "delete" sends Backspace, but the node editor only
  // listens for forward-Delete (ImGuiKey_Delete). Route Backspace into the
  // editor's manual-delete queue so the BeginDelete handler below picks it up.
  if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Backspace) &&
      !ImGui::GetIO().WantTextInput) {
    ed::NodeId nodes[128];
    int nn = ed::GetSelectedNodes(nodes, 128);
    for (int i = 0; i < nn; ++i) ed::DeleteNode(nodes[i]);
    ed::LinkId links[128];
    int nl = ed::GetSelectedLinks(links, 128);
    for (int i = 0; i < nl; ++i) ed::DeleteLink(links[i]);
  }

  // Undo / Redo: Cmd+Z / Cmd+Shift+Z (macOS).
  // IMPORTANT: imgui's ConfigMacOSXBehaviors (default on __APPLE__) swaps
  // Cmd->Ctrl inside AddKeyEvent, so a physical Cmd press lands in io.KeyCtrl,
  // NOT io.KeySuper. Detect Cmd via io.KeyCtrl. (Verified by --selftest-hand.)
  {
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
        macro->add(std::make_unique<sw::DeleteWiresCommand>(sw::doc::g_lib, cur->id,
                                                            standaloneWires));
      if (!delNodes.empty())
        macro->add(std::make_unique<sw::DeleteChildrenCommand>(sw::doc::g_lib, cur->id, delNodes));
      // Def removals LAST (mirror TiXL Modifications.cs:184-191: children deleted first so the def
      // scrub only touches connections still present — though our snapshot captures whatever it hits).
      for (const auto& [slot, isInput] : delDefs)
        macro->add(std::make_unique<sw::DeleteInputOrOutputDefCommand>(sw::doc::g_lib, cur->id, slot,
                                                                       isInput));
      sw::g_commands.push(std::move(macro));
      // Removing a def is a contract change — SAY so (柏為: silent edits read as broken). ASCII only.
      sw::doc::g_status = delDefs.empty() ? "deleted" : "removed boundary def (def edit, broadcasts)";
    }
  } else {
    ed::EndDelete();
  }

  // Context menu (批次 4): right-click a node -> "Combine into new symbol..." (the modal
  // itself draws after the editor scope, drawCombineDialog below).
  sw::ui::drawCanvasContextMenu();

  // Navigation gestures (TiXL MagGraph GraphStates.cs): double-click a COMPOUND child ->
  // enter its subgraph; double-click the background -> up one level. The doc refuses
  // atomic children, so plain double-clicks on operators stay inert. `cur` still points at
  // the symbol we LEFT for the rest of this frame — so the layout section below is skipped
  // this frame (g_relayout stays pending) and runs next frame against the new symbol.
  bool navThisFrame = false;
  {
    int dn = (int)ed::GetDoubleClickedNode().Get();
    if (dn > 0) {
      navThisFrame = sw::doc::pushComposition(dn);
    } else if (ed::IsBackgroundDoubleClicked()) {
      navThisFrame = sw::doc::popComposition();
    }
    if (navThisFrame) {
      g_navPending = true;
      // Pin/selection are bare child ids — across a composition switch they'd ALIAS a
      // same-id child of the new symbol (viewport silently shows the wrong node, refuter
      // N3 B2/S3). Clear immediately; ed selection cleared now too (capture below reads 0).
      g_pinnedNode = 0;
      g_selectedNode = 0;
      ed::ClearSelection();
    }
  }

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
        sw::g_commands.push(std::make_unique<sw::MoveChildrenCommand>(sw::doc::g_lib, cur->id, moves));
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

  // Capture selection (while the editor is current) for the Inspector.
  ed::NodeId sel[1];
  int nsel = ed::GetSelectedNodes(sel, 1);
  g_selectedNode = (nsel > 0) ? (int)sel[0].Get() : 0;

    ed::End();
  }
  ed::SetCurrentEditor(nullptr);

  sw::ui::drawCombineDialog();  // modal naming dialog (canvas host window scope)

  ImGui::End();  // ##canvas_host
}

}  // namespace sw::ui
