// runtime/node_registry_math_stateful — self-registering MATH NodeSpec leaf:
// stateful smoothing value ops (Damp / Spring / Ease families + their vec variants,
// DeltaSinceLastFrame, FreezeValue, DampPeakDecay).
//
// Split from the 980-line node_registry_math.cpp (ratchet debt, ARCHITECTURE rule 4 + rule 7).
// Every spec below is moved VERBATIM from the old mathSpecs() manifest — name / ports / widgets /
// defaults / evaluate binding unchanged. Adding a math op here = drop a MathOp registrar; the
// central manifest is never touched again (mirror of the point-modify / image-filter / value-op
// self-registration sinks). Stateless ops carry their pure evaluate fn; stateful ops carry
// nullptr (cooked by frame_cook's stateful-value seam, dispatched by type name).
#include "runtime/graph.h"            // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink
#include "runtime/value_eval_ops.h"    // evalAdd, evalSine, evalClamp, … (pure value-node fns)

namespace sw {
namespace {

      // --- Stateful value ops (批次25 stateful-value seam) ---
      // Like AudioReaction: evaluate==nullptr (no pure fn — they keep per-instance memory across
      // frames); cooked once per frame by frame_cook::cookStatefulValueNodes into ResidentNode::
      // extOut; evalResidentFloat reads extOut[outputPortIndex] (generic no-evaluate path). The
      // step math + parity citations live in runtime/stateful_value_ops.cpp.
      // Damp — exponential / critically-damped smoothing toward a target. TiXL float/process/Damp.cs.
static const MathOp _reg_Damp{
      {"Damp", "Damp",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr,
       "numbers.float.process"}
};

      // DampAngle — Damp in angle space (re-targets through the shortest angular delta). TiXL
      // float/process/DampAngle.cs.
static const MathOp _reg_DampAngle{
      {"DampAngle", "DampAngle",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -360.0f, 360.0f},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr,
       "numbers.float.process"}
};

      // DampVec2 / DampVec3 — component-wise Damp. TiXL vec2/DampVec2.cs, vec3/DampVec3.cs.
      // Outputs FIRST (stateful path reads extOut by port index → Result.* must be ports 0..N-1).
static const MathOp _reg_DampVec2{
      {"DampVec2", "DampVec2",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr,
       "numbers.vec2"}
};

static const MathOp _reg_DampVec3{
      {"DampVec3", "DampVec3",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr,
       "numbers.vec3"}
};

      // DeltaSinceLastFrame — Value minus its value last frame. TiXL floats/process/DeltaSinceLastFrame.cs.
      // (Threshold port exists in TiXL but is unused by its math — kept for port parity.)
static const MathOp _reg_DeltaSinceLastFrame{
      {"DeltaSinceLastFrame", "DeltaSinceLastFrame",
       {{"Change", "Change", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "numbers.floats.process"}
};

      // FreezeValue — sample-and-hold; DeltaSinceFreeze = Value−frozen. TiXL float/process/FreezeValue.cs.
static const MathOp _reg_FreezeValue{
      {"FreezeValue", "FreezeValue",
       {{"Result", "Result", "Float", false},
        {"DeltaSinceFreeze", "DeltaSinceFreeze", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Freeze", "Freeze", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"FreezeWhileTrue", "UpdateWhenSwitchingToTrue"}}},
       nullptr,
       "numbers.float.process"}
};

      // Spring — spring physics toward a target (overshoots, settles). TiXL float/process/Spring.cs.
static const MathOp _reg_Spring{
      {"Spring", "Spring",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Tension", "Tension", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 0.5f, 0.0f, 4.0f}},
       nullptr,
       "numbers.float.process"}
};

      // SpringVec2 / SpringVec3 — component-wise Spring. TiXL vec2/process/SpringVec2.cs, vec3/process/SpringVec3.cs.
static const MathOp _reg_SpringVec2{
      {"SpringVec2", "SpringVec2",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Tension", "Tension", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 0.5f, 0.0f, 4.0f}},
       nullptr,
       "numbers.vec2.process"}
};

static const MathOp _reg_SpringVec3{
      {"SpringVec3", "SpringVec3",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Tension", "Tension", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 0.5f, 0.0f, 4.0f}},
       nullptr,
       "numbers.vec3.process"}
};

      // Ease — time-based eased re-target toward a changing input. TiXL float/process/Ease.cs.
      // Port order = TiXL InputSlot decl order MINUS the dropped UseAppRunTime (named fork): Value,
      // Duration, Direction(enum), Interpolation(enum). Duration default 1.0 (TiXL .cs has no source
      // default; SymbolJson supplies one — 1.0s is the faithful neutral). Stateful: evaluate=nullptr.
static const MathOp _reg_Ease{
      {"Ease", "Ease",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
       nullptr,
       "numbers.float.process"}
};

      // EaseVec2 / EaseVec3 — component-wise Ease (shared eased-t, no cross-channel bleed). TiXL
      // vec2/process/EaseVec2.cs, vec3/process/EaseVec3.cs. Result.* outputs FIRST (stateful path
      // reads extOut by port index), then Value.* (Vec convention), Duration, Direction, Interpolation.
static const MathOp _reg_EaseVec2{
      {"EaseVec2", "EaseVec2",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
       nullptr,
       "numbers.vec2.process"}
};

static const MathOp _reg_EaseVec3{
      {"EaseVec3", "EaseVec3",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
       nullptr,
       "numbers.vec3.process"}
};

      // DampPeakDecay — one-way peak follower: snaps UP to a rising input, decays DOWN by Decay (a Lerp).
      // VU-meter / peak-hold envelope. TiXL floats/process/DampPeakDecay.cs (stateful; output FIRST;
      // nullptr eval). Scalar despite the floats/ namespace. .t3 default: Decay=0.05.
static const MathOp _reg_DampPeakDecay{
      {"DampPeakDecay", "DampPeakDecay",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Decay", "Decay", "Float", true, 0.05f, 0.0f, 1.0f}},
       nullptr,
       "numbers.floats.process"}
};

}  // namespace
}  // namespace sw
