#pragma once
// ui/output_window_canvas — the aspect-correct image canvas (port of TiXL ImageOutputCanvas +
// ScalableCanvas), split out of output_window.cpp. Internal to the Output window: declares the
// session-only view state + the pan/zoom/fit math the coordinator drives. Zone: ui.
//
//   screenPos = (canvasPos - scroll) * scale + regionTopLeft   (ScalableCanvas.TransformPositionFloat)
// The texture occupies canvas rect [0,0]..[W,H] so it NEVER stretches — a single uniform
// `scale` preserves aspect; the unfilled area is the letterbox/pillarbox.
namespace sw::ui {

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

// The single session-only canvas instance (defined in output_window_canvas.cpp).
extern CanvasState g_canvas;

// TiXL ScalableCanvas.ClampScaleToValidRange (non-timeline branch): [0.02, 40].
float clampScale(float s);

// TiXL GetScopeForCanvasArea: fit the texture rect [0,0]..[texW,texH] into the region,
// uniform scale (aspect preserved), centered. This is the load-bearing "no distortion" math.
void fitToRegion(CanvasState& c, float texW, float texH, float regionW, float regionH);

// TiXL Modes.Pixel: SetScaleToMatchPixels (scale -> 1). Recenter so 1:1 lands in the middle.
void setPixelScale(CanvasState& c, float texW, float texH, float regionW, float regionH);

}  // namespace sw::ui
