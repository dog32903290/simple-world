// runtime/node_registry_math_arithmetic — self-registering MATH NodeSpec leaf:
// scalar arithmetic / adjust / trig value ops (Const, Multiply, Sum, Sine, Add, Sub, Div,
// Clamp, Remap, Abs, Floor, Lerp, Sqrt, Pow, Modulo, Ceil, SmoothStep, Log, Cos, Round, Atan2, Sigmoid).
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

static const MathOp _reg_Const{
      {"Const", "Const",
       {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalConst,
       "numbers.float.basic"}  // category (experience-S0 demo, = TiXL Symbol.Namespace)
};

static const MathOp _reg_Multiply{
      {"Multiply", "Multiply",
       {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
        {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalMultiply,
       "numbers.float.basic"}
};

      // Sum — Σ of a MultiInput Float port (批次25 MultiInput seam). TiXL float/basic/Sum.cs.
      // The single "Input" port accepts N wires; eval expands them into in[] and evalSum reduces.
      // (PortSpec field 12 = multiInput=true.)
static const MathOp _reg_Sum{
      {"Sum", "Sum",
       {{"Result", "Result", "Float", false},
        {"Input", "Input", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1, true}},
       evalSum,
       "numbers.float.basic"}
};

static const MathOp _reg_Sine{
      {"Sine", "Sine",
       {{"x", "x", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSine,
       "numbers.float.trigonometry"}
};

      // --- Math value ops (批次12 lane F; TiXL: Operators/Lib/numbers/float/{basic,adjust,process}/) ---
      // Add: Input1 + Input2 (TiXL Add.cs)
static const MathOp _reg_Add{
      {"Add", "Add",
       {{"Input1", "Input1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Input2", "Input2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalAdd,
       "numbers.float.basic"}
};

      // Sub: Input1 - Input2 (TiXL Sub.cs)
static const MathOp _reg_Sub{
      {"Sub", "Sub",
       {{"Input1", "Input1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Input2", "Input2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSub,
       "numbers.float.basic"}
};

      // Div: A / B; B==0 → 0 (TiXL Div.cs; fork: NaN→0, see evalDiv comment)
static const MathOp _reg_Div{
      {"Div", "Div",
       {{"A", "A", "Float", true, 1.0f, -10.0f, 10.0f},
        {"B", "B", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalDiv,
       "numbers.float.basic"}
};

      // Clamp: clamp(Value, Min, Max) (TiXL Clamp.cs; MathUtils.Clamp = Min(Max(v,min),max))
static const MathOp _reg_Clamp{
      {"Clamp", "Clamp",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Min", "Min", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max", "Max", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalClamp,
       "numbers.float.adjust"}
};

      // Remap: linear remap [RangeInMin..RangeInMax] → [RangeOutMin..RangeOutMax]
      // (TiXL adjust/Remap.cs; fork: BiasAndGain+Mode omitted — see evalRemap comment)
static const MathOp _reg_Remap{
      {"Remap", "Remap",
       {{"Value",       "Value",       "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeInMin",  "RangeInMin",  "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeInMax",  "RangeInMax",  "Float", true, 1.0f, -10.0f, 10.0f},
        {"RangeOutMin", "RangeOutMin", "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeOutMax", "RangeOutMax", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalRemap,
       "numbers.float.adjust"}
};

      // Abs: |Value| (TiXL adjust/Abs.cs)
static const MathOp _reg_Abs{
      {"Abs", "Abs",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalAbs,
       "numbers.float.adjust"}
};

      // Floor: truncate toward zero via (int) cast (TiXL adjust/Floor.cs; fork: trunc not floor)
static const MathOp _reg_Floor{
      {"Floor", "Floor",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalFloor,
       "numbers.float.adjust"}
};

      // Lerp: A + (B-A)*F (TiXL process/Lerp.cs; fork: Clamp bool input omitted, always unclamped)
static const MathOp _reg_Lerp{
      {"Lerp", "Lerp",
       {{"A", "A", "Float", true, 0.0f, -10.0f, 10.0f},
        {"B", "B", "Float", true, 1.0f, -10.0f, 10.0f},
        {"F", "F", "Float", true, 0.5f, 0.0f,   1.0f},
        {"out", "out", "Float", false}},
       evalLerp,
       "numbers.float.process"}
};

      // [overnight-math] BEGIN specs
      // Sqrt: MathF.Sqrt(Value). TiXL Sqrt.cs. Fork: negative input → 0 (not NaN).
static const MathOp _reg_Sqrt{
      {"Sqrt", "Sqrt",
       {{"Value", "Value", "Float", true, 1.0f, 0.0f, 100.0f},  // TiXL Sqrt.t3 DefaultValue=1.0
        {"Result", "Result", "Float", false}},
       evalSqrt,
       "numbers.float.basic"}
};

      // Pow: Math.Pow(Value, Exponent). TiXL Pow.cs. No fork from TiXL logic.
static const MathOp _reg_Pow{
      {"Pow", "Pow",
       {{"Value",    "Value",    "Float", true, 1.0f, -10.0f, 10.0f},  // TiXL Pow.t3 DefaultValue=1.0
        {"Exponent", "Exponent", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Result",   "Result",   "Float", false}},
       evalPow,
       "numbers.float.basic"}
};

      // Modulo: floor-modulo. TiXL Modulo.cs. ModuloValue==0 → 0.
static const MathOp _reg_Modulo{
      {"Modulo", "Modulo",
       {{"Value",       "Value",       "Float", true, 0.0f, -100.0f, 100.0f},
        {"ModuloValue", "ModuloValue", "Float", true, 1.0f, -100.0f, 100.0f},
        {"Result",      "Result",      "Float", false}},
       evalModulo,
       "numbers.float.basic"}
};

      // Ceil: Math.Ceiling(Value). TiXL Ceil.cs. No fork.
static const MathOp _reg_Ceil{
      {"Ceil", "Ceil",
       {{"Value",  "Value",  "Float", true, 0.0f, -100.0f, 100.0f},
        {"Result", "Result", "Float", false}},
       evalCeil,
       "numbers.float.adjust"}
};

      // SmoothStep: SmootherStep(Min,Max,Value) = Perlin fade t^3(6t^2-15t+10).
      // TiXL SmoothStep.cs uses MathUtils.SmootherStep (= Fade, NOT classic smoothstep).
static const MathOp _reg_SmoothStep{
      {"SmoothStep", "SmoothStep",
       {{"Min",    "Min",    "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max",    "Max",    "Float", true, 1.0f, -10.0f, 10.0f},
        {"Value",  "Value",  "Float", true, 1.0f, -10.0f, 10.0f},  // TiXL SmoothStep.t3 DefaultValue=1.0
        {"Result", "Result", "Float", false}},
       evalSmoothStep,
       "numbers.float.process"}
};

      // Log: Math.Log(Value, Base). TiXL Log.cs. Fork: value≤0/base≤0/base=1 → 0 (not NaN).
static const MathOp _reg_Log{
      {"Log", "Log",
       {{"Value",  "Value",  "Float", true, 1.0f, 0.0f, 100.0f},
        {"Base",   "Base",   "Float", true, 1.0f, 0.0f, 100.0f},  // TiXL Log.t3 DefaultValue=1.0 (base=1 caught by fork below)
        {"Result", "Result", "Float", false}},
       evalLog,
       "numbers.float.basic"}
};

      // Cos: Math.Cos(Input). TiXL Cos.cs. No fork.
static const MathOp _reg_Cos{
      {"Cos", "Cos",
       {{"Input",  "Input",  "Float", true, 0.0f, -10.0f, 10.0f},
        {"Result", "Result", "Float", false}},
       evalCos,
       "numbers.float.trigonometry"}
};

      // [overnight-math] END specs
      // [math-batch22] BEGIN specs
      // Round: quantize Value to StepsPerUnit steps with RoundRatio edge smoothing.
      // TiXL float/adjust/Round.cs. Ports from Round.cs (3 inputs) + Round.t3 defaults:
      //   StepsPerUnit default=1.0, RoundRatio default=0.0, Value default=0.0.
      // Fork: StepsPerUnit==0 → passthrough (TiXL: div-by-zero → NaN).
static const MathOp _reg_Round{
      {"Round", "Round",
       {{"Value",        "Value",        "Float", true, 0.0f, -100.0f, 100.0f},
        {"StepsPerUnit", "StepsPerUnit", "Float", true, 1.0f, 0.001f,  100.0f},
        {"RoundRatio",   "RoundRatio",   "Float", true, 0.0f, 0.0f,    1.0f},
        {"Result",       "Result",       "Float", false}},
       evalRound,
       "numbers.float.adjust"}
};

      // Atan2: atan2(Vector.x, Vector.y). TiXL float/trigonometry/Atan2.cs:
      //   "MathF.Atan2(v.X, v.Y)" — note argument order is (X, Y) not standard (Y, X).
      // Vec2 input decomposed into two Float ports (fork-atan2-arg-order named in evalAtan2).
      // Atan2.t3: Vector default {X:0, Y:0}.
static const MathOp _reg_Atan2{
      {"Atan2", "Atan2",
       {{"Vector.x", "Vector",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 2},
        {"Vector.y", "Vector.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalAtan2,
       "numbers.float.trigonometry"}
};

      // Sigmoid: 1/(1+e^(Stretch*Value)). TiXL float/adjust/Sigmoid.cs:
      //   "1f/(1+ MathF.Pow(MathF.E, pow * v))"
      // NOTE: NOT standard sigmoid; Stretch is multiplier (positive = decreasing S-curve).
      // Sigmoid.t3: Value default=1.0, Stretch default=1.0.
static const MathOp _reg_Sigmoid{
      {"Sigmoid", "Sigmoid",
       {{"Value",   "Value",   "Float", true, 1.0f, -10.0f, 10.0f},
        {"Stretch", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Result",  "Result",  "Float", false}},
       evalSigmoid,
       "numbers.float.adjust"}
};

}  // namespace
}  // namespace sw
