// ui/output_window_canvas — the aspect-correct image canvas (port of TiXL ImageOutputCanvas +
// ScalableCanvas). Extracted verbatim from output_window.cpp; pure pan/zoom/fit math, no behavior
// change. Zone: ui (no app/runtime/platform deps — just std math + the shared canvas state).
#include "ui/output_window_canvas.h"

#include <algorithm>

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

}  // namespace sw::ui
