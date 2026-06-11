// ui/node_draw — draws ONE child instance on the canvas: the TiXL-parity skin (category
// color via node_style, port columns with type-colored slots, custom face via node_faces)
// plus the eye hooks for its body/pin rects. The canvas loop (editor_ui) calls
// drawChild(child) per child of the CURRENT symbol; connections/links are drawn by the
// canvas itself, not here. Zone: ui. Must be called between ed::Begin("canvas") and ed::End().
#pragma once

namespace sw { struct SymbolChild; }

namespace sw::ui {

void drawChild(const sw::SymbolChild& child);

}  // namespace sw::ui
