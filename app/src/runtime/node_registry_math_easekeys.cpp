// runtime/node_registry_math_easekeys — self-registering MATH NodeSpec leaf: the EaseKeys family
// (EaseKeys / EaseVec2Keys / EaseVec3Keys) — ease BETWEEN the keyframes of an animated input,
// replacing the curve's own interpolation. New per-subfamily leaf (SW_MATH_SRCS glob — no CMake
// edit), rows mirror the Ease family (node_registry_math_stateful.cpp).
//
// TiXL: numbers/float/process/EaseKeys.cs, vec2/process/EaseVec2Keys.cs, vec3/process/EaseVec3Keys.cs.
// Cooked by frame_cook's stateful-value seam via the cookEaseKeysNode hook (evaluate==nullptr; the
// cook needs the node's OWN Automation drivers — stateful_value_ops_easekeys.cpp). Result.* outputs
// FIRST (extOut by port index). .t3 defaults (EaseKeys.t3 / EaseVec2Keys.t3 / EaseVec3Keys.t3,
// load-bearing — a fresh drop must match TiXL): Value=0, Direction=2 (InOut), Interpolation=6 (Expo).
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

static const MathOp _reg_EaseKeys{
    {"EaseKeys", "EaseKeys",
     {{"Result", "Result", "Float", false},
      {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
      {"Direction", "Direction", "Float", true, 2.0f, 0.0f, 2.0f, Widget::Enum,
       {"In", "Out", "InOut"}},
      {"Interpolation", "Interpolation", "Float", true, 6.0f, 0.0f, 10.0f, Widget::Enum,
       {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
     nullptr,
     "numbers.float.process"}};

static const MathOp _reg_EaseVec2Keys{
    {"EaseVec2Keys", "EaseVec2Keys",
     {{"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Direction", "Direction", "Float", true, 2.0f, 0.0f, 2.0f, Widget::Enum,
       {"In", "Out", "InOut"}},
      {"Interpolation", "Interpolation", "Float", true, 6.0f, 0.0f, 10.0f, Widget::Enum,
       {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
     nullptr,
     "numbers.vec2.process"}};

static const MathOp _reg_EaseVec3Keys{
    {"EaseVec3Keys", "EaseVec3Keys",
     {{"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false},
      {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Direction", "Direction", "Float", true, 2.0f, 0.0f, 2.0f, Widget::Enum,
       {"In", "Out", "InOut"}},
      {"Interpolation", "Interpolation", "Float", true, 6.0f, 0.0f, 10.0f, Widget::Enum,
       {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
     nullptr,
     "numbers.vec3.process"}};

}  // namespace
}  // namespace sw
