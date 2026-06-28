#pragma once
#include "imgui_node_editor.h"

namespace sw::ui {

// Node-editor context + selection live here (owned by the editor UI).
extern ax::NodeEditor::EditorContext* g_NodeEditor;
extern int g_selectedNode;
// Composition switch requested this frame (double-click gesture in the canvas, or a
// breadcrumb click in the toolbar): the canvas consumes it after re-seeding positions —
// clear selection + frame the new content (TiXL JumpIn/JumpOut).
extern bool g_navPending;
// View ⊥ graph: which node the Output window is pinned to (0 = not pinned = show the
// graph terminal). Session-only state — like g_selectedNode it is NEVER serialized into
// .swproj (pin is "what I'm looking at", not "what I built"). The Output window (ui/
// output_window.cpp) owns the pin gesture; the shell reads this to pick the cook target.
extern int g_pinnedNode;

void drawToolbar();      // ui/toolbar.cpp: file ops + Add Node + audio pick + breadcrumbs
void drawNodeCanvas();   // ui/editor_ui.cpp: the main node graph workspace
void drawInspector();    // ui/inspector.cpp: selected node's parameters + FPS

// Spawn a node of `type` at the given canvas coordinates. Used by the canvas right-click
// "Add Node" submenu (combine_dialog) so spawn lands at the menu-open point — not (120,120).
// = TiXL GraphView.cs:861 "SymbolBrowser.OpenAt(InverseTransformPositionFloat(clickPosition))"
void spawnNodeAt(const std::string& type, float cx, float cy);

}  // namespace sw::ui
