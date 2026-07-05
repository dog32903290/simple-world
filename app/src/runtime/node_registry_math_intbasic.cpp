// runtime/node_registry_math_intbasic — self-registering MATH NodeSpec leaf: numbers/int/basic
// stateful choice ops (RandomChoiceIndex). New per-subfamily leaf (SW_MATH_SRCS glob — no CMake
// edit), mirror of node_registry_math_stateful.cpp's registrar rows.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

struct EvaluationContext;  // eval_context.h (fwd — the evaluate fn signature only takes a ref)

namespace sw {
namespace {

// IntToWrapmode evaluate fn. TiXL render/shading/IntToWrapmode.cs:18-21:
//   index = ModeIndex.GetValue(context).Clamp((int)Wrap=0, (int)MirrorOnce=4);  // Clamp = Min(Max(v,lo),hi)
//   Selected.Value = CastTo<TextureAddressMode>.From(index);
// TextureAddressMode ordinals (SharpDX): Wrap=0/Mirror=1/Clamp=2/Border=3/MirrorOnce=4. The enum has no
// home port type in sw → the ordinal rides the Float value-spine (value-spine (int) cast convention, like
// every int output). (int)in[0] truncates the int-slot wire. Local to this TU (only registrar consumer) so
// the at-cap value_eval_ops.cpp (line-count ratchet) is not touched.
float evalIntToWrapmode(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;
  int index = (int)in[0];
  index = index < 0 ? 0 : (index > 4 ? 4 : index);  // .Clamp(0, 4)
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

// IntToWrapmode — int ModeIndex → TextureAddressMode enum ordinal, clamped to [Wrap=0, MirrorOnce=4].
// TiXL render/shading/IntToWrapmode.cs:16-22. Pure value op (evalIntToWrapmode); the enum has no home
// port type → its ordinal rides the Float value-spine (value-spine (int) cast convention, same as every
// int output). ModeIndex is an int slot with the Enum widget's TextureAddressMode labels; .t3 default 0
// (= Wrap). Single output Selected FIRST, single input ModeIndex (evalFloat gathers input in[0] only).
static const MathOp _reg_IntToWrapmode{
    {"IntToWrapmode", "IntToWrapmode",
     {{"Selected", "Selected", "Float", false},
      {"ModeIndex", "ModeIndex", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Wrap", "Mirror", "Clamp", "Border", "MirrorOnce"}}},
     evalIntToWrapmode,
     "render.shading"}};

}  // namespace
}  // namespace sw
