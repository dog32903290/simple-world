// ui/node_draw — draws ONE child instance on the canvas: the TiXL-parity skin (category
// color via node_style, port columns with type-colored slots, custom face via node_faces)
// plus the eye hooks for its body/pin rects. The canvas loop (editor_ui) calls
// drawChild(child) per child of the CURRENT symbol; connections/links are drawn by the
// canvas itself, not here. Zone: ui. Must be called between ed::Begin("canvas") and ed::End().
#pragma once

namespace sw { struct SymbolChild; struct SlotDef; }

namespace sw::ui {

void drawChild(const sw::SymbolChild& child);

// One BOUNDARY item — the current symbol's own external port drawn as a movable canvas
// node (= TiXL Legacy InputNode/OutputNode). `isSource`: an inputDef feeds the subgraph
// (pin on the right); an outputDef drains it (pin on the left). The caller owns the ed/pin
// id scheme (negative ed node ids; boundary pins ride a high disjoint band, see editor_ui).
void drawBoundaryDef(const sw::SlotDef& def, int edNodeId, int pinId, bool isSource);

}  // namespace sw::ui
