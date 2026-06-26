// ui/editor_ui_grid — the faithful TiXL canvas background grid, split from editor_ui.cpp
// (mechanical, rule 4). Pure draw; no behavior change. Zone: ui.
#include "ui/editor_ui_grid.h"

#include <cmath>

#include "imgui.h"
#include "imgui_node_editor.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {

namespace {

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

}  // namespace

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

}  // namespace sw::ui
