// runtime/node_registry_math_intbasic — self-registering MATH NodeSpec leaf: numbers/int/basic
// stateful choice ops (RandomChoiceIndex). New per-subfamily leaf (SW_MATH_SRCS glob — no CMake
// edit), mirror of node_registry_math_stateful.cpp's registrar rows.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

struct EvaluationContext;  // eval_context.h (fwd — the evaluate fn signature only takes a ref)

namespace sw {
namespace {

// IntToWrapmode evaluate fn. TiXL render/shading/IntToWrapmode.cs:18-21:
//   index = ModeIndex.GetValue(context).Clamp((int)Wrap, (int)MirrorOnce);  // Clamp = Min(Max(v,lo),hi)
//   Selected.Value = CastTo<TextureAddressMode>.From(index);
// ★TextureAddressMode is SharpDX.Direct3D11.TextureAddressMode — a direct mirror of the native
// D3D11_TEXTURE_ADDRESS_MODE enum, which is 1-BASED: Wrap=1, Mirror=2, Clamp=3, Border=4, MirrorOnce=5.
// So the .cs clamp is .Clamp(1, 5) and the OUTPUT ordinal is the real D3D11 value [1,5] the render-pass
// sampler consumes (NOT a 0-based UI index). The enum has no home port type in sw → the ordinal rides the
// Float value-spine (value-spine (int) cast). NOTE the input-slot default is 0 (IntToWrapmode.cs:25
// `new(0)`), BELOW the range → clamps UP to Wrap=1 at defaults. (int)in[0] truncates the int-slot wire.
// Local to this TU so the at-cap value_eval_ops.cpp (line-count ratchet) is untouched.
float evalIntToWrapmode(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 1.0f;  // no input → clamp of the 0 default → Wrap=1
  int index = (int)in[0];
  index = index < 1 ? 1 : (index > 5 ? 5 : index);  // .Clamp(1, 5) — D3D11 Wrap..MirrorOnce
  return (float)index;
}

// RandomChoiceIndex — deterministic no-consecutive-repeat random index in [0, Mod). TiXL
// numbers/int/basic/RandomChoiceIndex.cs. Stateful (evaluate==nullptr): the XxHash slice-buffer
// window (_lastBufferIndex/_modulo hysteresis) is per-instance cross-frame memory, cooked once per
// frame by frame_cook's stateful-value seam into extOut[0] (step fn:
// stateful_value_ops_randomchoice.cpp). Result output FIRST (extOut by port index).
// Int ports dissolve to Float (value-spine (int) cast convention). .t3 defaults: Value=0, Mod=1
// (Mod<2 → Result 0, TiXL's fresh-drop eval).
static const MathOp _reg_RandomChoiceIndex{
    {"RandomChoiceIndex", "RandomChoiceIndex",
     {{"Result", "Result", "Float", false},
      {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"Mod", "Mod", "Float", true, 1.0f, 0.0f, 100.0f, Widget::Slider}},
     nullptr,
     "numbers.int.basic"}};

// IntToWrapmode — int ModeIndex → D3D11 TextureAddressMode ordinal [Wrap=1..MirrorOnce=5] (see fn above).
// TiXL render/shading/IntToWrapmode.cs:16-22. Pure value op (evalIntToWrapmode); the enum has no home port
// type → its ordinal rides the Float value-spine (value-spine (int) cast). The Enum widget indexes labels
// BY VALUE (node_draw.cpp:116 labels[value]), and values are 1-based, so labels[0] is an unused placeholder
// and labels[1..5] are the modes. Default 1 (= Wrap; the .cs input-slot default 0 clamps up to 1). Single
// output Selected FIRST, single input ModeIndex (evalFloat gathers input in[0] only).
static const MathOp _reg_IntToWrapmode{
    {"IntToWrapmode", "IntToWrapmode",
     {{"Selected", "Selected", "Float", false},
      {"ModeIndex", "ModeIndex", "Float", true, 1.0f, 1.0f, 5.0f, Widget::Enum,
       {"", "Wrap", "Mirror", "Clamp", "Border", "MirrorOnce"}}},
     evalIntToWrapmode,
     "render.shading"}};

}  // namespace
}  // namespace sw
