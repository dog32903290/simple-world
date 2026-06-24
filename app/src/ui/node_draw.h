// ui/node_draw — draws ONE child instance on the canvas: the TiXL-parity skin (category
// color via node_style, port columns with type-colored slots, custom face via node_faces)
// plus the eye hooks for its body/pin rects. The canvas loop (editor_ui) calls
// drawChild(child) per child of the CURRENT symbol; connections/links are drawn by the
// canvas itself, not here. Zone: ui. Must be called between ed::Begin("canvas") and ed::End().
#pragma once

#include <string>

namespace sw { struct SymbolChild; struct SlotDef; struct Symbol; struct PortSpec; }

namespace sw::ui {

// Zoom gating (TiXL MagGraphCanvas.DrawNode.cs): input/output LABELS draw at CanvasScale>0.25
// (:399); the per-input live VALUE string draws at CanvasScale>0.4 (:484), and only for
// NON-primary inputs (the primary input, ordinal 0, is the main data flow — TiXL skips it,
// :447). Pure (no imgui/graph) so the thresholds are a single SSOT shared by drawChild AND the
// --selftest-nodeval tooth — they cannot drift apart. `tixlScale` = ViewScale (= 1/GetCurrentZoom).
inline bool nodeShowLabelAtScale(float tixlScale) { return tixlScale > 0.25f; }
inline bool nodeShowValueAtScale(float tixlScale, int inputOrdinal) {
  return tixlScale > 0.4f && inputOrdinal > 0;
}

// Format an input port's effective value as the short body string (TiXL ValueUtils.GetValueString,
// :488-502): Float "%.3f" ("{:0.000}"), Bool "True/False", Enum → option label, String → truncated.
// Pure: the caller resolves the effective float `v` / text `strv` (via effectiveInput) and passes
// them in, so this is unit-testable without a live graph. Non-value dataTypes (Points/...) → "".
std::string formatInputValue(const sw::PortSpec& p, float v, const std::string& strv);

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

// --selftest-nodeval: the node-body value-string FORMAT + zoom-gating thresholds are a tooth
// (formatInputValue + nodeShow*AtScale). Pure logic, no GUI. Returns 0 GREEN / nonzero RED.
int runNodeValSelfTest(bool injectBug);

}  // namespace sw::ui
