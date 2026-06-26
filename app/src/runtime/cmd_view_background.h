// runtime/cmd_view_background — the Output window's VIEW BACKGROUND COLOR ambient (TiXL
// OutputWindow._backgroundColor → EvaluationContext.BackgroundColor, consumed in
// CommandOutputUi.Recompute:63-67 as the base ClearRenderTargetView before the Command chain runs).
//
// MECHANISM — same shape as the C1 active-camera scope (point_ops_camera_scope.h): an ambient the UI
// SETS (once, on the Output-window seam) and the terminal Command executor (cookRenderTarget,
// point_ops_rendertarget.cpp) READS. It is the BASE clear color for a TERMINAL Command realize; a
// chain-leading ClearRenderTarget op still overrides it (faithful: TiXL clears BackgroundColor first,
// then slot.Update runs the chain). UNSET == the executor's own opaque-black default == byte-identical
// to before this seam — a Texture2D view never engages it (the picker is Command-only, UI side).
//
// WHY AN AMBIENT (not a PointGraph field): there is exactly one Output window / one cooked target, and
// this is SESSION VIEW STATE ("what color the empty view clears to"), not graph state — it must never
// touch the .swproj nor the 16-byte GPU EvaluationContext (the GPU struct stays frozen). The UI zone
// owns "which color is selected"; the app shell calls the setter; the runtime executor reads it. Tiny
// own-header (NOT the at-cap point_graph.h / point_ops.h, both on the line-count ratchet) so the
// executor + the app shell can both reach it without the god-headers.
#pragma once

namespace sw {

// Engage the view background (RGBA). After this, the terminal Command executor clears to {r,g,b,a}
// instead of its default black. The Output window calls this for a Command-type view.
void setCommandViewBackground(float r, float g, float b, float a);

// Disengage → back to the executor's opaque-black default (byte-identical to before this seam). The
// Output window calls this when the view is not a Command type (Texture2D / preview / nothing).
void clearCommandViewBackground();

// The engaged background as a borrowed float[4] RGBA, or nullptr when unset. Consulted by the terminal
// Command executor (cookRenderTarget). Borrowed; valid until the next set/clear.
const float* commandViewBackground();

}  // namespace sw
