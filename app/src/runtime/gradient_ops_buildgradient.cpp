// BuildGradient gradient op (gradient self-registration seam leaf — List<Vector4> colors + List<float>
// positions → Gradient, the list-currency-fed gradient PRODUCER). TiXL authority:
// external/tixl/Operators/Lib/numbers/color/BuildGradient.cs (verbatim below).
//
//   BuildGradient.cs Update():
//     _gradient.Steps.Clear();                                                  // :18
//     var colors    = Colors.GetValue(context)    ?? [];                        // :19  List<Vector4>
//     var positions = Positions.GetValue(context) ?? [];                        // :20  List<float>
//     if (positions.Count == 0) {                                               // :23  NORMALIZE fallback
//         if (_normalizedPositions.Count != colors.Count) { ... }              // :25-40
//         if (colors.Count == 1) add 0; else for i: add (float)i/(colors.Count-1);
//         positions = _normalizedPositions;
//     }
//     var minCount = Math.Min(colors.Count, positions.Count);                   // :45
//     if (minCount == 0) return;                                                // :46-49 keep prior (empty) gradient
//     for i in 0..minCount:                                                     // :51
//         Steps.Add({ NormalizedPosition = positions[i], Color = colors[i], Id = IntToGuid(i) }); // :54-62
//     _gradient.SortHandles();                                                  // :65
//     _gradient.Interpolation = (Interpolations)Interpolation.GetValue();       // :66
//
//   Inputs: Colors = InputSlot<List<Vector4>> (.cs:80-81), Positions = InputSlot<List<float>> (.cs:83-84),
//           Interpolation = InputSlot<int> enum (.cs:86-87). Output: OutGradient = Slot<Gradient> (.cs:6-7).
//
// EVAL-SIDE LAYOUT: the two list inputs ride sw's ColorList + FloatList host rails (the list-currency seam):
// cookFlatGradient's new ColorList/FloatList branches gather them into GradientCookCtx::inputColorList /
// inputFloatList (resident twin = cookResidentColorList/cookResidentFloatList). Interpolation is a resolved
// Float param (the enum int → gradientParam, same as DefineGradient.Interpolation).
//
// FORKS (named):
//   - buildgradient-drop-guid: TiXL sets Step.Id = IntToGuid(index) (a per-index Guid for the GradientEditor
//     UI). The host SwGradientStep has NO Guid (sw_gradient.h drop-Guid fork) — the Guid is UI-only; Sample /
//     SortHandles read only pos + color, so parity is unaffected (identical to DefineGradient's drop-Guid).
//   - buildgradient-positions-via-floatrail: Positions = List<float> arrives on sw's FloatList rail; Colors =
//     List<Vector4> on the ColorList rail (vec4 currency). Faithful in VALUE (per-index pos + color) — only
//     the wire currency name differs from a dedicated List<Vector4>/List<float> DX11 slot.
//   - buildgradient-empty-normalize: positions empty → evenly-normalized 0..1 (single color → pos 0). sw's
//     unwired FloatList → empty inputFloatList → this SAME fallback (identical to null→[] then normalize).
//     (sw recomputes the fallback each cook = TiXL's _normalizedPositions memo produces the SAME values; the
//     memo is a perf cache only, not a value fork — sw has no cross-frame state here, byte-identical output.)
#include <algorithm>  // std::min
#include <cstdint>
#include <vector>

#include <simd/simd.h>

#include "runtime/gradient_op_registry.h"  // GradientOp / GradientCookCtx / gradientInjectBug / gradientParam
#include "runtime/graph.h"                  // NodeSpec, PortSpec, Widget

namespace sw {
namespace {

// BuildGradient cook: zip Colors (inputColorList) with Positions (inputFloatList) into gradient steps, one
// per index up to min(count); positions empty → evenly-normalized 0..1 (single color → pos 0). SortHandles,
// set interpolation from the enum param. minCount==0 → leave the (empty) output untouched (.cs:46-49).
void cookBuildGradient(GradientCookCtx& c) {
  if (!c.output) return;
  SwGradient& g = *c.output;
  g.steps.clear();  // BuildGradient.cs:18 — _gradient.Steps.Clear()

  const std::vector<simd::float4> emptyC;
  const std::vector<float> emptyP;
  const std::vector<simd::float4>& colors = c.inputColorList ? *c.inputColorList : emptyC;  // .cs:19 ?? []
  const std::vector<float>& positionsIn = c.inputFloatList ? *c.inputFloatList : emptyP;    // .cs:20 ?? []

  // Positions empty → normalized 0..1 (.cs:23-42): single color → [0]; else i/(N-1) for i in 0..N.
  std::vector<float> normalized;
  const std::vector<float>* positions = &positionsIn;
  if (positionsIn.empty()) {
    const size_t n = colors.size();
    normalized.reserve(n);
    if (n == 1) {
      normalized.push_back(0.0f);                                     // .cs:31
    } else {
      for (size_t i = 0; i < n; ++i)
        normalized.push_back(n > 1 ? (float)i / (float)(n - 1) : 0.0f);  // .cs:36
    }
    positions = &normalized;
  }

  const size_t minCount = std::min(colors.size(), positions->size());  // .cs:45
  if (minCount == 0) {                                                  // .cs:46-49 — keep the (cleared) gradient
    // (Empty gradient; interpolation is NOT touched — faithful to the early return before SortHandles/Interp.)
    return;
  }

  g.steps.reserve(minCount);
  for (size_t i = 0; i < minCount; ++i) {           // .cs:51
    SwGradientStep step;
    step.pos = (*positions)[i];                     // .cs:54 NormalizedPosition = positions[i]
    step.color = colors[i];                         // .cs:55/60 Color = colors[i]  (Guid dropped — UI-only)
    g.steps.push_back(step);                        // .cs:57-62
  }

  g.sortHandles();                                                       // .cs:65 SortHandles()
  g.interpolation = (int)gradientParam(c.params, "Interpolation", 0.0f); // .cs:66 (enum int)

  // Test-only: corrupt the REAL output on the actual cook path (drop the last step) so the golden's RED case
  // bites here, NOT by flipping the expected value. Off in production.
  if (gradientInjectBug() && !g.steps.empty())
    g.steps.pop_back();
}

NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "BuildGradient";
  spec.title = "BuildGradient";
  spec.category = "numbers/color";
  spec.ports = {
      {"OutGradient", "OutGradient", "Gradient", false},  // output (the host Gradient, .cs:6-7 Slot<Gradient>)
      // Colors: TiXL InputSlot<List<Vector4>> (.cs:80-81). Rides sw's ColorList vec4 host rail
      // (fork buildgradient-positions-via-floatrail) → cookFlatGradient's ColorList branch → inputColorList.
      {"Colors", "Colors", "ColorList", true},
      // Positions: TiXL InputSlot<List<float>> (.cs:83-84). Rides sw's FloatList host rail → inputFloatList.
      {"Positions", "Positions", "FloatList", true},
      // Interpolation enum (Gradient.Interpolations, .cs:86-87 int) — same 5 modes as DefineGradient.
      {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Linear", "Hold", "Smooth", "OkLab", "Spline"}},
  };
  spec.evaluate = nullptr;  // Gradient output cannot ride NodeSpec::evaluate (returns ONE float)
  return spec;
}

const GradientOp _reg_buildgradient(makeSpec(), cookBuildGradient);

}  // namespace
}  // namespace sw
