// DefineGradient gradient op (gradient self-registration seam leaf — the 8th flow's FIRST producer).
// TiXL authority: external/tixl/Operators/Lib/numbers/color/DefineGradient.cs (verbatim below) +
// its .t3 default mirror (DefineGradient.t3).
//
//   DefineGradient.cs Update():
//     _gradient.Steps.Clear();
//     foreach (slot in {Color1..Color4 / Pos1..Pos4}):
//       pos = slot.PositionSlot.GetValue(); if (pos < 0) continue;        // skip negative-pos stops
//       Steps.Add({ NormalizedPosition = pos, Color = slot.ColorSlot.GetValue() });
//     if (Steps.Count == 0): Steps.Add({ 0, Color1 });                    // empty fallback
//     SortHandles();
//     Interpolation = (Interpolations)Interpolation.GetValue();
//
//   Ports (DefineGradient.cs:66-91) + .t3 DefaultValue (DefineGradient.t3):
//     Color1 = Vector4 (1e-06, 9.9999e-07, 9.9999e-07, 1),  Color1Pos = 0.0
//     Color2 = Vector4 (1,1,1,1),                            Color2Pos = 1.0
//     Color3 = Vector4 (1,0,1,0),                            Color3Pos = -1.0   (default: skipped, pos<0)
//     Color4 = Vector4 (1,0,1,0),                            Color4Pos = -1.0   (default: skipped, pos<0)
//     Interpolation = int enum {Linear,Hold,Smooth,OkLab,Spline}, default 0 (Linear).
//   Output: OutGradient = Slot<Gradient> (the host gradient — the new Gradient channel's first producer).
//
// .t3 DEFAULT MIRROR (load-bearing — fresh-drop must match TiXL's fresh DefineGradient): the default
// produces a TWO-stop Linear gradient [ (0, ~black), (1, white) ] (Color3/Color4 skipped, pos=-1).
// (~black = (1e-06, 9.9999e-07, 9.9999e-07, 1) — TiXL's near-but-not-exactly-zero default Color1.)
//
// EVAL-SIDE LAYOUT: a pure PRODUCER — no Gradient input to gather. Every value comes from the
// RESOLVED Float params (the cook driver resolves all Float input ports, incl. each Color vec4's
// .x/.y/.z/.w components — boxsdf Center/Size precedent — and the scalar Pos / Interpolation enum).
//
// FORK (named): none beyond drop-Guid (the host SwGradient::Step has no Guid; DefineGradient.cs sets
// Step.Id = PositionSlot.Id, a UI-only identity carved out — see sw_gradient.h). Sample/sort parity
// is unaffected.
#include "runtime/gradient_op_registry.h"  // GradientOp / GradientCookCtx / gradientInjectBug / gradientParam
#include "runtime/graph.h"                  // NodeSpec, PortSpec, Widget

namespace sw {

int runGradientSelfTest(bool injectBug);

namespace {

// Read one Color vec4 from the resolved params (.x/.y/.z/.w), defaulting to the .t3 component values.
simd::float4 readColor(const std::map<std::string, float>* p, const char* base, simd::float4 def) {
  std::string b(base);
  return simd::make_float4(gradientParam(p, (b + ".x").c_str(), def.x),
                           gradientParam(p, (b + ".y").c_str(), def.y),
                           gradientParam(p, (b + ".z").c_str(), def.z),
                           gradientParam(p, (b + ".w").c_str(), def.w));
}

void cookDefineGradient(GradientCookCtx& c) {
  if (!c.output) return;
  SwGradient& g = *c.output;
  g.steps.clear();  // DefineGradient.cs:25 — _gradient.Steps.Clear()

  // The four (Color, Pos) slot defaults from DefineGradient.t3 (Color3/Color4 colors are the same
  // (1,0,1,0) per the .t3). Positions: Pos1=0, Pos2=1, Pos3=-1, Pos4=-1.
  const simd::float4 col1Def = simd::make_float4(1e-06f, 9.9999e-07f, 9.9999e-07f, 1.0f);
  const simd::float4 col2Def = simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f);
  const simd::float4 col34Def = simd::make_float4(1.0f, 0.0f, 1.0f, 0.0f);

  const char* colIds[4] = {"Color1", "Color2", "Color3", "Color4"};
  const char* posIds[4] = {"Color1Pos", "Color2Pos", "Color3Pos", "Color4Pos"};
  const simd::float4 colDefs[4] = {col1Def, col2Def, col34Def, col34Def};
  const float posDefs[4] = {0.0f, 1.0f, -1.0f, -1.0f};

  simd::float4 firstColor = readColor(c.params, colIds[0], colDefs[0]);  // for the empty fallback

  for (int i = 0; i < 4; ++i) {                               // DefineGradient.cs:26
    float pos = gradientParam(c.params, posIds[i], posDefs[i]);  // cs:28
    if (pos < 0) continue;                                    // cs:29-30 — skip negative-pos stops
    SwGradientStep step;
    step.pos = pos;                                           // cs:34
    step.color = readColor(c.params, colIds[i], colDefs[i]);  // cs:35
    g.steps.push_back(step);                                  // cs:32-37
  }

  if (g.steps.empty()) {                                      // cs:40 — empty fallback: stop at (0, Color1)
    SwGradientStep step;
    step.pos = 0.0f;                                          // cs:44
    step.color = firstColor;                                  // cs:45
    g.steps.push_back(step);                                  // cs:42-47
  }

  g.sortHandles();                                            // cs:49 — SortHandles()
  g.interpolation = (int)gradientParam(c.params, "Interpolation", 0.0f);  // cs:51 (enum int)

  // Test-only: corrupt the REAL output on the actual cook path (drop the last step) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (gradientInjectBug() && !g.steps.empty())
    g.steps.pop_back();
}

}  // namespace

// Self-registration. File-scope static GradientOp — independent leaf .cpp (no shared edit point).
// Feeds gradientSpecSink() + gradientCookFns() during pre-main dynamic init.
//   Color1..Color4 = vec4 heads (.x/.y/.z/.w, Widget::Vec vecArity=4 — boxsdf Center/Size precedent),
//   Color1Pos..Color4Pos = scalar Float, Interpolation = Float Widget::Enum (5 modes), out = Gradient.
static const GradientOp _reg_definegradient{
    {"DefineGradient", "DefineGradient",
     {// Color1 (.t3 default ~black) + Color1Pos = 0
      {"Color1.x", "Color1", "Float", true, 1e-06f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Color1.y", "Color1.y", "Float", true, 9.9999e-07f, -100.0f, 100.0f},
      {"Color1.z", "Color1.z", "Float", true, 9.9999e-07f, -100.0f, 100.0f},
      {"Color1.w", "Color1.w", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Color1Pos", "Color1Pos", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // Color2 (white) + Color2Pos = 1
      {"Color2.x", "Color2", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Color2.y", "Color2.y", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Color2.z", "Color2.z", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Color2.w", "Color2.w", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Color2Pos", "Color2Pos", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Slider},
      // Color3 (1,0,1,0) + Color3Pos = -1 (skipped by default)
      {"Color3.x", "Color3", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Color3.y", "Color3.y", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Color3.z", "Color3.z", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Color3.w", "Color3.w", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Color3Pos", "Color3Pos", "Float", true, -1.0f, -1.0f, 1.0f, Widget::Slider},
      // Color4 (1,0,1,0) + Color4Pos = -1 (skipped by default)
      {"Color4.x", "Color4", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Color4.y", "Color4.y", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Color4.z", "Color4.z", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Color4.w", "Color4.w", "Float", true, 0.0f, -100.0f, 100.0f},
      {"Color4Pos", "Color4Pos", "Float", true, -1.0f, -1.0f, 1.0f, Widget::Slider},
      // Interpolation enum (Gradient.Interpolations, DefineGradient.cs:90)
      {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Linear", "Hold", "Smooth", "OkLab", "Spline"}},
      // Output: the host Gradient (the new value channel's currency)
      {"out", "out", "Gradient", false}},
     /*evaluate=*/nullptr},  // Gradient output cannot ride NodeSpec::evaluate (returns ONE float)
    cookDefineGradient};

}  // namespace sw
