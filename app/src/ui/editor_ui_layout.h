#pragma once
// ui/editor_ui_layout — canvas position sync split out of editor_ui.cpp (mechanical, rule 4).
// The relayout-seed / drag-move (undo) / passive-sync of node + boundary positions between the
// node-editor and the lib. Owns the drag-start scratch. Runs INSIDE ed::Begin..End. Zone: ui.
namespace sw {
struct Symbol;
}

namespace sw::ui {

// Sync canvas node + boundary-def positions with the lib for this frame.
//   navThisFrame: a composition switch happened this frame → skip layout (positions belong to
//                 the symbol we left); the caller leaves g_relayout pending for next frame.
// On g_relayout: SetNodePosition seeds from the lib (initial/add/load/composition switch), then
// consumes the pending g_navPending (clear selection + frame content). Otherwise: drag → one
// MoveChildrenCommand on release, or passive position read-back. Then boundary defs sync straight
// back to the lib (no undo step — named asymmetry vs child moves).
void syncCanvasLayout(sw::Symbol* cur, bool navThisFrame);

}  // namespace sw::ui
