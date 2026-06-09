#pragma once
// ui/output_window — the live preview viewport (view ⊥ graph). Faithful to TiXL's
// OutputWindow + ViewSelectionPinning: a fixed floating window that shows whatever the
// session has PINNED (sw::ui::g_pinnedNode, owned in editor_ui), NOT a wired graph edge.
// Pinning never touches doc::g_graph and never enters .swproj — it is "what I'm looking
// at", not "what I built". Zone: ui (reads app + runtime; never mutates the graph).
namespace sw::ui {

// Draw the floating Output window: a Pin/Unpin toggle + the pinned (or terminal) node's
// rendered output. Call once per frame alongside drawToolbar/drawNodeCanvas/drawInspector.
void drawOutputWindow();

}  // namespace sw::ui
