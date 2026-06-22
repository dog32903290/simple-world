// runtime/node_registry_math_vector — self-registering MATH NodeSpec leaf:
// vec2 / vec3 value ops (component math, dot/cross, magnitude, lerp, normalize, rotate,
// remap, decompose) + InvertFloat.
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

      // --- clean stateless vec value ops (batch31, completeness tier). Multi-output: inputs first,
      // outputs after (eval component k = outIdx - n). ---
      // DivideVector2 — (A/B)/UniformScale component-wise. TiXL vec2/DivideVector2.cs. (.t3: A{0,0} B{1,1} U=1)
static const MathOp _reg_DivideVector2{
      {"DivideVector2", "DivideVector2",
       {{"A.x", "A",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y", "A.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x", "B",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"B.y", "B.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalDivideVector2}
};

      // Vec2ToVec3 — (XY.x, XY.y, Z). TiXL vec2/Vec2ToVec3.cs. (.t3: Z=0)
static const MathOp _reg_Vec2ToVec3{
      {"Vec2ToVec3", "Vec2ToVec3",
       {{"XY.x", "XY",  "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"XY.y", "XY.y","Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Z", "Z", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalVec2ToVec3}
};

      // EulerToAxisAngle — Rotation(radians) → Axis(unit vec3) + Angle(rad). TiXL vec3/EulerToAxisAngle.cs.
static const MathOp _reg_EulerToAxisAngle{
      {"EulerToAxisAngle", "EulerToAxisAngle",
       {{"Rotation.x", "Rotation",  "Float", true, 0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 3},
        {"Rotation.y", "Rotation.y","Float", true, 0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 1},
        {"Rotation.z", "Rotation.z","Float", true, 0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 1},
        {"Axis.x", "Axis.x", "Float", false},
        {"Axis.y", "Axis.y", "Float", false},
        {"Axis.z", "Axis.z", "Float", false},
        {"Angle", "Angle", "Float", false}},
       evalEulerToAxisAngle}
};

      // RemapVec2 — component remap, Mode(Normal/Clamped/Modulo). TiXL vec2/RemapVec2.cs.
      // (.t3: Value{0,0} RangeIn{0,0}/{1,1} RangeOut{0,0}/{1,1} Mode=0)
static const MathOp _reg_RemapVec2{
      {"RemapVec2", "RemapVec2",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeInMin.x", "RangeInMin",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeInMin.y", "RangeInMin.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeInMax.x", "RangeInMax",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeInMax.y", "RangeInMax.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeOutMin.x", "RangeOutMin",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeOutMin.y", "RangeOutMin.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeOutMax.x", "RangeOutMax",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeOutMax.y", "RangeOutMax.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Normal", "Clamped", "Modulo"}},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalRemapVec2}
};

      // PadVec2Range — pad/scale a [min,max] range (A.x=min,A.y=max) about center. TiXL vec2/
      // PadVec2Range.cs. (.t3: A{0,0} UniformScale=1 GuaranteedRange{0,0} ClampMinExtend=0)
static const MathOp _reg_PadVec2Range{
      {"PadVec2Range", "PadVec2Range",
       {{"A.x", "A",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y", "A.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, -100.0f, 100.0f},
        {"GuaranteedRange.x", "GuaranteedRange",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"GuaranteedRange.y", "GuaranteedRange.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"ClampMinExtend", "ClampMinExtend", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalPadVec2Range}
};

      // AddVec3: Input1 + Input2 (component-wise). TiXL vec3/AddVec3.cs.
      // Each Vec3 decomposed into 3 Float ports (head Widget::Vec, vecArity=3).
      // Three output pins: Result.x / Result.y / Result.z.
      // AddVec3.t3: both inputs default {X:0, Y:0, Z:0}.
static const MathOp _reg_AddVec3{
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
       evalAddVec3}
};

      // SubVec3: Input1 - Input2 (component-wise). TiXL vec3/SubVec3.cs.
      // SubVec3.t3: both inputs default {X:0, Y:0, Z:0}.
static const MathOp _reg_SubVec3{
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
       evalSubVec3}
};

      // ScaleVector3: A * B * ScaleUniform (component-wise). TiXL vec3/ScaleVector3.cs:
      //   "Result.Value = a * b * u;"
      // ScaleVector3.t3: A default {1,1,1}, B default {1,1,1}, ScaleUniform default=1.0.
static const MathOp _reg_ScaleVector3{
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
       evalScaleVector3}
};

      // [math-batch22] END specs
      // [math-batch23] BEGIN specs
      // Magnitude: length(Input). TiXL vec3/Magnitude.cs:
      //   "Result.Value = Input.GetValue(context).Length();"
      // Input decomposed into 3 Float ports. Single scalar output "Result".
      // Magnitude.t3: Input default {X:0, Y:0, Z:0}.
static const MathOp _reg_Magnitude{
      {"Magnitude", "Magnitude",
       {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input.z", "Input.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",  "Result",  "Float", false}},
       evalMagnitude}
};

      // DotVec3: dot(Input1, Input2). TiXL vec3/DotVec3.cs:
      //   "Result.Value = Vector3.Dot(Input1.GetValue(context), Input2.GetValue(context));"
      // DotVec3.t3: both inputs default {X:0, Y:0, Z:0}.
static const MathOp _reg_DotVec3{
      {"DotVec3", "DotVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalDotVec3}
};

      // Vec3Distance: distance(Input1, Input2). TiXL vec3/Vec3Distance.cs:
      //   "Result.Value = Vector3.Distance(Input1.GetValue(context), Input2.GetValue(context));"
      // Vec3Distance.t3: both inputs default {X:0, Y:0, Z:0}.
static const MathOp _reg_Vec3Distance{
      {"Vec3Distance", "Vec3Distance",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalVec3Distance}
};

      // Vector3Components: decompose Vec3 → X/Y/Z. TiXL vec3/Vector3Components.cs:
      //   "X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z;"
      // Vector3Components.t3: Value default {X:0, Y:0, Z:0}.
static const MathOp _reg_Vector3Components{
      {"Vector3Components", "Vector3Components",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"X", "X", "Float", false},
        {"Y", "Y", "Float", false},
        {"Z", "Z", "Float", false}},
       evalVector3Components}
};

      // RotateVector3: CreateFromAxisAngle(Axis, Angle°) * VectorA * Scale. TiXL vec3/RotateVector3.cs:
      //   "var angle = Angle.GetValue(context) / 180 * MathF.PI;"
      //   "var m = Matrix4x4.CreateFromAxisAngle(axis, angle);"
      //   "Result.Value = Vector3.TransformNormal(vec, m) * Scale.GetValue(context);"
      // RotateVector3.t3: VectorA default {1,0,0}, Angle default=0, Axis default {0,0,1}, Scale default=1.
      // Fork: fork-angle-degrees (Angle in degrees → radians). fork-axis-normalize.
      // Three output pins: Result.x / Result.y / Result.z.
static const MathOp _reg_RotateVector3{
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
       evalRotateVector3}
};

      // [math-batch23] END specs
      // [math-batch24] BEGIN specs
      // InvertFloat: (Invert?-1:1)*A. TiXL float/adjust/InvertFloat.cs.
      // TiXL InvertFloat.t3: A default=1.0, Invert default=true(1.0).
      // Invert is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
static const MathOp _reg_InvertFloat{
      {"InvertFloat", "InvertFloat",
       {{"A",      "A",      "Float", true, 1.0f, -100.0f, 100.0f},
        {"Invert", "Invert", "Float", true, 1.0f, 0.0f,    1.0f, Widget::Bool},
        {"Result", "Result", "Float", false}},
       evalInvertFloat}
};

      // CrossVec3: Vector3.Cross(Input1, Input2). TiXL vec3/CrossVec3.cs.
      // TiXL CrossVec3.t3: both inputs default {X:0, Y:0, Z:0}.
static const MathOp _reg_CrossVec3{
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
       evalCrossVec3}
};

      // LerpVec3: Vector3.Lerp(A,B,F) with Clamp bool. TiXL vec3/LerpVec3.cs.
      // TiXL LerpVec3.t3: A default {0,0,0}, B default {0,0,0}, F default=0, Clamp default=false(0).
      // Clamp is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
static const MathOp _reg_LerpVec3{
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
       evalLerpVec3}
};

      // NormalizeVector3: normalize(A)*Factor; guard: length<=0.001 passthrough. TiXL vec3/NormalizeVector3.cs.
      // TiXL NormalizeVector3.t3: A default {0,0,0}, Factor default=1.0.
      // Fork: fork-normalize-zero-guard: TiXL's explicit >0.001f threshold.
static const MathOp _reg_NormalizeVector3{
      {"NormalizeVector3", "NormalizeVector3",
       {{"A.x",    "A",      "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",    "A.y",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",    "A.z",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Factor", "Factor", "Float", true, 1.0f, -10.0f,  10.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalNormalizeVector3}
};

      // RoundVec3: per-component Round/Floor/Ceil scaled by Precision. TiXL vec3/RoundVec3.cs.
      // TiXL RoundVec3.t3: Value default {0,0,0}, Precision default {1,1,1}, Mode default=0 (Round).
      // Mode is an Enum input (TiXL: Modes {Round,Floor,Ceiling}).
      // Fork: fork-roundvec3-precision-zero: Precision==0 → 0 (TiXL: NaN).
static const MathOp _reg_RoundVec3{
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
       evalRoundVec3}
};

      // AddVec2: Input1+Input2 (component-wise). TiXL vec2/AddVec2.cs.
      // TiXL AddVec2.t3: both inputs default {X:0, Y:0}.
      // Vec2 decomposed into 2 Float ports (head Widget::Vec vecArity=2).
static const MathOp _reg_AddVec2{
      {"AddVec2", "AddVec2",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalAddVec2}
};

      // DotVec2: Vector2.Dot(Input1, Input2). TiXL vec2/DotVec2.cs.
      // TiXL DotVec2.t3: both inputs default {X:0, Y:0}. Single scalar output "Result".
static const MathOp _reg_DotVec2{
      {"DotVec2", "DotVec2",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalDotVec2}
};

      // Vec2Magnitude: length(Input). TiXL vec3/Vec2Magnitude.cs (file lives in vec3/ folder in TiXL).
      // TiXL Vec2Magnitude.t3: Input default {X:0, Y:0}.
static const MathOp _reg_Vec2Magnitude{
      {"Vec2Magnitude", "Vec2Magnitude",
       {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",  "Result",  "Float", false}},
       evalVec2Magnitude}
};

      // Vector2Components: decompose Vec2 → X, Y. TiXL vec2/Vector2Components.cs.
      // TiXL Vector2Components.t3: Value default {X:0, Y:0}.
static const MathOp _reg_Vector2Components{
      {"Vector2Components", "Vector2Components",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"X", "X", "Float", false},
        {"Y", "Y", "Float", false}},
       evalVector2Components}
};

      // ScaleVector2: A*B*UniformScale (component-wise). TiXL vec2/ScaleVector2.cs:
      //   "Result.Value = a * b * u;"
      // TiXL ScaleVector2.t3: A default {0,0}, B default {1,1}, UniformScale default=1.0.
static const MathOp _reg_ScaleVector2{
      {"ScaleVector2", "ScaleVector2",
       {{"A.x",          "A",           "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y",          "A.y",         "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",          "B",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"B.y",          "B.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale","Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x",     "Result.x",    "Float", false},
        {"Result.y",     "Result.y",    "Float", false}},
       evalScaleVector2}
};

}  // namespace
}  // namespace sw
