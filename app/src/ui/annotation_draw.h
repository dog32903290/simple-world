// ui/annotation_draw — draw + interact with the canvas annotation frames (批B/C, = TiXL
// MagGraphCanvas.DrawAnnotation.cs + AnnotationDragging/Resizing/Renaming.cs). Zone: ui. Depends on
// app(annotation_commands/document/command) + runtime + verify(thin eye hook). Never the reverse.
//
// WHY a leaf next to editor_ui (not inside it): editor_ui already runs ~460 lines (rule 4 ceiling).
// Annotations are their OWN draw/interaction surface — they are NOT imgui-node-editor nodes (TiXL draws
// them on the canvas drawlist behind the node layer, MagGraphCanvas.Drawing.cs:120,317). So we draw them
// on the BASE drawlist BEFORE ed::Begin (same招 as the V1 grid layer, editor_ui.cpp:54), and hit-test
// them with our OWN invisible buttons + mouse geometry rather than ed's node hit-test. This keeps the
// annotation surface a single-responsibility file and editor_ui under its line budget.
#pragma once

namespace sw { struct Symbol; }

namespace sw::ui {

// Draw every annotation of `cur` on the canvas + run their interaction (drag-move / corner-resize /
// double-click rename / click-select). MUST be called AFTER ed::SetCurrentEditor but BEFORE ed::Begin
// (so the frames land on the base drawlist UNDER the node layer = TiXL annotation-behind-nodes order)
// AND inside the canvas host window scope (it reads/uses ed::CanvasToScreen + the window drawlist).
// A no-op when `cur` is null. Pushes undoable commands (Add/MoveResize/ChangeText) for finished gestures.
void drawAnnotations(sw::Symbol* cur);

// Shift+A handler (keymap row): queue "create an annotation at the mouse canvas point next frame, then
// enter inline rename". Deferred to next frame's drawAnnotations because the create must read the mouse
// position in canvas space and seed the rename state, both of which live in this module. Faithful to
// TiXL NodeActions.AddAnnotation (100x140 default at the mouse, or the selection bbox + Expand(60,120))
// + KeyboardActions.cs:138 SetState(RenameAnnotation) right after.
void requestCreateAnnotation();

// Is an inline annotation rename currently open? editor_ui asks so it can suppress the Backspace->
// DeleteNode routing + Cmd shortcuts while the user is typing into the rename text fields (the imgui
// io.WantTextInput guard already covers most of it; this is the belt-and-suspenders for node deletion).
bool annotationRenameActive();

// Headless isolation test (鐵律5): the pure-geometry helpers this module owns (mouse-in-rect hit test,
// the create sizing math, the zoom fade ramp) WITHOUT an imgui context. injectBug breaks the contained-
// in-rect test so a point clearly inside reads as outside -> FAIL (teeth).
int runAnnotationDrawSelfTest(bool injectBug);

}  // namespace sw::ui
