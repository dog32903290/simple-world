// ui/node_faces — per-node custom face rendering on the graph canvas (the imgui
// draw-list content drawn inside a node's body, beyond its pins). Mirrors TiXL's
// Editor/Gui/OpUis/UIs/*Ui.cs: one custom face per operator that needs more than a
// pin list. Zone: ui. Reads app + runtime data only; never mutates the graph.
#pragma once

namespace sw { struct Node; }

namespace sw::ui {

// AudioReaction's TiXL-parity face (faithful port of AudioReactionUi.DrawChildUi):
// windowed spectrum colored by detection window + threshold line + level meter +
// AccumulatedLevel spinner. Call between ed::BeginNode/EndNode.
void drawAudioReactionFace(const sw::Node& node);

}  // namespace sw::ui
