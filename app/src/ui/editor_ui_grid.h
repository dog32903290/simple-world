#pragma once
// ui/editor_ui_grid — the faithful TiXL canvas background grid (DrawBackgroundGrids,
// MagGraphCanvas.Drawing.cs:377-426), split out of editor_ui.cpp (mechanical, rule 4).
// Pure draw: reads the node-editor zoom + the current imgui window, paints 1px grid lines
// onto the base draw-list channel. Must be called AFTER ed::SetCurrentEditor but BEFORE
// ed::Begin (so the draws land under the editor's own layers). Zone: ui.
namespace sw::ui {

// Draw fine + coarse background grids (TiXL DrawBackgroundGrids). Zoom-ramped opacity,
// snapped to canvas coords. See editor_ui_grid.cpp for the exact TiXL constants.
void drawCanvasBackgroundGrids();

}  // namespace sw::ui
