// ui/output_window — TiXL OutputWindow + ViewSelectionPinning, faithful + minimal.
// Zone: ui. Depends on app(document) + runtime(graph) + the shell's previewTexture seam.
// Never mutates the graph; the pin (g_pinnedNode) is session-only state, never serialized.
//
// view ⊥ graph: "what I'm building" (the graph) and "what I'm looking at" (the pin) are
// two different things. The cook target is decided by the pin in the shell (main.cpp); this
// window only drives the pin and shows the result. OUTPUT_PIN_VIEWER_CONTRACT §4-A / §5.
//
// This file is the COORDINATOR: it lays out the toolbar + drives the viewport. The heavy sub-systems
// live in their own TUs (one file, one job): output_window_canvas.{h,cpp} (pan/zoom/fit math),
// output_window_resolution.{h,cpp} (resolution preset table + apply), output_window_persist.{h,cpp}
// (out-window-persistence: capture/restore pin/res/bg to the app store document.cpp saves per project).
#include "ui/output_window.h"

#include <cmath>
#include <string>

#include "imgui.h"

#include "app/document.h"
#include "app/snapshot.h"  // saveSnapshot: product Output→PNG (TiXL OutputWindow Icon.Snapshot)
#include "ui/editor_ui.h"  // g_pinnedNode (the session pin) + g_selectedNode (what Pin grabs)
#include "ui/output_window_canvas.h"      // the aspect-correct image canvas (split out)
#include "ui/output_window_orbit.h"       // phase-C 3D orbit gesture: Viewer / Locked-to-Op (split out)
#include "ui/output_window_persist.h"     // out-window-persistence: capture/restore view state (split out)
#include "ui/output_window_resolution.h"  // the Output resolution selector (split out)
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec (a compound child resolves like an atomic, N1)
#include "verify/eye/eye.h"  // one-line hook: record the Pin button rect for the hand

// The shell renders the live preview into a texture and exposes it through these STABLE
// accessors (defined in main.cpp). previewTexture() is nullptr until the first frame; its
// native pixel size drives the aspect-correct fit (Metal stays out of the ui zone).
namespace MTL { class Texture; }
namespace sw {
MTL::Texture* previewTexture();
bool previewTextureSize(int& w, int& h);
// Output-resolution-selector seam (S1-ui): the combo writes the frame render-size override the
// cook seeds into RequestedResolution. The setters live in output_window_resolution.cpp; the
// Fill baseline (TiXL GetWindowSize role) is read here. Defined in main.cpp (shell owns g_pointGraph).
bool outputWindowResolution(int& w, int& h);
// View background-color seam (TiXL OutputWindow._backgroundColor → EvaluationContext.BackgroundColor): the
// picker (Command views only) forwards its color to the terminal Command executor's clear. Defined in main.cpp.
void setOutputBackgroundColor(float r, float g, float b, float a);
void clearOutputBackgroundColor();
}  // namespace sw

namespace sw::ui {

namespace {
// A child's primary output type ("Points" | "ParticleForce" | "Float"). Empty if it has no output
// port (a draw node like DrawPoints) or no spec — TiXL's typed OutputUi lookup (none -> nothing to show).
std::string outputTypeOf(const sw::SymbolChild* c) {
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s) return "";
  for (const sw::PortSpec& p : s->ports)
    if (!p.isInput) return p.dataType;
  return "";
}

// View background color (TiXL OutputWindow._backgroundColor, OutputWindow.cs:634). View state persisted
// per project via output_window_persist (out-window-persistence). Default = TiXL's 0.1 grey (Command clears here).
float g_viewBackground[4] = {0.1f, 0.1f, 0.1f, 1.0f};
}  // namespace

void drawOutputWindow() {
  // out-window-persistence: on project open/new, restore saved view state into the globals BEFORE the
  // toolbar reads them (this frame shows the restored pin/res/bg). Capture mirror at end of function.
  restoreOutputWindowStateIfPending(g_pinnedNode, g_selectedResIndex, g_viewBackground);

  // Docked by ui/layout_dock (default = first tab of the lower-right stack). No manual
  // SetNextWindowPos/Size — DockBuilder owns first placement so resize re-flows it (柏為 drags freely).
  ImGui::Begin("Output");

  // Drop a stale pin (the pinned node was deleted) -> resume following selection.
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (g_pinnedNode != 0 && (!cur || !sw::childById(*cur, g_pinnedNode))) g_pinnedNode = 0;
  const bool pinned = g_pinnedNode != 0;
  const sw::SymbolChild* pinnedNode = pinned ? sw::childById(*cur, g_pinnedNode) : nullptr;

  // Resolve WHAT the viewport shows ONCE (main.cpp cook-target priority: pinned > selected > terminal). The
  // bg picker (Command-only) + the type block below read this. Command view = bare terminal OR a Command op.
  const sw::SymbolChild* viewNode = pinnedNode;
  if (!viewNode && g_selectedNode != 0 && cur) viewNode = sw::childById(*cur, g_selectedNode);
  const sw::NodeSpec* vs = viewNode ? sw::findSpec(viewNode->symbolId) : nullptr;
  const std::string outType = viewNode ? outputTypeOf(viewNode) : "";
  const bool viewIsCommand = !viewNode || outType == "Command";

  // --- toolbar: wraps to a new row in a narrow dock (imgui_demo.cpp "Wrapping" idiom — check the
  // PREVIOUS item's own right edge, not GetContentRegionAvail(), which already reads a fresh line). ---
  const float toolbarRightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  auto sameLineIfFits = [toolbarRightEdge](float nextWidth) {
    const float nextX2 = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth;
    if (nextX2 < toolbarRightEdge) ImGui::SameLine();
  };

  // --- Output resolution selector FIRST (TiXL ResolutionHandling.DrawSelector, OutputWindow.cs:316):
  // moved to the front so it's laid out before anything else can push it off a narrow panel (the
  // control 柏為 needs most often). Picks the frame render size the cook seeds into RequestedResolution;
  // Fill follows the window, a preset retargets a Texture terminal. g_selectedResIndex is view state,
  // persisted per project via output_window_persist. Record the combo's rect from PRE-widget geometry:
  // while its popup is open the "last item" is the popup's last Selectable, so recording after
  // BeginCombo would hand the map that instead (inspector.cpp:190 refuter N4 #2). Open rows are
  // addressed by the eye's popup walker as popup_item:<combo-window>:<row>.
  const ImVec2 comboPos = ImGui::GetCursorScreenPos();
  ImGui::SetNextItemWidth(110.0f);
  const float comboW = 110.0f;
  if (ImGui::BeginCombo("##OutputResolution", kResPresets[g_selectedResIndex].title)) {
    // Group header (TiXL ResolutionHandling.cs:22 CustomComponents.MenuGroupHeader): a non-
    // selectable label row above the presets. Faithful, AND it occupies the popup's top slot —
    // which ImGui overlaps with the combo box — so every SELECTABLE preset (incl. Fill) sits in a
    // row clear of the combo button, keeping each one hand-clickable (no row-0/header collision).
    ImGui::TextDisabled("Output Resolution");
    for (int i = 0; i < kResPresetCount; ++i) {
      const bool sel = (i == g_selectedResIndex);
      if (ImGui::Selectable(kResPresets[i].title, sel) && i != g_selectedResIndex) {
        g_selectedResIndex = i;                  // record the pick (TiXL selectedResolution = res)
        int winW = 0, winH = 0;
        sw::outputWindowResolution(winW, winH);  // Fill baseline for the aspect-fit presets
        applyResolutionSelection(winW, winH);    // ON CHANGE: Fill clears, a preset sets — no churn
      }
      // Give each open row a '#'-free named rect for the hand (output_res_row:<i>): the generic eye
      // walker emits "popup_item:##Combo_00:<row>", but the .scn runner strips '#', so that label
      // can't be addressed. Row index i == geometric order (we omit the focus-scroll, see NB below).
      {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        char rowLbl[32];
        std::snprintf(rowLbl, sizeof(rowLbl), "output_res_row:%d", i);
        sw::eye::recordRect(rowLbl, mn.x, mn.y, mx.x, mx.y);
      }
      // NB: deliberately NOT calling SetItemDefaultFocus() — it auto-scrolls the popup to align the
      // selected item with the combo box, shifting every row's screen pos and breaking the eye's
      // geometric walker. The short list is fully visible, so row N == table order N stays true.
    }
    ImGui::EndCombo();
  } else if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Adjust requested output resolution");  // TiXL tooltip (ResolutionHandling.cs:56)
  }
  sw::eye::recordRect("output_resolution_combo", comboPos.x, comboPos.y,
                      comboPos.x + comboW, comboPos.y + ImGui::GetFrameHeight());
  sameLineIfFits(70.0f);  // Custom row's W x H fields, only drawn when Custom is picked
  drawCustomResolutionEditor();  // Custom row only: inline W/H fields (split into the resolution module)

  // --- Pin / switch / Unpin (TiXL Icon.Pin + PinSelectionToView). Unpinned, the viewport FOLLOWS the
  // selected node; Pin LOCKS it. One button: a differently-selected node -> "Pin selected"; else,
  // if pinned -> "Unpin" resumes following selection. ---
  sameLineIfFits(100.0f);  // "Pin selected" is the widest label this button ever shows
  const bool canPinSelection = g_selectedNode != 0 && g_selectedNode != g_pinnedNode;
  if (ImGui::Button(canPinSelection ? "Pin selected" : (pinned ? "Unpin" : "Pin selected"))) {
    if (canPinSelection) g_pinnedNode = g_selectedNode;  // lock / switch to the active op
    else if (pinned) g_pinnedNode = 0;                   // resume following selection
  }
  sw::eye::recordItem("output_pin_btn");             // eye: hand off this button's screen rect

  // Fit / 1:1 view-mode buttons (TiXL ImageOutputCanvas.SetViewMode; both recompute below once size known).
  sameLineIfFits(40.0f);
  const bool wantFit = ImGui::Button("Fit");
  sw::eye::recordItem("output_fit_btn");
  sameLineIfFits(40.0f);
  const bool wantPixel = ImGui::Button("1:1");
  sw::eye::recordItem("output_pixel_btn");

  // Snapshot: save the current Output render to a PNG the user keeps (TiXL OutputWindow.cs:332
  // Icon.Snapshot → RenderProcess.TryRenderScreenShot). Writes <project>/Screenshots/<stamp>.png
  // directly — no save dialog (faithful to TiXL, which has none for screenshots). Disabled when
  // there is no preview texture yet (nothing to capture), mirroring TiXL's MainOutputType==null
  // disabled state. ui → app(saveSnapshot) → platform(image_save): no Metal in this zone.
  sameLineIfFits(80.0f);
  {
    MTL::Texture* snapTex = sw::previewTexture();
    ImGui::BeginDisabled(snapTex == nullptr);
    if (ImGui::Button("Snapshot")) {
      std::string path;
      const std::string written = sw::saveSnapshot(snapTex, &path);
      sw::doc::g_status = written.empty() ? ("snapshot failed -> " + path)
                                          : ("snapshot saved -> " + written);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Save screenshot");  // TiXL tooltip (OutputWindow.cs:338)
  }
  sw::eye::recordItem("output_snapshot_btn");  // eye: hand off this button's screen rect

  // --- Reset View (TiXL CameraSelectionHandling.ResetView → CameraInteraction.ResetView, cs:402-405):
  // in Viewer mode clears the session ViewCamera override back to the TiXL default pose. A 3D-view control,
  // so only shown for a Command/Points view (a Texture2D view has no orbit camera to reset). Locked-to-Op's
  // reset is the node's own param reset (dossier follow-up), so this button only touches the Viewer camera. ---
  const bool is3DView = !viewNode || outType == "Command" || outType == "Points";
  if (is3DView) {
    sameLineIfFits(70.0f);
    if (ImGui::Button("Reset View")) sw::ui::resetOutputViewToDefault();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset the viewer camera to its default position");
    sw::eye::recordItem("output_reset_view_btn");
  }

  // --- View background color (TiXL OutputWindow.cs:306-312: shown ONLY for a Command view; no effect on a
  // Texture2D view). Seeds the terminal Command executor's base clear; engage every frame (TiXL), else clear. ---
  if (viewIsCommand) {
    sameLineIfFits(30.0f);
    ImGui::ColorEdit4("##OutputBackground", g_viewBackground,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);  // TiXL ColorEditButton
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adjust background color of view");  // TiXL:311
    sw::eye::recordItem("output_background_btn");
    sw::setOutputBackgroundColor(g_viewBackground[0], g_viewBackground[1], g_viewBackground[2],
                                 g_viewBackground[3]);
  } else {
    sw::clearOutputBackgroundColor();  // Texture2D / preview view → executor default black
  }

  // What the viewport is actually showing (viewNode/vs/outType resolved once at the top of the window).
  if (pinned)
    ImGui::TextDisabled("%s (pinned)", vs ? vs->title.c_str() : "?");
  else if (viewNode)
    ImGui::TextDisabled("%s (selected)", vs ? vs->title.c_str() : "?");
  else
    ImGui::TextDisabled("Terminal (select a node to preview it)");

  // --- type honesty (§5): v1 only visualizes Points. A draw node (DrawPoints) is always drawable
  // (renders its own input). A command op renders via the RenderTarget executor when terminal; Points →
  // reuse DrawPoints preview; Texture2D (RenderTarget) → show the texture directly. ---
  const bool drawable = !viewNode || outType == "Command" || outType == "Points" || outType == "Texture2D";
  if (!drawable)
    ImGui::TextDisabled("no preview for output type \"%s\" yet",
                        outType.empty() ? "?" : outType.c_str());

  // --- the viewport: the shell's preview texture, cooked for the pinned/terminal node, drawn
  // aspect-correct (letterbox/pillarbox) so resizing the window NEVER distorts the image. ---
  const ImVec2 region = ImGui::GetContentRegionAvail();
  pushFillWindowSize(region.x, region.y);  // S1-fill window-follow: Fill render size = this region, live
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  MTL::Texture* tex = sw::previewTexture();
  int texW = 0, texH = 0;
  const bool haveTex = tex && sw::previewTextureSize(texW, texH) &&
                       region.x > 1.0f && region.y > 1.0f;

  if (haveTex) {
    const float fTexW = static_cast<float>(texW), fTexH = static_cast<float>(texH);

    // Apply view-mode buttons / first-frame fit. Fitted re-fits every frame so a window
    // resize always re-letterboxes (the whole point of this gap).
    if (wantFit) g_canvas.mode = ViewMode::Fitted;
    if (wantPixel) {
      g_canvas.mode = ViewMode::Pixel;
      setPixelScale(g_canvas, fTexW, fTexH, region.x, region.y);
    }
    if (g_canvas.mode == ViewMode::Fitted)
      fitToRegion(g_canvas, fTexW, fTexH, region.x, region.y);

    // An invisible button over the whole region captures hover + drag without letting the texture
    // (drawn at an arbitrary offset) steal the interaction. It feeds EITHER the 3D orbit gesture (a
    // Command/Points view) OR the 2D image-canvas pan/zoom (a Texture2D view) — the view-type gate below
    // (TiXL CameraSelectionHandling.PreventImageCanvasInteraction, cs:83/141) picks which, mutually
    // exclusively, so a 3D scene orbits and a flat texture still pans/zooms as before.
    ImGui::InvisibleButton("##outputcanvas", region,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (is3DView)
      // 3D view: left-drag = orbit, wheel = zoom. Auto mode routes to Locked-to-Op (a selected Camera /
      // OrbitCamera node's params, undoable) or Viewer (a session ViewCamera → the phase-B cook seam).
      sw::ui::handleOutputOrbit(cur, viewNode, active, hovered);
    else
      // Texture2D view: the existing 2D image-canvas pan/zoom (moved into output_window_canvas, unchanged).
      handleImageCanvasMouse(origin.x, origin.y, active, hovered);

    // Draw the texture at its transformed rect (clipped to the region). Aspect is preserved
    // because width and height share the same `scale`.
    const ImVec2 topLeft(origin.x - g_canvas.scrollX * g_canvas.scale,
                         origin.y - g_canvas.scrollY * g_canvas.scale);
    const ImVec2 botRight(topLeft.x + fTexW * g_canvas.scale,
                          topLeft.y + fTexH * g_canvas.scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + region.x, origin.y + region.y), true);
    dl->AddImage(reinterpret_cast<ImTextureID>(tex), topLeft, botRight);

    // Bottom overlay: "WxH ×scale" centered (TiXL ImageOutputCanvas description line).
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%dx%d  x%.2f", texW, texH, g_canvas.scale);
    const ImVec2 tsz = ImGui::CalcTextSize(overlay);
    const ImVec2 tpos(origin.x + (region.x - tsz.x) * 0.5f,
                      origin.y + region.y - tsz.y - 4.0f);
    const ImU32 shadow = IM_COL32(0, 0, 0, 160);
    dl->AddText(ImVec2(tpos.x + 1, tpos.y), shadow, overlay);
    dl->AddText(ImVec2(tpos.x - 1, tpos.y), shadow, overlay);
    dl->AddText(ImVec2(tpos.x, tpos.y + 1), shadow, overlay);
    dl->AddText(ImVec2(tpos.x, tpos.y - 1), shadow, overlay);
    dl->AddText(tpos, IM_COL32(235, 235, 235, 255), overlay);
    dl->PopClipRect();
  }

  captureOutputWindowState(g_pinnedNode, g_selectedResIndex, g_viewBackground);  // mirror -> app store (next Save persists it)

  ImGui::End();
}

}  // namespace sw::ui
