// ui/fence_preview — LIVE preview of a rubber-band (fence) selection over the node canvas:
// while the user drags a selection box on empty canvas, highlight EVERY node the box
// currently covers (i.e. what releasing now would select) instead of revealing the
// selection only after release. Zone: ui. Reads the current Symbol + ed transforms;
// draws an overlay; never mutates the graph or the selection.
//
// = TiXL Editor/UiModel/Selection/SelectionFence.cs (locked SHA 395c4c55): TiXL runs its
// OWN fence over the MagGraph canvas (a custom canvas, NOT imgui-node-editor) and each
// frame computes BoundsInScreen from the press point -> current mouse, then the caller
// pre-highlights the items inside. Our canvas uses the vendored imgui-node-editor whose
// built-in fence performs the actual selection on RELEASE but exposes no in-progress rect
// (imgui_node_editor.h has GetSelectedNodes/HasSelectionChanged but no marquee getter).
// So this adds the LIVE highlight TiXL has and the built-in fence lacks; the built-in
// fence still does the real selection on release. Named fork "preview-only-overlay".
#pragma once

#include <string>
#include <vector>

namespace sw { struct Symbol; }

namespace sw::ui {

// Call once per frame from drawNodeCanvas, INSIDE ed::Begin("canvas") ... ed::End(), AFTER
// the children are drawn (so node positions/sizes are queryable). No-op unless a fence drag
// is in progress on empty canvas.
//   hostHovered: IsWindowHovered() of the "##canvas_host" window, measured by the caller
//   BEFORE ed::Begin (inside the ed scope the current window is the node-editor's internal
//   child, so a host-hover query there returns the wrong answer — the fence press gate needs
//   "mouse over the canvas, not over a floating Inspector/Output/Timeline window").
void drawFenceSelectionPreview(const sw::Symbol* cur, bool hostHovered);

// Verify surface (eye state.json hook, 鐵律3 one-liner): the LAST fence drag's covered set —
// the child ids the live highlight outlined on its final Active frame (= what releasing then
// selected). Latched, so it survives the release the live-driver sees. fenceActive() is true
// only WHILE a drag is in progress (transient). Used by the live .scn to assert the preview
// computed the right membership without needing a mid-drag screenshot.
bool fenceActive();
const std::vector<int>& fenceLastCovered();
std::string fenceLastCoveredJson();  // sorted ascending, "[a, b, c]"

// Isolation test (ARCHITECTURE.md 鐵律 5): the pure rect-overlap predicate that decides
// which nodes the fence covers. --selftest-fencepreview / -bug.
int runFenceSelfTest(bool injectBug);

}  // namespace sw::ui
