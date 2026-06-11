// ui/copy_paste_ui — the Cmd+C / Cmd+V gestures + context-menu Copy/Paste for the node canvas
// (copy/paste 契約 4 GUI seam). Zone: ui. Keeps editor_ui.cpp from bloating past its line cap.
//
// Copy: serialize the current selection's clipboard JSON into the OS clipboard (imgui
//   SetClipboardText) — cross-symbol AND cross-process, 照 TiXL (clipboard JSON, transient
//   symbol never registered).
// Paste: parse the OS clipboard, plan a paste into the CURRENT symbol anchored at `canvasX/Y`,
//   push the undoable CopyPasteChildrenCommand. The cycle gate inside planPaste drops any child
//   that would self-nest. A no-op (empty selection / non-clipboard text / all dropped) pushes
//   nothing onto the undo stack.
#pragma once

#include <vector>

namespace sw::ui {

// Copy an EXPLICIT set of child ids (the context menu's captured selection, which may include a
// right-clicked-but-unselected node). Serializes clipboard JSON to the OS clipboard. No-op if the
// ids yield nothing copyable. Must be called with the node editor set current.
void copyChildrenToClipboard(const std::vector<int>& childIds);

// Copy the canvas's currently-selected child ids (queried from the node editor) to the OS
// clipboard. Boundary items (negative ed ids) are excluded — only real children copy. No-op (and
// a status line) if nothing's selected. Must be called with the node editor set current.
void copySelectionToClipboard();

// Paste the OS clipboard into the current symbol, anchoring the selection's upper-left at the
// given CANVAS coords (caller converts a screen point via ed::ScreenToCanvas, or passes a small
// cascade offset for keyboard paste). Pushes one undoable command; no-op on empty/foreign clipboard.
void pasteClipboardAt(float canvasX, float canvasY);

}  // namespace sw::ui
