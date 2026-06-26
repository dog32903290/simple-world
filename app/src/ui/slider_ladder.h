// ui/slider_ladder — precision value-edit overlay for Float/Vec drag interactions.
// Port of TiXL Editor/Gui/Interaction/SliderLadder.cs (Draw()) — draws a vertical stack
// of "range" rows (1000…0.001); the row the mouse hovers selects the active scale factor,
// horizontal mouse motion scrubs the value at that scale. Alt ×0.01, Shift ×10, Ctrl snap.
//
// Zone: ui (imgui draw only). Depends on imgui.h ONLY — no app/runtime/platform/verify.
// The caller (inspector.cpp) owns the value + undo flow; this only renders the overlay and
// (optionally) applies the ladder delta into editValue.
#pragma once

struct ImVec2;

namespace sw::ui {

// Call every frame while the owning Float/Vec drag widget IsItemActive().
//   editValue        : current value (by-ref; written back when the ladder applies a delta)
//   min/max          : clamp bounds (paired with clampMin/clampMax)
//   scale            : the param's default per-pixel drag step (TiXL `scale`); picks center range
//   timeSinceVisible : ImGui::GetTime() - (time the drag began); <0.2s = initial delay
//   clampMin/clampMax: whether to clamp at min / max
//   center           : ImGui-screen mouse-down point — the overlay origin
//   applyDelta       : if true the ladder writes editValue (full TiXL behavior); if false it
//                      only draws the overlay + modifier hints and leaves editValue untouched
//                      (simple_world v1: let the host DragFloat own the delta — fork below).
void drawSliderLadder(double& editValue, double min, double max,
                      float scale, float timeSinceVisible,
                      bool clampMin, bool clampMax,
                      ImVec2 center, bool applyDelta = false);

// Convenience for the inspector: if the just-drawn DragFloat/DragScalarN IsItemActive(), draw the
// overlay over the mouse-down point. Reads center from MouseClickedPos[0] and time-since-visible
// from `timeStart` (the GetTime() captured on IsItemActivated). v1 never applies the delta (the
// host drag widget owns the value), so this is overlay-only — collapses the per-call-site block
// to one line and keeps inspector.cpp under the 400-line ratchet.
void drawLadderIfActive(double minV, double maxV, double timeStart);

// Reset the persistent latch state (locked range / drag threshold / last-x). The host calls
// this on IsItemActivated() so a fresh drag starts clean — TiXL resets these via the
// timeSinceVisible<0.2 initial-delay branch; we expose it explicitly for the same effect.
void resetSliderLadder();

}  // namespace sw::ui
