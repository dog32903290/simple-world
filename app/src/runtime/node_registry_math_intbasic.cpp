// runtime/node_registry_math_intbasic — self-registering MATH NodeSpec leaf: numbers/int/basic
// stateful choice ops (RandomChoiceIndex). New per-subfamily leaf (SW_MATH_SRCS glob — no CMake
// edit), mirror of node_registry_math_stateful.cpp's registrar rows.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

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

}  // namespace
}  // namespace sw
