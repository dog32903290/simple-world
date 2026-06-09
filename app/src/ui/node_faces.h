// ui/node_faces — per-node custom face rendering on the graph canvas (the imgui
// draw-list content drawn inside a node's body, beyond its pins). Mirrors TiXL's
// Editor/Gui/OpUis/UIs/*Ui.cs: one custom face per operator that needs more than a
// pin list. Zone: ui. Reads app + runtime data only; never mutates the graph.
//
// 資料驅動 dispatch (ARCHITECTURE.md 鐵律 7): to give a node type a custom draw-list
// body, write a face fn + add ONE row to the kFaces table in node_faces.cpp — do NOT
// add a per-type `if (node.type==...)` in editor_ui's canvas loop. drawNodeFace() is
// the single call the canvas makes per node; it no-ops for types without a face.
// (Preview-texture bodies like DrawPoints are a separate "preview policy" concern,
// still handled inline in editor_ui until that policy is formalized.)
#pragma once

namespace sw { struct Node; }

namespace sw::ui {

// Draw `node`'s registered custom face, or nothing if its type has none.
// Call between ed::BeginNode/EndNode.
void drawNodeFace(const sw::Node& node);

}  // namespace sw::ui
