// runtime/node_registry_math — NodeSpec table for MATH / VALUE ops.
// Ops: Time, AudioReaction, Const, Multiply, Sine, Add, Sub, Div, Clamp, Remap, Abs, Floor, Lerp.
// These are pure Float value-node ops plus stateful audio/time value sources.
// Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
#include "runtime/node_registry_math.h"
#include "runtime/graph.h"
#include "runtime/value_eval_ops.h"  // evalTime, evalConst, evalMultiply, etc.

namespace sw {

const std::vector<NodeSpec>& mathSpecs() {
  static const std::vector<NodeSpec> specs = {
      // --- Value nodes (Task 2) ---
      {"Time", "Time", {{"out", "out", "Float", false}}, evalTime},
      // TiXL AudioReaction (full parity): 3 outputs + 10 params. STATEFUL — cooked in main
      // from the live spectrum (runtime/audio_reaction) because it needs the whole spectrum
      // (too big for ctx) + per-node memory; so it has no pure evaluate() and evalFloat reads
      // its outputs from Node::outCache. Params are pinless (Inspector knobs, no canvas pins).
      {"AudioReaction", "AudioReaction",
       {{"Level", "Level", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"HitCount", "HitCount", "Float", false},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
        {"InputBand", "InputBand", "Float", true, 2.0f, 0.0f, 4.0f, Widget::Enum,
         {"RawFft", "NormalizedFft", "FrequencyBands", "Peaks", "Attacks"}, true},
        {"WindowCenter", "WindowCenter", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowWidth", "WindowWidth", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowEdge", "WindowEdge", "Float", true, 1.0f, 0.0001f, 1.0f, Widget::Slider, {}, true},
        {"Threshold", "Threshold", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.1f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Output", "Output", "Float", true, 3.0f, 0.0f, 4.0f, Widget::Enum,
         {"Pulse", "TimeSinceHit", "Count", "Level", "AccumulatedLevel"}, true},
        {"Bias", "Bias", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider, {}, true},
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      {"Const", "Const",
       {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalConst},
      {"Multiply", "Multiply",
       {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
        {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalMultiply},
      {"Sine", "Sine",
       {{"x", "x", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSine},
      // --- Math value ops (批次12 lane F; TiXL: Operators/Lib/numbers/float/{basic,adjust,process}/) ---
      // Add: Input1 + Input2 (TiXL Add.cs)
      {"Add", "Add",
       {{"Input1", "Input1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Input2", "Input2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalAdd},
      // Sub: Input1 - Input2 (TiXL Sub.cs)
      {"Sub", "Sub",
       {{"Input1", "Input1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Input2", "Input2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSub},
      // Div: A / B; B==0 → 0 (TiXL Div.cs; fork: NaN→0, see evalDiv comment)
      {"Div", "Div",
       {{"A", "A", "Float", true, 1.0f, -10.0f, 10.0f},
        {"B", "B", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalDiv},
      // Clamp: clamp(Value, Min, Max) (TiXL Clamp.cs; MathUtils.Clamp = Min(Max(v,min),max))
      {"Clamp", "Clamp",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Min", "Min", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max", "Max", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalClamp},
      // Remap: linear remap [RangeInMin..RangeInMax] → [RangeOutMin..RangeOutMax]
      // (TiXL adjust/Remap.cs; fork: BiasAndGain+Mode omitted — see evalRemap comment)
      {"Remap", "Remap",
       {{"Value",       "Value",       "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeInMin",  "RangeInMin",  "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeInMax",  "RangeInMax",  "Float", true, 1.0f, -10.0f, 10.0f},
        {"RangeOutMin", "RangeOutMin", "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeOutMax", "RangeOutMax", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalRemap},
      // Abs: |Value| (TiXL adjust/Abs.cs)
      {"Abs", "Abs",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalAbs},
      // Floor: truncate toward zero via (int) cast (TiXL adjust/Floor.cs; fork: trunc not floor)
      {"Floor", "Floor",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalFloor},
      // Lerp: A + (B-A)*F (TiXL process/Lerp.cs; fork: Clamp bool input omitted, always unclamped)
      {"Lerp", "Lerp",
       {{"A", "A", "Float", true, 0.0f, -10.0f, 10.0f},
        {"B", "B", "Float", true, 1.0f, -10.0f, 10.0f},
        {"F", "F", "Float", true, 0.5f, 0.0f,   1.0f},
        {"out", "out", "Float", false}},
       evalLerp},
      // [overnight-math] BEGIN specs
      // Sqrt: MathF.Sqrt(Value). TiXL Sqrt.cs. Fork: negative input → 0 (not NaN).
      {"Sqrt", "Sqrt",
       {{"Value", "Value", "Float", true, 1.0f, 0.0f, 100.0f},  // TiXL Sqrt.t3 DefaultValue=1.0
        {"Result", "Result", "Float", false}},
       evalSqrt},
      // Pow: Math.Pow(Value, Exponent). TiXL Pow.cs. No fork from TiXL logic.
      {"Pow", "Pow",
       {{"Value",    "Value",    "Float", true, 1.0f, -10.0f, 10.0f},  // TiXL Pow.t3 DefaultValue=1.0
        {"Exponent", "Exponent", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Result",   "Result",   "Float", false}},
       evalPow},
      // Modulo: floor-modulo. TiXL Modulo.cs. ModuloValue==0 → 0.
      {"Modulo", "Modulo",
       {{"Value",       "Value",       "Float", true, 0.0f, -100.0f, 100.0f},
        {"ModuloValue", "ModuloValue", "Float", true, 1.0f, -100.0f, 100.0f},
        {"Result",      "Result",      "Float", false}},
       evalModulo},
      // Ceil: Math.Ceiling(Value). TiXL Ceil.cs. No fork.
      {"Ceil", "Ceil",
       {{"Value",  "Value",  "Float", true, 0.0f, -100.0f, 100.0f},
        {"Result", "Result", "Float", false}},
       evalCeil},
      // SmoothStep: SmootherStep(Min,Max,Value) = Perlin fade t^3(6t^2-15t+10).
      // TiXL SmoothStep.cs uses MathUtils.SmootherStep (= Fade, NOT classic smoothstep).
      {"SmoothStep", "SmoothStep",
       {{"Min",    "Min",    "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max",    "Max",    "Float", true, 1.0f, -10.0f, 10.0f},
        {"Value",  "Value",  "Float", true, 1.0f, -10.0f, 10.0f},  // TiXL SmoothStep.t3 DefaultValue=1.0
        {"Result", "Result", "Float", false}},
       evalSmoothStep},
      // Log: Math.Log(Value, Base). TiXL Log.cs. Fork: value≤0/base≤0/base=1 → 0 (not NaN).
      {"Log", "Log",
       {{"Value",  "Value",  "Float", true, 1.0f, 0.0f, 100.0f},
        {"Base",   "Base",   "Float", true, 1.0f, 0.0f, 100.0f},  // TiXL Log.t3 DefaultValue=1.0 (base=1 caught by fork below)
        {"Result", "Result", "Float", false}},
       evalLog},
      // Cos: Math.Cos(Input). TiXL Cos.cs. No fork.
      {"Cos", "Cos",
       {{"Input",  "Input",  "Float", true, 0.0f, -10.0f, 10.0f},
        {"Result", "Result", "Float", false}},
       evalCos},
      // [overnight-math] END specs
      // [math-batch22] BEGIN specs
      // Round: quantize Value to StepsPerUnit steps with RoundRatio edge smoothing.
      // TiXL float/adjust/Round.cs. Ports from Round.cs (3 inputs) + Round.t3 defaults:
      //   StepsPerUnit default=1.0, RoundRatio default=0.0, Value default=0.0.
      // Fork: StepsPerUnit==0 → passthrough (TiXL: div-by-zero → NaN).
      {"Round", "Round",
       {{"Value",        "Value",        "Float", true, 0.0f, -100.0f, 100.0f},
        {"StepsPerUnit", "StepsPerUnit", "Float", true, 1.0f, 0.001f,  100.0f},
        {"RoundRatio",   "RoundRatio",   "Float", true, 0.0f, 0.0f,    1.0f},
        {"Result",       "Result",       "Float", false}},
       evalRound},
      // Atan2: atan2(Vector.x, Vector.y). TiXL float/trigonometry/Atan2.cs:
      //   "MathF.Atan2(v.X, v.Y)" — note argument order is (X, Y) not standard (Y, X).
      // Vec2 input decomposed into two Float ports (fork-atan2-arg-order named in evalAtan2).
      // Atan2.t3: Vector default {X:0, Y:0}.
      {"Atan2", "Atan2",
       {{"Vector.x", "Vector",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 2},
        {"Vector.y", "Vector.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalAtan2},
      // Sigmoid: 1/(1+e^(Stretch*Value)). TiXL float/adjust/Sigmoid.cs:
      //   "1f/(1+ MathF.Pow(MathF.E, pow * v))"
      // NOTE: NOT standard sigmoid; Stretch is multiplier (positive = decreasing S-curve).
      // Sigmoid.t3: Value default=1.0, Stretch default=1.0.
      {"Sigmoid", "Sigmoid",
       {{"Value",   "Value",   "Float", true, 1.0f, -10.0f, 10.0f},
        {"Stretch", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Result",  "Result",  "Float", false}},
       evalSigmoid},
      // AddVec3: Input1 + Input2 (component-wise). TiXL vec3/AddVec3.cs.
      // Each Vec3 decomposed into 3 Float ports (head Widget::Vec, vecArity=3).
      // Three output pins: Result.x / Result.y / Result.z.
      // AddVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"AddVec3", "AddVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalAddVec3},
      // SubVec3: Input1 - Input2 (component-wise). TiXL vec3/SubVec3.cs.
      // SubVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"SubVec3", "SubVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalSubVec3},
      // ScaleVector3: A * B * ScaleUniform (component-wise). TiXL vec3/ScaleVector3.cs:
      //   "Result.Value = a * b * u;"
      // ScaleVector3.t3: A default {1,1,1}, B default {1,1,1}, ScaleUniform default=1.0.
      {"ScaleVector3", "ScaleVector3",
       {{"A.x",          "A",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",          "A.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",          "A.z",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",          "B",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"B.y",          "B.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.z",          "B.z",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"ScaleUniform", "ScaleUniform","Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x",     "Result.x",    "Float", false},
        {"Result.y",     "Result.y",    "Float", false},
        {"Result.z",     "Result.z",    "Float", false}},
       evalScaleVector3},
      // [math-batch22] END specs
      // [math-batch23] BEGIN specs
      // Magnitude: length(Input). TiXL vec3/Magnitude.cs:
      //   "Result.Value = Input.GetValue(context).Length();"
      // Input decomposed into 3 Float ports. Single scalar output "Result".
      // Magnitude.t3: Input default {X:0, Y:0, Z:0}.
      {"Magnitude", "Magnitude",
       {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input.z", "Input.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",  "Result",  "Float", false}},
       evalMagnitude},
      // DotVec3: dot(Input1, Input2). TiXL vec3/DotVec3.cs:
      //   "Result.Value = Vector3.Dot(Input1.GetValue(context), Input2.GetValue(context));"
      // DotVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"DotVec3", "DotVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalDotVec3},
      // Vec3Distance: distance(Input1, Input2). TiXL vec3/Vec3Distance.cs:
      //   "Result.Value = Vector3.Distance(Input1.GetValue(context), Input2.GetValue(context));"
      // Vec3Distance.t3: both inputs default {X:0, Y:0, Z:0}.
      {"Vec3Distance", "Vec3Distance",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalVec3Distance},
      // Vector3Components: decompose Vec3 → X/Y/Z. TiXL vec3/Vector3Components.cs:
      //   "X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z;"
      // Vector3Components.t3: Value default {X:0, Y:0, Z:0}.
      {"Vector3Components", "Vector3Components",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"X", "X", "Float", false},
        {"Y", "Y", "Float", false},
        {"Z", "Z", "Float", false}},
       evalVector3Components},
      // RotateVector3: CreateFromAxisAngle(Axis, Angle°) * VectorA * Scale. TiXL vec3/RotateVector3.cs:
      //   "var angle = Angle.GetValue(context) / 180 * MathF.PI;"
      //   "var m = Matrix4x4.CreateFromAxisAngle(axis, angle);"
      //   "Result.Value = Vector3.TransformNormal(vec, m) * Scale.GetValue(context);"
      // RotateVector3.t3: VectorA default {1,0,0}, Angle default=0, Axis default {0,0,1}, Scale default=1.
      // Fork: fork-angle-degrees (Angle in degrees → radians). fork-axis-normalize.
      // Three output pins: Result.x / Result.y / Result.z.
      {"RotateVector3", "RotateVector3",
       {{"VectorA.x", "VectorA",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"VectorA.y", "VectorA.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"VectorA.z", "VectorA.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Angle",     "Angle",     "Float", true, 0.0f, -360.0f, 360.0f},
        {"Axis.x",    "Axis",      "Float", true, 0.0f, -1.0f,   1.0f,   Widget::Vec, {}, false, 3},
        {"Axis.y",    "Axis.y",    "Float", true, 0.0f, -1.0f,   1.0f,   Widget::Vec, {}, false, 1},
        {"Axis.z",    "Axis.z",    "Float", true, 1.0f, -1.0f,   1.0f,   Widget::Vec, {}, false, 1},
        {"Scale",     "Scale",     "Float", true, 1.0f, -10.0f,  10.0f},
        {"Result.x",  "Result.x",  "Float", false},
        {"Result.y",  "Result.y",  "Float", false},
        {"Result.z",  "Result.z",  "Float", false}},
       evalRotateVector3},
      // [math-batch23] END specs
      // [math-batch24] BEGIN specs
      // InvertFloat: (Invert?-1:1)*A. TiXL float/adjust/InvertFloat.cs.
      // TiXL InvertFloat.t3: A default=1.0, Invert default=true(1.0).
      // Invert is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
      {"InvertFloat", "InvertFloat",
       {{"A",      "A",      "Float", true, 1.0f, -100.0f, 100.0f},
        {"Invert", "Invert", "Float", true, 1.0f, 0.0f,    1.0f, Widget::Bool},
        {"Result", "Result", "Float", false}},
       evalInvertFloat},
      // CrossVec3: Vector3.Cross(Input1, Input2). TiXL vec3/CrossVec3.cs.
      // TiXL CrossVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"CrossVec3", "CrossVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalCrossVec3},
      // LerpVec3: Vector3.Lerp(A,B,F) with Clamp bool. TiXL vec3/LerpVec3.cs.
      // TiXL LerpVec3.t3: A default {0,0,0}, B default {0,0,0}, F default=0, Clamp default=false(0).
      // Clamp is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
      {"LerpVec3", "LerpVec3",
       {{"A.x",   "A",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",   "A.y",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",   "A.z",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",   "B",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"B.y",   "B.y",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.z",   "B.z",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"F",     "F",     "Float", true, 0.0f, 0.0f,    1.0f},
        {"Clamp", "Clamp", "Float", true, 0.0f, 0.0f,    1.0f, Widget::Bool},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalLerpVec3},
      // NormalizeVector3: normalize(A)*Factor; guard: length<=0.001 passthrough. TiXL vec3/NormalizeVector3.cs.
      // TiXL NormalizeVector3.t3: A default {0,0,0}, Factor default=1.0.
      // Fork: fork-normalize-zero-guard: TiXL's explicit >0.001f threshold.
      {"NormalizeVector3", "NormalizeVector3",
       {{"A.x",    "A",      "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",    "A.y",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",    "A.z",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Factor", "Factor", "Float", true, 1.0f, -10.0f,  10.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalNormalizeVector3},
      // RoundVec3: per-component Round/Floor/Ceil scaled by Precision. TiXL vec3/RoundVec3.cs.
      // TiXL RoundVec3.t3: Value default {0,0,0}, Precision default {1,1,1}, Mode default=0 (Round).
      // Mode is an Enum input (TiXL: Modes {Round,Floor,Ceiling}).
      // Fork: fork-roundvec3-precision-zero: Precision==0 → 0 (TiXL: NaN).
      {"RoundVec3", "RoundVec3",
       {{"Value.x",     "Value",       "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y",     "Value.y",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z",     "Value.z",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Precision.x", "Precision",   "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 3},
        {"Precision.y", "Precision.y", "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 1},
        {"Precision.z", "Precision.z", "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 1},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Round","Floor","Ceiling"}},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalRoundVec3},
      // AddVec2: Input1+Input2 (component-wise). TiXL vec2/AddVec2.cs.
      // TiXL AddVec2.t3: both inputs default {X:0, Y:0}.
      // Vec2 decomposed into 2 Float ports (head Widget::Vec vecArity=2).
      {"AddVec2", "AddVec2",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalAddVec2},
      // DotVec2: Vector2.Dot(Input1, Input2). TiXL vec2/DotVec2.cs.
      // TiXL DotVec2.t3: both inputs default {X:0, Y:0}. Single scalar output "Result".
      {"DotVec2", "DotVec2",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalDotVec2},
      // Vec2Magnitude: length(Input). TiXL vec3/Vec2Magnitude.cs (file lives in vec3/ folder in TiXL).
      // TiXL Vec2Magnitude.t3: Input default {X:0, Y:0}.
      {"Vec2Magnitude", "Vec2Magnitude",
       {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",  "Result",  "Float", false}},
       evalVec2Magnitude},
      // Vector2Components: decompose Vec2 → X, Y. TiXL vec2/Vector2Components.cs.
      // TiXL Vector2Components.t3: Value default {X:0, Y:0}.
      {"Vector2Components", "Vector2Components",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"X", "X", "Float", false},
        {"Y", "Y", "Float", false}},
       evalVector2Components},
      // ScaleVector2: A*B*UniformScale (component-wise). TiXL vec2/ScaleVector2.cs:
      //   "Result.Value = a * b * u;"
      // TiXL ScaleVector2.t3: A default {0,0}, B default {1,1}, UniformScale default=1.0.
      {"ScaleVector2", "ScaleVector2",
       {{"A.x",          "A",           "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y",          "A.y",         "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",          "B",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"B.y",          "B.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale","Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x",     "Result.x",    "Float", false},
        {"Result.y",     "Result.y",    "Float", false}},
       evalScaleVector2},
      // [math-batch24] END specs
  };
  return specs;
}

}  // namespace sw
