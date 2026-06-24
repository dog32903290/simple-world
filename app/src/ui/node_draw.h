// ui/node_draw — draws ONE child instance on the canvas: the TiXL-parity skin (category
// color via node_style, port columns with type-colored slots, custom face via node_faces)
// plus the eye hooks for its body/pin rects. The canvas loop (editor_ui) calls
// drawChild(child) per child of the CURRENT symbol; connections/links are drawn by the
// canvas itself, not here. Zone: ui. Must be called between ed::Begin("canvas") and ed::End().
#pragma once

namespace sw { struct SymbolChild; struct SlotDef; struct Symbol; }

namespace sw::ui {

// `parent` = the symbol owning this child; used to test each input pin for an incoming
// connection (L-G required-input indicator). May be null → the required test is skipped.
void drawChild(const sw::SymbolChild& child, const sw::Symbol* parent);

// One BOUNDARY item — the current symbol's own external port drawn as a movable canvas
// node (= TiXL Legacy InputNode/OutputNode). `isSource`: an inputDef feeds the subgraph
// (pin on the right); an outputDef drains it (pin on the left). The caller owns the ed/pin
// id scheme (negative ed node ids; boundary pins ride a high disjoint band, see editor_ui).
void drawBoundaryDef(const sw::SlotDef& def, int edNodeId, int pinId, bool isSource);

// Connection-arrow overlay (TiXL MagGraphCanvas.DrawConnection.cs:226-231, RightToLeft style):
// a small filled triangle at the wire's TARGET (input) end, apex pointing RIGHT along the wire
// into the input slot. node_draw records each input pin's left-edge anchor (the wire terminus)
// as it lays pins out; the canvas wire loop then draws the arrow in the connection's color.
// clearConnectionArrowAnchors() resets the per-frame map (call once before drawing children).
// Both halves live in ui (no verify/app dependency); color is an ImU32 packed RGBA.
void clearConnectionArrowAnchors();
void drawConnectionArrow(int inputPinId, unsigned int color);  // no-op if anchor unknown this frame

}  // namespace sw::ui
