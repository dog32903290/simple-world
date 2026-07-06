// runtime/node_registry_math_keyframes — self-registering MATH NodeSpec leaf: the anim/utils keyframe-
// reflection ops FindKeyframes / SetKeyframes. They REFLECT across the AnimatedOp connection into a
// CONNECTED upstream operator's keyframe curves — read (FindKeyframes) or write (SetKeyframes). New
// per-subfamily leaf (SW_MATH_SRCS glob — no CMake edit).
//
// TiXL: numbers/anim/utils/FindKeyframes.cs (.t3), numbers/anim/utils/SetKeyframes.cs (.t3).
// Cooked by frame_cook's stateful-value seam via cookFindKeyframesNode / cookSetKeyframesNode
// (evaluate==nullptr; the cook needs the resident GRAPH — to follow the AnimatedOp connection — and,
// for SetKeyframes, a MUTABLE lib; stateful_value_ops_keyframes.cpp). Result/output ports FIRST
// (extOut by port index).
//
// .t3 defaults (FindKeyframes.t3 / SetKeyframes.t3, load-bearing — a fresh drop must match TiXL):
//   FindKeyframes: CurveIndex=0, WrapIndex=false, Mode=0 (Index), IndexOrTime=0, AnimatedOp=1.0, OpIndex=0.
//   SetKeyframes:  OpIndex=0, Value=0, CurveIndex=0, AnimatedOp=1.0, TriggerClear=false, TriggerSet=false.
// AnimatedOp is a MultiInput<float> (= TiXL MultiInputSlot): its DefaultValue 1.0 is the passthrough
// when unconnected (the op no-ops without a connection). Modeled as a multiInput Float port.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// PortSpec positional init: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
// vecArity}. AnimatedOp sets the trailing multiInput flag — a Vec-arity 1 (scalar) port that accepts
// N wires (OpIndex selects among the connected upstream ops). Widget defaults Slider; the trigger
// bools use Widget::Bool, the enums Widget::Enum.

static const MathOp _reg_FindKeyframes{
    {"FindKeyframes", "FindKeyframes",
     {{"Time", "Time", "Float", false},
      {"Value", "Value", "Float", false},
      {"KeyframeCount", "KeyframeCount", "Float", false},
      // AnimatedOp: MultiInput Float (field 12 = multiInput=true), = TiXL MultiInputSlot<float>.
      {"AnimatedOp", "AnimatedOp", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1,
       true},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"Index", "Nearest", "SampleAndDistance"}},
      {"IndexOrTime", "IndexOrTime", "Float", true, 0.0f, -1000.0f, 1000.0f},
      {"WrapIndex", "WrapIndex", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"OpIndex", "OpIndex", "Float", true, 0.0f, 0.0f, 16.0f},
      {"CurveIndex", "CurveIndex", "Float", true, 0.0f, 0.0f, 16.0f}},
     nullptr,
     "numbers.anim.utils"}};

static const MathOp _reg_SetKeyframes{
    {"SetKeyframes", "SetKeyframes",
     {{"CurrentValue", "CurrentValue", "Float", false},
      // AnimatedOp: MultiInput Float (field 12 = multiInput=true), = TiXL MultiInputSlot<float>.
      {"AnimatedOp", "AnimatedOp", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1,
       true},
      {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f},
      {"TriggerSet", "TriggerSet", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"TriggerClear", "TriggerClear", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"OpIndex", "OpIndex", "Float", true, 0.0f, 0.0f, 16.0f},
      {"CurveIndex", "CurveIndex", "Float", true, 0.0f, 0.0f, 16.0f}},
     nullptr,
     "numbers.anim.utils"}};

}  // namespace
}  // namespace sw
