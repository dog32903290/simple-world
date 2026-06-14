// runtime/value_eval_ops — pure value-node evaluate functions (no GPU), mechanically split
// out of node_registry.cpp (批次12-F, ARCHITECTURE rule 4: <400). One fn per value op;
// in[] is ordered by the Float input ports in the spec; n is the count. TiXL citations and
// named forks live next to each definition in the .cpp.
#pragma once

// EvaluationContext is a GLOBAL-namespace dual-compiled struct (eval_context.h) — forward
// declared OUTSIDE sw on purpose; nesting it inside sw would mint a second, incomplete type.
struct EvaluationContext;

namespace sw {

float evalTime(int, const float*, int, const EvaluationContext& ctx);
float evalConst(int, const float* in, int n, const EvaluationContext&);
float evalMultiply(int, const float* in, int n, const EvaluationContext&);
float evalSine(int, const float* in, int n, const EvaluationContext&);
float evalAdd(int, const float* in, int n, const EvaluationContext&);
float evalSub(int, const float* in, int n, const EvaluationContext&);
float evalDiv(int, const float* in, int n, const EvaluationContext&);
float evalClamp(int, const float* in, int n, const EvaluationContext&);
float evalRemap(int, const float* in, int n, const EvaluationContext&);
float evalAbs(int, const float* in, int n, const EvaluationContext&);
float evalFloor(int, const float* in, int n, const EvaluationContext&);
float evalLerp(int, const float* in, int n, const EvaluationContext&);
// [overnight-math] BEGIN declarations
float evalSqrt(int, const float* in, int n, const EvaluationContext&);
float evalPow(int, const float* in, int n, const EvaluationContext&);
float evalModulo(int, const float* in, int n, const EvaluationContext&);
float evalCeil(int, const float* in, int n, const EvaluationContext&);
float evalSmoothStep(int, const float* in, int n, const EvaluationContext&);
float evalLog(int, const float* in, int n, const EvaluationContext&);
float evalCos(int, const float* in, int n, const EvaluationContext&);
// [overnight-math] END declarations
// [math-batch22] BEGIN declarations
// Round: quantize to N steps/unit with edge smoothing. TiXL float/adjust/Round.cs.
float evalRound(int outIdx, const float* in, int n, const EvaluationContext&);
// Atan2: atan2(x, y) from a Vec2. TiXL float/trigonometry/Atan2.cs.
// in: [Vector.x, Vector.y]; outIdx unused (single output).
float evalAtan2(int outIdx, const float* in, int n, const EvaluationContext&);
// Sigmoid: 1/(1+e^(stretch*v)). TiXL float/adjust/Sigmoid.cs.
// in: [Value, Stretch]; outIdx unused (single output).
float evalSigmoid(int outIdx, const float* in, int n, const EvaluationContext&);
// AddVec3: Input1 + Input2 (component-wise). TiXL vec3/AddVec3.cs.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]
// outIdx 0/1/2 → Result.x/.y/.z
float evalAddVec3(int outIdx, const float* in, int n, const EvaluationContext&);
// SubVec3: Input1 - Input2 (component-wise). TiXL vec3/SubVec3.cs.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]
// outIdx 0/1/2 → Result.x/.y/.z
float evalSubVec3(int outIdx, const float* in, int n, const EvaluationContext&);
// ScaleVector3: A * B * ScaleUniform (component-wise). TiXL vec3/ScaleVector3.cs.
// in: [A.x, A.y, A.z, B.x, B.y, B.z, ScaleUniform]
// outIdx 0/1/2 → Result.x/.y/.z
float evalScaleVector3(int outIdx, const float* in, int n, const EvaluationContext&);
// [math-batch22] END declarations
// [math-batch23] BEGIN declarations
// Magnitude: length(Input). TiXL vec3/Magnitude.cs.
// in: [Input.x, Input.y, Input.z]; outIdx unused (single scalar output).
float evalMagnitude(int outIdx, const float* in, int n, const EvaluationContext&);
// DotVec3: dot(Input1, Input2). TiXL vec3/DotVec3.cs.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]; outIdx unused.
float evalDotVec3(int outIdx, const float* in, int n, const EvaluationContext&);
// Vec3Distance: distance(Input1, Input2). TiXL vec3/Vec3Distance.cs.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]; outIdx unused.
float evalVec3Distance(int outIdx, const float* in, int n, const EvaluationContext&);
// Vector3Components: decompose Vec3 → X/Y/Z. TiXL vec3/Vector3Components.cs.
// in: [Value.x, Value.y, Value.z]; outIdx 3/4/5 → X/Y/Z component.
float evalVector3Components(int outIdx, const float* in, int n, const EvaluationContext&);
// RotateVector3: CreateFromAxisAngle(Axis, Angle°) * VectorA * Scale. TiXL vec3/RotateVector3.cs.
// in: [VectorA.x, VectorA.y, VectorA.z, Angle, Axis.x, Axis.y, Axis.z, Scale]
// outIdx 8/9/10 → Result.x/.y/.z
float evalRotateVector3(int outIdx, const float* in, int n, const EvaluationContext&);
// [math-batch23] END declarations
// [math-batch24] BEGIN declarations
// InvertFloat: (Invert?-1:1)*A. TiXL float/adjust/InvertFloat.cs.
// in: [A, Invert(Bool as Float: 0=false/1=true)]; outIdx unused (single output "Result").
float evalInvertFloat(int outIdx, const float* in, int n, const EvaluationContext&);
// CrossVec3: Vector3.Cross(Input1, Input2). TiXL vec3/CrossVec3.cs.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]
// outIdx → component k = outIdx - n (n=6); Result.x/y/z
float evalCrossVec3(int outIdx, const float* in, int n, const EvaluationContext&);
// LerpVec3: Vector3.Lerp(A,B,F) = A+(B-A)*F; Clamp bool optionally clamps F to [0,1].
// TiXL vec3/LerpVec3.cs. in: [A.x,A.y,A.z, B.x,B.y,B.z, F, Clamp(Bool as Float)]
// outIdx → component k = outIdx - n (n=8); Result.x/y/z
float evalLerpVec3(int outIdx, const float* in, int n, const EvaluationContext&);
// NormalizeVector3: normalize(A)*Factor; guard: length<=0.001 → passthrough (TiXL: a/=length only if >0.001).
// TiXL vec3/NormalizeVector3.cs. in: [A.x,A.y,A.z, Factor]
// outIdx → component k = outIdx - n (n=4); Result.x/y/z
float evalNormalizeVector3(int outIdx, const float* in, int n, const EvaluationContext&);
// RoundVec3: per-component Round/Floor/Ceil scaled by Precision. TiXL vec3/RoundVec3.cs.
// in: [Value.x,Value.y,Value.z, Precision.x,Precision.y,Precision.z, Mode(Enum: 0=Round,1=Floor,2=Ceiling)]
// outIdx → component k = outIdx - n (n=7); Result.x/y/z
float evalRoundVec3(int outIdx, const float* in, int n, const EvaluationContext&);
// AddVec2: Input1+Input2. TiXL vec2/AddVec2.cs.
// in: [Input1.x, Input1.y, Input2.x, Input2.y]; outIdx → component k = outIdx - n (n=4); Result.x/y
float evalAddVec2(int outIdx, const float* in, int n, const EvaluationContext&);
// DotVec2: Vector2.Dot(Input1,Input2). TiXL vec2/DotVec2.cs.
// in: [Input1.x, Input1.y, Input2.x, Input2.y]; outIdx unused (single scalar output).
float evalDotVec2(int outIdx, const float* in, int n, const EvaluationContext&);
// Vec2Magnitude: length(Input). TiXL vec3/Vec2Magnitude.cs (lives in vec3/ folder in TiXL).
// in: [Input.x, Input.y]; outIdx unused (single scalar output "Result").
float evalVec2Magnitude(int outIdx, const float* in, int n, const EvaluationContext&);
// Vector2Components: decompose Vec2 → X, Y. TiXL vec2/Vector2Components.cs.
// in: [Value.x, Value.y]; outIdx → component k = outIdx - n (n=2); X/Y outputs.
float evalVector2Components(int outIdx, const float* in, int n, const EvaluationContext&);
// ScaleVector2: A*B*UniformScale (component-wise). TiXL vec2/ScaleVector2.cs.
// in: [A.x, A.y, B.x, B.y, UniformScale]; outIdx → component k = outIdx - n (n=5); Result.x/y
float evalScaleVector2(int outIdx, const float* in, int n, const EvaluationContext&);
// [math-batch24] END declarations
// [math-batch25] BEGIN declarations
// Sum: Result = Σ in[0..n-1] over a MultiInput Float port (TiXL float/basic/Sum.cs: foreach
// collected input, Result += input; empty → 0). n = number of wired sources (1 = the unwired
// default 0). outIdx unused (single output). Needs the MultiInput seam (PortSpec.multiInput).
float evalSum(int outIdx, const float* in, int n, const EvaluationContext&);
// [math-batch25] END declarations

// [logic-batch27] BEGIN declarations — stateless float→bool(0/1) logic. Bool output dissolves to
// Float 0/1 (Cut 32: no Bool port type). TiXL float/logic/{IsGreater,Compare}.cs.
float evalIsGreater(int outIdx, const float* in, int n, const EvaluationContext&);
float evalCompare(int outIdx, const float* in, int n, const EvaluationContext&);
// [logic-batch27] END declarations

// [vec-batch31] clean stateless vec value ops (completeness tier). TiXL vec2/vec3.
float evalDivideVector2(int outIdx, const float* in, int n, const EvaluationContext&);
float evalVec2ToVec3(int outIdx, const float* in, int n, const EvaluationContext&);
float evalEulerToAxisAngle(int outIdx, const float* in, int n, const EvaluationContext&);
// [vec-batch31] END declarations
// [vec-batch32]
float evalRemapVec2(int outIdx, const float* in, int n, const EvaluationContext&);
}  // namespace sw
