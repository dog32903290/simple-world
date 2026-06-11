// ui/node_faces — per-node custom face rendering on the graph canvas (the imgui
// draw-list content drawn inside a node's body, beyond its pins). Mirrors TiXL's
// Editor/Gui/OpUis/UIs/*Ui.cs: one custom face per operator that needs more than a
// pin list. Zone: ui. Reads app + runtime data only; never mutates the lib.
//
// 資料驅動 dispatch (ARCHITECTURE.md 鐵律 7): to give an operator a custom draw-list
// body, write a face fn + add ONE row to the kFaces table in node_faces.cpp — do NOT
// add a per-type `if (child.symbolId==...)` in editor_ui's canvas loop. drawNodeFace()
// is the single call the canvas makes per child; it no-ops for types without a face.
// Faces read params through effectiveInput (override/definition default) and live
// outputs through framecook::residentOut (the resident extOut, by path).
#pragma once

namespace sw { struct SymbolChild; }

namespace sw::ui {

// Draw `child`'s registered custom face, or nothing if its symbol type has none.
// Call between ed::BeginNode/EndNode.
void drawNodeFace(const sw::SymbolChild& child);

}  // namespace sw::ui
