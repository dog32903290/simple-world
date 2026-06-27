// ui/editor_ui — toolbar / node canvas (imgui draw); the Inspector lives in ui/inspector.cpp.
// Zone: ui. Depends on app(document) + runtime + verify(thin hook). Never the reverse.
//
// Lib-native canvas (批次 3, 照 TiXL GraphCanvas): the canvas renders the CURRENT Symbol's
// children/connections straight off doc::g_lib() — no flat Graph, no projection layer.
// Composition switch (N3) = same canvas, different symbol.
#include "ui/editor_ui.h"
#include "ui/annotation_draw.h"  // Annotation frames (批B/C): canvas draw + drag/resize/rename
#include "ui/canvas_ids.h"  // pin/link/boundary id scheme (mechanical split, rule 4)
#include "ui/combine_dialog.h"
#include "ui/connection_ops.h"  // applyConnection — shared wire-edit (drag + `connect` verb)
#include "ui/editor_ui_canvas_edit.h"  // processCanvasKeyboard / processCanvasDeletions (split, rule 4)
#include "ui/editor_ui_grid.h"  // drawCanvasBackgroundGrids (split, rule 4)
#include "ui/editor_ui_layout.h"  // syncCanvasLayout (split, rule 4)
#include "ui/fence_preview.h"  // live rubber-band selection highlight (TiXL SelectionFence)
#include "ui/keymap.h"
#include "ui/node_draw.h"
#include "ui/node_style.h"  // V1 grid color helpers, V2 connection line colors
#include "ui/theme.h"  // theme::canvasBackground() — themable canvas Bg (was hardcoded literal)
#include "ui/quick_add.h"   // Cmd+F palette (SearchGraph)

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "app/command.h"
#include "app/document.h"
#include "app/frame_cook.h"  // connection idle-fade signal (source node lastUpdatePass)
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec / pinId / pinNode (the int pin scheme rides on child ids)

namespace ed = ax::NodeEditor;

namespace sw::ui {

ax::NodeEditor::EditorContext* g_NodeEditor = nullptr;
int g_selectedNode = 0;
int g_pinnedNode = 0;       // view ⊥ graph (see editor_ui.h); set by ui/output_window.cpp
bool g_navPending = false;  // see editor_ui.h (set by canvas gestures + toolbar breadcrumbs)

// (anon-namespace helpers split out — mechanical, rule 4:
//   canvas background grid  → ui/editor_ui_grid.{h,cpp}        (drawCanvasBackgroundGrids)
//   keyboard + delete macro → ui/editor_ui_canvas_edit.{h,cpp} (processCanvasKeyboard/Deletions)
//   position sync + drag    → ui/editor_ui_layout.{h,cpp}      (syncCanvasLayout, owns g_dragStart)
//   pin/link/boundary ids   → ui/canvas_ids.{h,cpp})

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

  // Is the mouse over the canvas host (NOT over a floating Inspector/Output/Timeline window)?
  // Must be read HERE, while ##canvas_host is the current window — inside ed::Begin the current
  // window is the node-editor's internal child and this query returns the wrong answer. Fed to
  // the fence-selection preview's press gate (ui/fence_preview).
  const bool canvasHostHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

  sw::Symbol* cur = sw::doc::currentSymbol();

  ed::SetCurrentEditor(g_NodeEditor);
  // Kill imgui-node-editor's BUILT-IN gray grid (default StyleColor_Grid=(120,120,120,40)) —
  // it drew OVER the faithful TiXL grid below and dominated it (refuter-R-VK V3: the canvas
  // showed ed's gray cells, not the TiXL black ones). TiXL has no such built-in layer.
  ed::GetStyle().Colors[ed::StyleColor_Grid] = ImVec4(0, 0, 0, 0);
  // Canvas background fill (TiXL UiColors.CanvasBackground = (0.12,0.12,0.12,0.98),
  // UiColors.cs:29). imgui-node-editor fills its whole view with StyleColor_Bg
  // (default ImColor(60,60,70,200) — a bluish gray); override to the themed canvas color
  // (default = TiXL's flat dark gray; a registry theme can recolor it).
  ed::GetStyle().Colors[ed::StyleColor_Bg] = sw::ui::theme::canvasBackground();
  if (hostOpen && cur) {
    // V1: canvas background grid (TiXL DrawBackgroundGrids, Drawing.cs:377-396 multi-layer +
    // :398-426 single-layer loop). Called BEFORE ed::Begin so our draws land on the base
    // draw-list channel; the node-editor splits channels on top starting from Begin().
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
  sw::ui::clearConnectionArrowAnchors();  // rebuilt as children lay out → read by the arrow pass
  for (const sw::SymbolChild& child : cur->children) sw::ui::drawChild(child, cur);  // also fills anchors

  // The symbol's OWN external ports as movable boundary items (TiXL Input/Output canvas
  // items): inputDefs are sources inside, outputDefs are sinks. node_draw renders them.
  for (size_t i = 0; i < cur->inputDefs.size(); ++i)
    sw::ui::drawBoundaryDef(cur->inputDefs[i], edIdForInputDef((int)i),
                            boundaryPinId((int)i), /*isSource=*/true);
  for (size_t j = 0; j < cur->outputDefs.size(); ++j)
    sw::ui::drawBoundaryDef(cur->outputDefs[j], edIdForOutputDef((int)j),
                            boundaryPinId((int)(cur->inputDefs.size() + j)),
                            /*isSource=*/false);

  // Wires — boundary sides resolve to boundary pins. V2 color: TiXL DrawConnection.cs:32-42.
  std::vector<std::pair<int, ImU32>> arrowDraws;  // (input pin id, color) for the arrow pass below
  for (const sw::SymbolConnection& w : cur->connections) {
    int from = pinOfSlot(w.srcChild, w.srcSlot, /*isInput=*/false);
    int to = pinOfSlot(w.dstChild, w.dstSlot, /*isInput=*/true);
    if (from < 0 || to < 0) continue;  // unresolvable wire: cook tolerance, canvas tolerance
    uint64_t lid = linkIdOf(from, to);
    bool sel = ed::IsLinkSelected(lid) || (int)ed::GetHoveredLink().Get() == (int)lid;
    std::string dt = pinInfoOf(from).dataType;
    // Idle-fade (TiXL DrawConnection.cs:35): RemapAndClamp(FramesSinceLastUpdate,0,100,1,0).
    const std::string srcPath = doc::residentPathFor(w.srcChild);
    const uint32_t curPass  = framecook::currentFrameIndex();
    const uint32_t lastPass = framecook::residentNodeLastUpdatePass(srcPath.c_str());
    const float framesSince = (curPass >= lastPass) ? (float)(curPass - lastPass) : 0.0f;
    float idleFadeProgress = std::clamp(1.0f - (framesSince / 100.0f), 0.0f, 1.0f);  // 0..100 -> 1..0
    ImU32 lcol = sw::ui::connectionLineColor(dt, sel, idleFadeProgress);
    float lthick = sw::ui::connectionThickness(sel, idleFadeProgress);  // TiXL DrawConnection.cs:170
    ed::Link(lid, from, to, ImGui::ColorConvertU32ToFloat4(lcol), lthick);
    arrowDraws.push_back({to, lcol});
  }
  // Connection arrows (TiXL DrawConnection.cs:226-231 RightToLeft): triangle at each wire's input
  // terminus, pointing into the slot. Suspend → screen space, on top of nodes/wires.
  ed::Suspend();
  for (auto& a : arrowDraws) sw::ui::drawConnectionArrow(a.first, a.second);
  ed::Resume();

  // Annotation frames (批B/C): drawn + interacted over the canvas via ed::Suspend/Resume (fork-G,
  // see annotation_draw.cpp). Placed after the node/wire draw so its InvisibleButtons can claim
  // hover for drag/resize/rename before ed's own background gestures. Pushes its own undo commands.
  sw::ui::drawAnnotations(cur);

  // LIVE fence-selection preview (TiXL SelectionFence): while a rubber-band drag is in
  // progress on empty canvas, outline every node the box currently covers (= what releasing
  // now would select). The node-editor's built-in fence still commits the real selection on
  // release; this only adds the live highlight it lacks (preview-only fork, see fence_preview).
  // Drawn after nodes/wires/annotations so the outlines + box land on top.
  sw::ui::drawFenceSelectionPreview(cur, canvasHostHovered);

  // Create links by dragging pin -> pin (one input + one output, different nodes).
  if (ed::BeginCreate()) {
    // Port-drag -> empty canvas opens the type-pre-filtered quick-add (= TiXL PlaceholderCreation
    // QueryNewNode + PlaceHolderUi.Open inputFilter/outputFilter). Released on empty canvas, the
    // dragged pin's dataType narrows the palette to ops that can wire to it:
    //   drag FROM an OUTPUT pin -> the new node must RECEIVE that type -> inputFilter
    //       (= TiXL OpenForItemOutput, PlaceholderCreation.cs:148-152 inputFilter = output type);
    //   drag FROM an INPUT pin  -> the new node must PRODUCE that type -> outputFilter
    //       (= TiXL OpenForItemInput,  PlaceholderCreation.cs:266-269 outputFilter = input type).
    ed::PinId np;
    if (ed::QueryNewNode(&np) && np) {
      if (ed::AcceptNewItem()) {
        int pin = (int)np.Get();
        PinInfo pi = pinInfoOf(pin);
        std::string inFilter, outFilter;
        if (pi.valid) {
          // pinIsInput == canvas-sink. Dragging off a SOURCE (output) pin pre-filters by the new
          // node's INPUTS; dragging off a SINK (input) pin pre-filters by its OUTPUTS.
          if (pinIsInput(pin)) outFilter = pi.dataType;  // input pin -> need a matching OUTPUT
          else                 inFilter  = pi.dataType;  // output pin -> need a matching INPUT
        }
        ImVec2 drop = ed::ScreenToCanvas(ImGui::GetMousePos());
        sw::ui::openQuickAdd(drop.x, drop.y, inFilter, outFilter);
      }
    }
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
          // The wire-edit (MultiInput add / reconnect / exact-dup skip / push as one undo unit)
          // lives in ONE place now — ui::applyConnection — shared with the `connect` hand verb so
          // both entry points behave identically. The pin-drag guards above (different nodes, one
          // input + one output, matching dataType) already validated this `nw`.
          sw::ui::applyConnection(cur, nw);
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

  // Undo/Redo (Cmd+Z/Shift+Z) + Copy/Paste (Cmd+C/V). Body in ui/editor_ui_canvas_edit.cpp.
  sw::ui::processCanvasKeyboard();

  // Delete links / nodes / boundary defs (select + Delete key, or Backspace routed above).
  // Body in ui/editor_ui_canvas_edit.cpp.
  sw::ui::processCanvasDeletions(cur);

  // Context menu (批次 4): right-click a node -> "Combine into new symbol..." (the modal
  // itself draws after the editor scope, drawCombineDialog below).
  sw::ui::drawCanvasContextMenu();

  // Cmd+F quick-add palette (SearchGraph, TiXL FactoryKeyMap.cs:56 + SymbolBrowser.Draw).
  // Must be inside ed::Begin...End so ed::Suspend/Resume are valid (palette spawns via
  // spawnNodeAt which needs the canvas state). Suspension wraps the popup panel.
  sw::ui::drawQuickAdd();

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
      resetAnnotationGesture();  // annotation ids alias across symbols too (refuter-R-ANB 攻擊2)
    }
  }

  // Canvas position sync (relayout-seed / drag-move undo / passive read-back + boundary defs).
  // Body in ui/editor_ui_layout.cpp (owns the drag-start scratch + consumes g_navPending).
  sw::ui::syncCanvasLayout(cur, navThisFrame);

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
