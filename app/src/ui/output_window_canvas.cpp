// ui/output_window_canvas — the aspect-correct image canvas (port of TiXL ImageOutputCanvas +
// ScalableCanvas). Extracted verbatim from output_window.cpp; pure pan/zoom/fit math, no behavior
// change. Zone: ui (no app/runtime/platform deps — just std math + the shared canvas state).
#include "ui/output_window_canvas.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"  // handleImageCanvasMouse: the Texture2D-view pan/zoom drag (reads MouseDelta/Wheel/Pos)

namespace sw::ui {

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

// The Texture2D-view mouse interaction (moved verbatim from the coordinator so a 3D vs 2D view is one
// branch there). Left-drag pan + cursor-anchored wheel zoom; any manual move -> ViewMode::Custom.
void handleImageCanvasMouse(float originX, float originY, bool active, bool hovered) {
  // Pan: drag moves the content with the cursor (TiXL ScrollTarget -= delta / scale).
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    if (d.x != 0.0f || d.y != 0.0f) {
      g_canvas.scrollX -= d.x / g_canvas.scale;
      g_canvas.scrollY -= d.y / g_canvas.scale;
      g_canvas.mode = ViewMode::Custom;  // manual pan -> Custom (UpdateViewMode)
    }
  }
  // Zoom around the cursor (TiXL ApplyZoomDelta): scale *= zoom, then keep the texel under the mouse
  // fixed by shifting scroll toward the focus point by (zoom-1)/zoom.
  const float wheel = ImGui::GetIO().MouseWheel;
  if (hovered && wheel != 0.0f) {
    const float zoom = std::pow(1.2f, wheel);  // TiXL zoomSpeed = 1.2 per notch
    const float newScale = clampScale(g_canvas.scale * zoom);
    if (newScale != g_canvas.scale) {
      const float applied = newScale / g_canvas.scale;  // honour the clamp
      const ImVec2 m = ImGui::GetIO().MousePos;
      const float focusX = (m.x - originX) / g_canvas.scale + g_canvas.scrollX;
      const float focusY = (m.y - originY) / g_canvas.scale + g_canvas.scrollY;
      g_canvas.scale = newScale;
      g_canvas.scrollX += (focusX - g_canvas.scrollX) * (applied - 1.0f) / applied;
      g_canvas.scrollY += (focusY - g_canvas.scrollY) * (applied - 1.0f) / applied;
      g_canvas.mode = ViewMode::Custom;  // manual zoom -> Custom
    }
  }
}

}  // namespace sw::ui
