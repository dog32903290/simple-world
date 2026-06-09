// ui/node_draw — draws ONE graph node on the canvas: the TiXL-parity skin (category color
// via node_style, port columns with type-colored slots, custom face via node_faces) plus
// the eye hooks for its body/pin rects. The canvas loop (editor_ui) calls drawNode(node)
// per node; connections/links are drawn by the canvas itself, not here. Zone: ui.
// Must be called between ed::Begin("canvas") and ed::End().
#pragma once

namespace sw { struct Node; }

namespace sw::ui {

void drawNode(const sw::Node& node);

}  // namespace sw::ui
