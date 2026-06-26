#pragma once
// ui/editor_ui_canvas_edit — canvas mutation handlers split out of editor_ui.cpp (mechanical,
// rule 4). Two self-contained phases of drawNodeCanvas that mutate the document: keyboard
// shortcuts (undo/redo/copy/paste) and the node/wire/boundary-def deletion macro. Both run
// INSIDE ed::Begin..End (they call ed:: queries that need the editor current). Zone: ui.
namespace sw {
struct Symbol;
}

namespace sw::ui {

// Undo/Redo (Cmd+Z / Cmd+Shift+Z) + Copy/Paste (Cmd+C / Cmd+V). Reads io.KeyCtrl
// (ConfigMacOSXBehaviors swaps Cmd→Ctrl). Must be called inside ed::Begin..End with the
// canvas host window focused (ed::ScreenToCanvas for paste anchor needs the editor current).
void processCanvasKeyboard();

// Delete links / nodes / boundary defs (select + Delete key, or routed Backspace). Drains
// the node-editor's BeginDelete/EndDelete queue and pushes ONE MacroCommand. `cur` = the
// current symbol being edited. Must be called inside ed::Begin..End.
void processCanvasDeletions(sw::Symbol* cur);

}  // namespace sw::ui
