#pragma once
#include "imgui_node_editor.h"

namespace sw::ui {

// Node-editor context + selection live here (owned by the editor UI).
extern ax::NodeEditor::EditorContext* g_NodeEditor;
extern int g_selectedNode;

void drawToolbar();      // New/Open/Save/Save As + Add Node (calls sw::doc ops)
void drawNodeCanvas();   // the main node graph workspace
void drawInspector();    // selected node's parameters + FPS

}  // namespace sw::ui
