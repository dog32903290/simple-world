#pragma once
// ui/variation_panel — the Variation window (P2). A standalone floating window (like Inspector /
// Output), deliberately OUT of the contended node_draw / editor_ui wire paths. Paints the snapshot
// POOL GRID (3 columns, TiXL VariationCanvas layout), the N-WAY weighted MIX (per-snapshot weight
// sliders + Apply), and the full 2-WAY CROSSFADER (left/right pickers + the 0..127 fader). All state +
// wiring lives in app/variation_panel.{h,cpp}; this file is the imgui surface only.
// Zone: ui (reads app::varpanel + app::document; never touches the graph directly).
namespace sw::ui {

// Window visibility (default OFF). Like the Asset Library, an always-open panel over the canvas
// eats canvas clicks/right-clicks under it (the load-position hit-test regression). Opened ON
// DEMAND from the toolbar "Vary" button, TiXL-tool-window style, not pinned over the graph.
bool& variationPanelVisible();

// Draw the floating Variation window. Call once per frame alongside drawToolbar / drawOutputWindow.
// No-op when variationPanelVisible() is false (the canvas underneath stays fully clickable).
void drawVariationPanel();

}  // namespace sw::ui
