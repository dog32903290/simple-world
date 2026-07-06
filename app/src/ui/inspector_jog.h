// ui/inspector_jog — the Inspector's Float knob "jog手感" (drag-to-scrub) helpers, split from
// inspector.cpp so that file stays under its line-count ratchet (one file one duty; ARCHITECTURE.md
// rule 4). Zone: ui. Header-only (tiny inline funcs called from exactly one TU) — no .cpp.
//
// These reproduce TiXL's parameter jog手感 on top of ImGui's DragFloat/DragScalarN:
//   - drag-to-scrub + double-click-to-type (= TiXL SingleValueEdit),
//   - sensitivity derived from the port's range (= TiXL FloatVectorInputValueUi.GetScaleFromRange),
//   - hard-clamp only when the port opts in (= TiXL default is NON-clamp; a drag may leave range),
//   - Shift=×10 / Alt=×0.01 fine/coarse modifiers for free (ImGui DragBehavior == TiXL SliderLadder).
#pragma once

#include <cmath>

#include "imgui.h"

#include "runtime/graph.h"  // PortSpec (scale / clamp / minV / maxV)

namespace sw::ui {

// Drag sensitivity (= TiXL FloatVectorInputValueUi.GetScaleFromRange, FloatVectorInputValueUi.cs
// :273-286): pixels-per-value for a Float knob. An explicit PortSpec.scale (>0) wins; otherwise it
// is DERIVED from [minV,maxV] — an "unbounded" range (TiXL's ±9999999 sentinel; here |bound|>=9999)
// falls back to 0.01f, a bounded range spreads |min-max|/100 across the drag. The common sw port
// keeps the struct default [0,1] → |0-1|/100 = 0.01f, so this reproduces the OLD hardcoded 0.01f
// exactly for every unconfigured knob; only ports with a real wide range now drag coarser.
inline float dragSpeedFor(const sw::PortSpec& p) {
  if (p.scale > 0.0f) return p.scale;                         // explicit Scale wins (TiXL: scale>0)
  if (p.minV < -9999.0f || p.maxV > 9999.0f) return 0.01f;    // "unbounded" — strict, = TiXL :280
  const float range = std::abs(p.minV - p.maxV);
  return range > 0.0f ? range / 100.0f : 0.01f;  // range==0 (min==max): guard div-by-0 → 0.01f
}

// Draw one Float knob as a jog-drag (= TiXL SingleValueEdit: scrub + double-click-to-type). Speed
// from dragSpeedFor; hard clamp ONLY when p.clamp (TiXL default is non-clamp — a drag can leave the
// range). Non-clamp passes nullptr min/max so ImGui treats the value as unbounded during the drag
// (imgui_widgets.cpp:3614 — Drag clamps only under AlwaysClamp). Shift=×10 / Alt=×0.01 fine/coarse
// come free from ImGui's DragBehavior (imgui_widgets.cpp:2445-2448), matching TiXL SliderLadder's
// modifier convention (SliderLadder.cs:129-144). Returns true if edited this frame.
inline bool jogDragFloat(const char* label, float* v, const sw::PortSpec& p) {
  const float speed = dragSpeedFor(p);
  if (p.clamp) {
    return ImGui::DragFloat(label, v, speed, p.minV, p.maxV, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  }
  return ImGui::DragScalar(label, ImGuiDataType_Float, v, speed, nullptr, nullptr, "%.2f");
}

// Vec sibling of jogDragFloat (DragScalarN owns all N components). Same speed + clamp semantics so
// scalar and Vec手感 never diverge.
inline bool jogDragScalarN(const char* label, float* vals, int N, const sw::PortSpec& p) {
  const float speed = dragSpeedFor(p);
  if (p.clamp) {
    return ImGui::DragScalarN(label, ImGuiDataType_Float, vals, N, speed, &p.minV, &p.maxV, "%.2f",
                              ImGuiSliderFlags_AlwaysClamp);
  }
  return ImGui::DragScalarN(label, ImGuiDataType_Float, vals, N, speed, nullptr, nullptr, "%.2f");
}

}  // namespace sw::ui
