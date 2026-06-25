// ui/output_window — TiXL OutputWindow + ViewSelectionPinning, faithful + minimal.
// Zone: ui. Depends on app(document) + runtime(graph) + the shell's previewTexture seam.
// Never mutates the graph; the pin (g_pinnedNode) is session-only state, never serialized.
//
// view ⊥ graph: "what I'm building" (the graph) and "what I'm looking at" (the pin) are
// two different things. The cook target is decided by the pin in the shell (main.cpp); this
// window only drives the pin and shows the result. OUTPUT_PIN_VIEWER_CONTRACT §4-A / §5.
//
// The image canvas is a faithful port of TiXL ImageOutputCanvas + ScalableCanvas:
//   screenPos = (canvasPos - scroll) * scale + regionTopLeft   (ScalableCanvas.TransformPositionFloat)
// The texture occupies canvas rect [0,0]..[W,H] so it NEVER stretches — a single uniform
// `scale` preserves aspect; the unfilled area is the letterbox/pillarbox. Fit/1:1 = view modes
// (ImageOutputCanvas.Modes); mouse-drag pans, wheel zooms around the cursor; a manual pan/zoom
// flips Fitted/Pixel -> Custom (ImageOutputCanvas.UpdateViewMode). Damping is disabled (TiXL's
// DisableDamping path) — the targets are applied immediately.
#include "ui/output_window.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"

#include "app/document.h"
#include "app/snapshot.h"  // saveSnapshot: product Output→PNG (TiXL OutputWindow Icon.Snapshot)
#include "ui/editor_ui.h"  // g_pinnedNode (the session pin) + g_selectedNode (what Pin grabs)
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
// cook seeds into RequestedResolution (cook-core hook landed in 1b53b12). A preset → set; Fill →
// clear (back to window size, byte-identical to today). Defined in main.cpp (shell owns g_pointGraph).
void setOutputResolutionOverride(int w, int h);
void clearOutputResolutionOverride();
bool outputWindowResolution(int& w, int& h);  // Fill baseline (TiXL GetWindowSize role)
}  // namespace sw

namespace sw::ui {

namespace {
// A child's primary output type ("Points" | "ParticleForce" | "Float"). Empty if it
// has no output port (a draw node like DrawPoints) or no spec — exactly TiXL's typed
// OutputUi lookup: no OutputUi for the type -> nothing to show.
std::string outputTypeOf(const sw::SymbolChild* c) {
  const sw::NodeSpec* s = c ? sw::findSpec(c->symbolId) : nullptr;
  if (!s) return "";
  for (const sw::PortSpec& p : s->ports)
    if (!p.isInput) return p.dataType;
  return "";
}

// --- the aspect-correct image canvas (port of TiXL ImageOutputCanvas + ScalableCanvas) ---
enum class ViewMode { Fitted, Pixel, Custom };

// Session-only view state, owned by this window (like TiXL's ImageOutputCanvas instance
// fields). Never serialized — "how I'm looking", not "what I built".
struct CanvasState {
  float scale = 1.0f;     // uniform px-per-texel (aspect always preserved)
  float scrollX = 0.0f;   // canvas-space scroll (TiXL Scroll)
  float scrollY = 0.0f;
  ViewMode mode = ViewMode::Fitted;
};
CanvasState g_canvas;

// TiXL ScalableCanvas.ClampScaleToValidRange (non-timeline branch): [0.02, 40].
float clampScale(float s) { return std::clamp(s, 0.02f, 40.0f); }

// TiXL GetScopeForCanvasArea: fit the texture rect [0,0]..[texW,texH] into the region,
// uniform scale (aspect preserved), centered. This is the load-bearing "no distortion" math.
void fitToRegion(CanvasState& c, float texW, float texH, float regionW, float regionH) {
  if (texW < 1.0f || texH < 1.0f || regionW < 1.0f || regionH < 1.0f) return;
  const float texAspect = texW / texH;
  const float regionAspect = regionW / regionH;
  if (texAspect > regionAspect) {
    c.scale = regionW / texW;                          // fit to width, center vertically
    c.scrollX = 0.0f;
    c.scrollY = -(regionH / c.scale - texH) * 0.5f;
  } else {
    c.scale = regionH / texH;                          // fit to height, center horizontally
    c.scrollX = -(regionW / c.scale - texW) * 0.5f;
    c.scrollY = 0.0f;
  }
}

// TiXL Modes.Pixel: SetScaleToMatchPixels (scale -> 1). Recenter so 1:1 lands in the middle.
void setPixelScale(CanvasState& c, float texW, float texH, float regionW, float regionH) {
  c.scale = 1.0f;
  c.scrollX = -(regionW - texW) * 0.5f;
  c.scrollY = -(regionH - texH) * 0.5f;
}

// --- Output resolution selector (port of TiXL ResolutionHandling) ----------------------------
// The preset TABLE is the canonical default set (ResolutionHandling.cs:69-78) — exact labels +
// dims, in order, 隨 TiXL 不自編. `useAsAspectRatio` entries resolve through ComputeResolution
// (window-aspect fit); fixed-pixel entries return their {w,h} verbatim. The Fill sentinel is
// index 0 (DefaultResolution): selecting it CLEARS the cook override (== window size == today).
// Data-driven (鐵律 7): one row per preset, the combo + apply both iterate the table.
struct ResPreset {
  const char* title;
  int w, h;
  bool useAsAspectRatio;
};
const ResPreset kResPresets[] = {
    {"Fill", 0, 0, true},   {"1:1", 1, 1, true},   {"16:9", 16, 9, true},
    {"4:3", 4, 3, true},    {"480p", 850, 480, false},  {"720p", 1280, 720, false},
    {"1080p", 1920, 1080, false}, {"4k", 1920 * 2, 1080 * 2, false},
    {"8k", 1920 * 4, 1080 * 4, false}, {"4k Portrait", 1080 * 2, 1920 * 2, false},
};
constexpr int kResPresetCount = static_cast<int>(sizeof(kResPresets) / sizeof(kResPresets[0]));

// Session-only selection: index into kResPresets. 0 = Fill = DefaultResolution = follow window
// (the cook default). Never serialized — matches the Pin (a view setting, not graph state) and
// TiXL, which keeps _selectedResolution in the OutputWindow instance, not the .t3.
int g_selectedResIndex = 0;

// TiXL Resolution.ComputeResolution (ResolutionHandling.cs:115-135). Fixed-pixel preset → its
// own size. UseAsAspectRatio with 0/0 (Fill) → the window size verbatim. Otherwise fit the
// requested aspect into the window: requested wider than window → fit width (letterbox), else fit
// height (pillarbox). `winW/winH` is the Fill baseline = the cook's window resolution. Returns
// {0,0} only if the window is degenerate (caller treats that as "leave override unset" = Fill).
struct Int2 { int w, h; };
Int2 computeResolution(const ResPreset& p, int winW, int winH) {
  if (!p.useAsAspectRatio) return {p.w, p.h};
  if (winW <= 0 || winH <= 0) return {0, 0};
  if (p.w <= 0 || p.h <= 0) return {winW, winH};  // Fill: window size verbatim
  const float windowAspect = static_cast<float>(winW) / static_cast<float>(winH);
  const float requestedAspect = static_cast<float>(p.w) / static_cast<float>(p.h);
  return (requestedAspect > windowAspect)
             ? Int2{winW, static_cast<int>(winW / requestedAspect)}
             : Int2{static_cast<int>(winH * requestedAspect), winH};
}

// Drive the cook-core override to match g_selectedResIndex. Fill (index 0) clears; any other
// preset sets the computed size. `winW/winH` = the Fill-baseline window size (the size the cook
// shows when no override is engaged). Called ON CHANGE (not every frame) so there is no cook
// churn — the setters are idempotent, but we still gate on a change to keep the contract crisp.
void applyResolutionSelection(int winW, int winH) {
  const ResPreset& p = kResPresets[g_selectedResIndex];
  if (g_selectedResIndex == 0) {  // Fill / DefaultResolution -> back to window size.
    sw::clearOutputResolutionOverride();
    return;
  }
  const Int2 r = computeResolution(p, winW, winH);
  if (r.w > 0 && r.h > 0) sw::setOutputResolutionOverride(r.w, r.h);
  else sw::clearOutputResolutionOverride();  // degenerate window -> behave as Fill
}
}  // namespace

void drawOutputWindow() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // Default spot: lower-right, clear of the toolbar (top-left), the Inspector (top-right)
  // and the default graph's nodes. 柏為 drags it wherever he wants — it's his viewport.
  ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 404.0f,
                                 vp->WorkPos.y + vp->WorkSize.y - 444.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::Begin("Output");

  // Drop a stale pin (the pinned node was deleted) -> resume following selection.
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  if (g_pinnedNode != 0 && (!cur || !sw::childById(*cur, g_pinnedNode))) g_pinnedNode = 0;
  const bool pinned = g_pinnedNode != 0;
  const sw::SymbolChild* pinnedNode = pinned ? sw::childById(*cur, g_pinnedNode) : nullptr;

  // --- toolbar: Pin / switch / Unpin, on the active op (TiXL Icon.Pin + PinSelectionToView).
  // Unpinned, the viewport FOLLOWS the selected node (TiXL); Pin LOCKS it so it stops
  // following (and clicking other nodes no longer changes the view). One button:
  //   - a node is selected that isn't the pinned one -> "Pin selected" locks / switches to it
  //   - otherwise, if pinned -> "Unpin" resumes following selection ---
  const bool canPinSelection = g_selectedNode != 0 && g_selectedNode != g_pinnedNode;
  if (ImGui::Button(canPinSelection ? "Pin selected" : (pinned ? "Unpin" : "Pin selected"))) {
    if (canPinSelection)
      g_pinnedNode = g_selectedNode;                 // lock / switch to the active op
    else if (pinned)
      g_pinnedNode = 0;                              // resume following selection
  }
  sw::eye::recordItem("output_pin_btn");             // eye: hand off this button's screen rect
  ImGui::SameLine();

  // Fit / 1:1 view-mode buttons (TiXL ImageOutputCanvas.SetViewMode). Fit = aspect-correct
  // letterbox; 1:1 = native pixels. Both recompute below once the texture size is known.
  const bool wantFit = ImGui::Button("Fit");
  sw::eye::recordItem("output_fit_btn");
  ImGui::SameLine();
  const bool wantPixel = ImGui::Button("1:1");
  sw::eye::recordItem("output_pixel_btn");
  ImGui::SameLine();

  // Snapshot: save the current Output render to a PNG the user keeps (TiXL OutputWindow.cs:332
  // Icon.Snapshot → RenderProcess.TryRenderScreenShot). Writes <project>/Screenshots/<stamp>.png
  // directly — no save dialog (faithful to TiXL, which has none for screenshots). Disabled when
  // there is no preview texture yet (nothing to capture), mirroring TiXL's MainOutputType==null
  // disabled state. ui → app(saveSnapshot) → platform(image_save): no Metal in this zone.
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
  ImGui::SameLine();

  // --- Output resolution selector (TiXL ResolutionHandling.DrawSelector, OutputWindow.cs:316) ---
  // Picks the frame render size the cook seeds into RequestedResolution. Fill (default) follows the
  // window (== today, byte-identical); a preset retargets a Texture/image-filter terminal. The
  // selection is session-only view state (g_selectedResIndex), never serialized — same as the Pin.
  // Record the combo box's rect from PRE-widget geometry: while its popup is open, the "last item"
  // is the popup's last Selectable, so recording after BeginCombo would hand the map that instead
  // of the combo box (inspector.cpp:190 refuter N4 #2). The open rows are addressed by the eye's
  // popup walker as popup_item:<combo-window>:<row> (eye.mm recordOpenPopupItems).
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
      // Give each open row a '#'-free named rect for the hand (output_res_row:<i>). The generic
      // eye popup-walker emits "popup_item:##Combo_00:<row>", but the scenario runner strips '#'
      // as a comment delimiter, so that label can't be addressed from a .scn. These named rects
      // (recorded only while the popup is open, same as the inspector enum combo) are clean. The
      // index is the TABLE order (i), which == geometric order because we omit the focus-scroll.
      {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        char rowLbl[32];
        std::snprintf(rowLbl, sizeof(rowLbl), "output_res_row:%d", i);
        sw::eye::recordRect(rowLbl, mn.x, mn.y, mx.x, mx.y);
      }
      // NB: deliberately NOT calling SetItemDefaultFocus() on the selected row. That call makes
      // ImGui auto-scroll the popup so the selected item aligns with the combo box — which shifts
      // every row's screen position by the selection offset, breaking the eye's geometric popup
      // walker (a hand clicking "Fill" after picking "1080p" would land on a scrolled-away row).
      // The full preset list is short and fully visible, so no scroll-to-selection is needed; the
      // selected row still shows its highlight. Keeps geometric row N == table order N, always.
    }
    ImGui::EndCombo();
  } else if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Adjust requested output resolution");  // TiXL tooltip (ResolutionHandling.cs:56)
  }
  sw::eye::recordRect("output_resolution_combo", comboPos.x, comboPos.y,
                      comboPos.x + comboW, comboPos.y + ImGui::GetFrameHeight());
  ImGui::SameLine();

  // What the viewport is actually showing (mirror the shell's cook-target priority in
  // main.cpp): the pinned node wins; else the selected node (follow); else the terminal.
  const sw::SymbolChild* viewNode = pinnedNode;
  if (!viewNode && g_selectedNode != 0 && cur) viewNode = sw::childById(*cur, g_selectedNode);
  const sw::NodeSpec* vs = viewNode ? sw::findSpec(viewNode->symbolId) : nullptr;
  if (pinned)
    ImGui::TextDisabled("%s (pinned)", vs ? vs->title.c_str() : "?");
  else if (viewNode)
    ImGui::TextDisabled("%s (selected)", vs ? vs->title.c_str() : "?");
  else
    ImGui::TextDisabled("Terminal (select a node to preview it)");

  // --- type honesty (§5): v1 only visualizes Points. A draw node (DrawPoints) is always
  // drawable (renders its own input). Nothing pinned/selected = the terminal draw node. ---
  bool drawable;
  std::string outType;
  if (!viewNode)
    drawable = true;                                 // terminal draw node -> today's picture
  else {
    outType = outputTypeOf(viewNode);
    // A command op (DrawPoints/DrawLines/DrawBillboards: Command out) renders its own input via
    // the RenderTarget executor when terminal; Points -> reuse DrawPoints preview; Texture2D
    // (RenderTarget) -> show the texture directly.
    drawable = (outType == "Command" || outType == "Points" || outType == "Texture2D");
  }
  if (!drawable)
    ImGui::TextDisabled("no preview for output type \"%s\" yet",
                        outType.empty() ? "?" : outType.c_str());

  // --- the viewport: the shell's preview texture, cooked for the pinned/terminal node, drawn
  // aspect-correct (letterbox/pillarbox) so resizing the window NEVER distorts the image. ---
  const ImVec2 region = ImGui::GetContentRegionAvail();
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

    // An invisible button over the whole region captures hover + drag for pan/zoom without
    // letting the texture (drawn at an arbitrary offset) steal the interaction.
    ImGui::InvisibleButton("##outputcanvas", region,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    // Pan: drag moves the content with the cursor (TiXL ScrollTarget -= delta / scale).
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      const ImVec2 d = ImGui::GetIO().MouseDelta;
      if (d.x != 0.0f || d.y != 0.0f) {
        g_canvas.scrollX -= d.x / g_canvas.scale;
        g_canvas.scrollY -= d.y / g_canvas.scale;
        g_canvas.mode = ViewMode::Custom;            // manual pan -> Custom (UpdateViewMode)
      }
    }

    // Zoom around the cursor (TiXL ApplyZoomDelta): scale *= zoom, then keep the texel under
    // the mouse fixed by shifting scroll toward the focus point by (zoom-1)/zoom.
    const float wheel = ImGui::GetIO().MouseWheel;
    if (hovered && wheel != 0.0f) {
      const float zoom = std::pow(1.2f, wheel);      // TiXL zoomSpeed = 1.2 per notch
      const float newScale = clampScale(g_canvas.scale * zoom);
      if (newScale != g_canvas.scale) {
        const float applied = newScale / g_canvas.scale;  // honour the clamp
        // focus point in canvas space (InverseTransformPositionFloat) at the OLD scale.
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float focusX = (m.x - origin.x) / g_canvas.scale + g_canvas.scrollX;
        const float focusY = (m.y - origin.y) / g_canvas.scale + g_canvas.scrollY;
        g_canvas.scale = newScale;
        g_canvas.scrollX += (focusX - g_canvas.scrollX) * (applied - 1.0f) / applied;
        g_canvas.scrollY += (focusY - g_canvas.scrollY) * (applied - 1.0f) / applied;
        g_canvas.mode = ViewMode::Custom;            // manual zoom -> Custom
      }
    }

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

  ImGui::End();
}

}  // namespace sw::ui
