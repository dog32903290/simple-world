// RemapFloatList floatlist op (floatlist self-registration seam leaf — List<float> -> List<float>, the
// per-element range remap with gain/bias curve). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/RemapFloatList.cs (verbatim below) +
// external/tixl/Core/Utils/MathUtils.cs:44-88 (GetBias / GetSchlickBias / ApplyGainAndBias).
//
//   RemapFloatList.cs Update():
//     var inputList = FloatList.GetValue(context);
//     if (inputList == null || inputList.Count == 0) { Result = []; return; }   // empty → empty
//     var inMin/inMax/outMin/outMax/biasAndGain/mode = inputs;
//     var inRange = inMax - inMin;
//     if (Abs(inRange) < 1e-5) { fill with outMin; return; }                    // degenerate input range
//     foreach value:
//       normalized = (value - inMin) / inRange;
//       if (normalized > 0 && normalized < 1)
//           normalized = normalized.ApplyGainAndBias(biasAndGain.X, biasAndGain.Y);  // X=gain, Y=bias
//       v = normalized * (outMax - outMin) + outMin;
//       switch (mode):
//         Clamped: v = v.Clamp(min(outMin,outMax), max(outMin,outMax));
//         Modulo : range = max-min; v = (|range|>1e-5) ? min + Fmod(v-min, range) : min;
//       result.Add(v);
//
//   ApplyGainAndBias(value, gain, bias)  (MathUtils.cs:65):
//     b = bias.Clamp(0,1); g = gain.Clamp(0,1);
//     if (value > 0.999f) return 1f;  if (value < 0.00001f) return 0f;
//     if (g < 0.5f) { value = GetBias(b,value); value = GetSchlickBias(g,value); }
//     else          { value = GetSchlickBias(g,value); value = GetBias(b,value); }
//     return value;
//   GetBias(b,x)        = x / ((1/b - 2)(1-x) + 1)                              (MathUtils.cs:44)
//   GetSchlickBias(g,x) : x<0.5 → 0.5*GetBias(g, 2x); else → 0.5*GetBias(1-g, 2x-1)+0.5  (MathUtils.cs:50)
//
//   Ports: BiasAndGain = Vector2 (X=gain, Y=bias); FloatList = List<float>; Mode = int enum
//          {Normal,Clamped,Modulo}; RangeInMax/RangeInMin/RangeOutMax/RangeOutMin = float.
//   Output: Result = Slot<List<float>>.
//
// EVAL-SIDE LAYOUT: FloatList input gathered as inputLists[0]; the 4 ranges + Mode (enum dissolve) are
// resolved Float params; BiasAndGain is a Vec2 param (Widget::Vec, vecArity 2 → two Float components
// "BiasAndGain.x"=gain, "BiasAndGain.y"=bias resolved like any scalar pair). STATELESS — closed-form.
//
// FORKS (named):
//   - fork-bias-gain-arg-order: TiXL calls `normalized.ApplyGainAndBias(biasAndGain.X, biasAndGain.Y)`
//     where ApplyGainAndBias(value, GAIN, BIAS) → so X=GAIN, Y=BIAS (NOT the obsolete ApplyBiasAndGain).
//     The gain/bias curve below is a VERBATIM transcription of MathUtils GetBias/GetSchlickBias (sw's
//     anim_math.h `schlickBias` is a DIFFERENT AnimMath curve — NOT reused; this leaf carries its own).
//   - fork-enum-dissolve: Mode {0=Normal,1=Clamped,2=Modulo} dissolves to a Float param (std::lround).
//   - fork-empty/degenerate: empty input → empty output; |inMax-inMin|<1e-5 → every element = outMin.
#include <algorithm>  // std::min, std::max
#include <cmath>      // std::fabs, std::lround

#include "runtime/anim_math.h"               // sw::fmodFloored (T3 MathUtils.Fmod — floored modulo)
#include "runtime/floatlist_op_registry.h"   // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// MathUtils.cs:44 — GetBias(b,x) = x / ((1/b - 2)(1-x) + 1).
float getBias(float b, float x) { return x / (((1.0f / b - 2.0f) * (1.0f - x)) + 1.0f); }

// MathUtils.cs:50 — GetSchlickBias(g,x): symmetric S-curve about 0.5.
float getSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * getBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * getBias(1.0f - g, x) + 0.5f;
  }
  return x;
}

// MathUtils.cs:65 — ApplyGainAndBias(value, gain, bias).
float applyGainAndBias(float value, float gain, float bias) {
  const float b = clamp01(bias);
  const float g = clamp01(gain);
  if (value > 0.999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = getBias(b, value);
    value = getSchlickBias(g, value);
  } else {
    value = getSchlickBias(g, value);
    value = getBias(b, value);
  }
  return value;
}

void cookRemapFloatList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  // Empty/absent input → empty output (RemapFloatList.cs:137-141).
  if (!c.inputLists || c.inputLists->empty() || (*c.inputLists)[0].empty()) {
    if (floatListInjectBug() && !c.output->empty()) c.output->pop_back();  // (no-op on empty; symmetry)
    return;
  }
  const std::vector<float>& in = (*c.inputLists)[0];

  const float inMin = floatListParam(c.params, "RangeInMin", 0.0f);
  const float inMax = floatListParam(c.params, "RangeInMax", 0.0f);
  const float outMin = floatListParam(c.params, "RangeOutMin", 0.0f);
  const float outMax = floatListParam(c.params, "RangeOutMax", 0.0f);
  const float gain = floatListParam(c.params, "BiasAndGain.x", 0.0f);  // Vector2.X = gain
  const float bias = floatListParam(c.params, "BiasAndGain.y", 0.0f);  // Vector2.Y = bias
  const int mode = (int)std::lround(floatListParam(c.params, "Mode", 0.0f));

  const float inRange = inMax - inMin;
  // Degenerate input range → fill with outMin (RemapFloatList.cs:154-164).
  if (std::fabs(inRange) < 0.00001f) {
    c.output->assign(in.size(), outMin);
  } else {
    c.output->reserve(in.size());
    for (float value : in) {
      float normalized = (value - inMin) / inRange;
      if (normalized > 0.0f && normalized < 1.0f)
        normalized = applyGainAndBias(normalized, gain, bias);  // X=gain, Y=bias
      float v = normalized * (outMax - outMin) + outMin;
      if (mode == 1) {  // Clamped
        const float mn = std::min(outMin, outMax);
        const float mx = std::max(outMin, outMax);
        v = v < mn ? mn : (v > mx ? mx : v);
      } else if (mode == 2) {  // Modulo (T3 floored Fmod)
        const float mn = std::min(outMin, outMax);
        const float mx = std::max(outMin, outMax);
        const float modRange = mx - mn;
        v = (std::fabs(modRange) > 0.00001f) ? (mn + anim_math::fmodFloored(v - mn, modRange)) : mn;
      }
      c.output->push_back(v);
    }
  }

  // Test-only: corrupt the REAL output (drop last) so the golden's RED bites on the actual cook path.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first; "FloatList" input; Mode (enum) + 4 range Floats + BiasAndGain (Vec2). A vec param
//          is N CONSECUTIVE Float ports with ids "<base>.x"/".y" (the HEAD carries vecArity=2, the tail
//          vecArity=1) — NOT one port with vecArity=2 (that leaves the components unresolved; see Group
//          golden). resolveNodeParams emits out["BiasAndGain.x"/".y"], read by the cook (X=gain, Y=bias).
static const FloatListOp _reg_remapfloatlist{
    {"RemapFloatList", "RemapFloatList",
     {{"out", "out", "FloatList", false},
      {"FloatList", "FloatList", "FloatList", true},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Normal", "Clamped", "Modulo"}, true},
      {"RangeInMin", "RangeInMin", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"RangeInMax", "RangeInMax", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"RangeOutMin", "RangeOutMin", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"RangeOutMax", "RangeOutMax", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"BiasAndGain.x", "BiasAndGain", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, /*vecArity=*/2},
      {"BiasAndGain.y", "BiasAndGain.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookRemapFloatList};

}  // namespace sw
