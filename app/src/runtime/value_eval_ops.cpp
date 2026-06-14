// runtime/value_eval_ops — see header. Bodies moved VERBATIM from node_registry.cpp
// (批次12-F mechanical split); TiXL citations unchanged.
#include "runtime/value_eval_ops.h"

#include <cmath>

#include "runtime/eval_context.h"

namespace sw {

// ----- Value-node evaluate functions (pure value, no GPU). -----
// in[] is ordered by the Float input ports in the spec; n is the count.

float evalTime(int, const float*, int, const EvaluationContext& ctx) { return ctx.time; }
// AudioReaction is stateful (TiXL parity) and has no pure evaluate — it's cooked in main from
// the live spectrum into Node::outCache, which evalFloat returns directly (see below).
float evalConst(int, const float* in, int n, const EvaluationContext&) { return n > 0 ? in[0] : 0.0f; }
float evalMultiply(int, const float* in, int n, const EvaluationContext&) {
  return n >= 2 ? in[0] * in[1] : 0.0f;
}
float evalSine(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::sin(in[0]) : 0.0f;
}

// --- Math value-op evaluate fns (批次12 lane F) ---
// Each fn matches the TiXL source; see Operators/Lib/numbers/float/{basic,adjust,process}/.

// Add: Input1 + Input2 (TiXL Add.cs: Result = Input1 + Input2)
float evalAdd(int, const float* in, int n, const EvaluationContext&) {
  return n >= 2 ? in[0] + in[1] : 0.0f;
}

// Sub: Input1 - Input2 (TiXL Sub.cs: Result = Input1 - Input2)
float evalSub(int, const float* in, int n, const EvaluationContext&) {
  return n >= 2 ? in[0] - in[1] : 0.0f;
}

// Div: A / B; B==0 → 0.0f (FORK: TiXL Div.cs returns float.NaN; we return 0 to avoid
// NaN propagation through Metal/inspector; named fork per ARCHITECTURE rule — revisit if
// a NaN-sentinel mode is needed downstream).
// TiXL Div.cs: "Result.Value = b == 0 ? float.NaN : A.GetValue(context) / b;"
float evalDiv(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  return in[1] == 0.0f ? 0.0f : in[0] / in[1];
}

// Clamp: clamp(Value, Min, Max). TiXL Clamp.cs: MathUtils.Clamp(v, min, max) = Min(Max(v,min),max).
// When min > max the TiXL impl lets Max(v,min) win, so result >= min always — matching C++ behavior.
float evalClamp(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  float v = in[0], lo = in[1], hi = in[2];
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Remap: maps value from [RangeInMin, RangeInMax] → [RangeOutMin, RangeOutMax].
// TiXL Remap.cs (adjust/): normalized=(value-inMin)/(inMax-inMin), result=normalized*(outMax-outMin)+outMin.
// FORK (named): BiasAndGain and Mode omitted (both are identity defaults: BiasAndGain=(0.5,0.5)
// = linear passthrough; Mode=Normal = unclamped). Add when needed.
// in: [Value, RangeInMin, RangeInMax, RangeOutMin, RangeOutMax]
float evalRemap(int, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  float v = in[0], inMin = in[1], inMax = in[2], outMin = in[3], outMax = in[4];
  float dIn = inMax - inMin;
  float t = dIn == 0.0f ? 0.0f : (v - inMin) / dIn;
  return outMin + (outMax - outMin) * t;
}

// Abs: |Value|. TiXL Abs.cs: "Result.Value = v > 0 ? v : (-1*v);"
float evalAbs(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::fabs(in[0]) : 0.0f;
}

// Floor: truncate toward zero (cast to int). TiXL Floor.cs: "(int)Value.GetValue(context)".
// NOTE: TiXL uses C# (int) cast which truncates toward zero, NOT floor-toward-negative-infinity.
// We follow C# semantics exactly: (int) cast = truncf. Named fork if true floor is ever needed.
float evalFloor(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? (float)(int)in[0] : 0.0f;
}

// Lerp: A + (B-A)*F. TiXL Lerp.cs uses MathUtils.Lerp(a, b, f) = a + (b-a)*f.
// TiXL has a Clamp bool input (default false = no clamp on F). FORK (named): Clamp input omitted
// (always unclamped, matching TiXL default Clamp=false). Add when needed.
// in: [A, B, F]
float evalLerp(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  return in[0] + (in[1] - in[0]) * in[2];
}


// [overnight-math] BEGIN implementations
// Sqrt: MathF.Sqrt(v). TiXL Sqrt.cs: "Result.Value = MathF.Sqrt(v);"
// FORK (named): negative input → 0.0f instead of NaN; avoids NaN propagation through
// inspector/Metal uniforms. If a NaN-passthrough mode is needed downstream, add a flag.
float evalSqrt(int, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;
  float v = in[0];
  return v < 0.0f ? 0.0f : std::sqrt(v);
}

// Pow: Math.Pow(value, exponent). TiXL Pow.cs: "Result.Value = (float)Math.Pow(v,pow);"
// No fork: C++ std::pow matches C# Math.Pow IEEE semantics (Pow(0,0)=1, etc.).
// in: [Value, Exponent]
float evalPow(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  return std::pow(in[0], in[1]);
}

// Modulo: floor-modulo, v - mod*floor(v/mod). TiXL Modulo.cs:
//   "Result.Value = v - mod2 * (float)Math.Floor(v/mod2);"
// /0 → 0.0f (TiXL logs warning and returns 0; we match the output, not the log).
// in: [Value, ModuloValue]
float evalModulo(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  float v = in[0], m = in[1];
  if (m == 0.0f) return 0.0f;
  return v - m * std::floor(v / m);
}

// Ceil: Math.Ceiling(v). TiXL Ceil.cs: "Result.Value = (float)Math.Ceiling(v);"
// No fork: std::ceil matches C# Math.Ceiling exactly.
float evalCeil(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::ceil(in[0]) : 0.0f;
}

// SmoothStep: Ken Perlin smootherstep (TiXL calls MathUtils.SmootherStep, which uses Fade).
// TiXL SmoothStep.cs: "Result.Value = MathUtils.SmootherStep(Min, Max, Value);"
// MathUtils.SmootherStep → t=clamp((v-min)/(max-min),0,1); Fade(t)=t^3*(6t^2-15t+10).
// in: [Min, Max, Value]
float evalSmoothStep(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  float lo = in[0], hi = in[1], v = in[2];
  float denom = hi - lo;
  // FORK: min==max → t=0 (TiXL divides by zero here → NaN; we clamp to 0 for GPU/UI safety).
  float t = (denom == 0.0f) ? 0.0f : (v - lo) / denom;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  // Perlin's smootherstep (Fade): t^3*(6t^2 - 15t + 10)
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Log: Math.Log(value, base). TiXL Log.cs: "Result.Value = (float)Math.Log(v,newBase);"
// FORK (named): value ≤ 0 → 0.0f; base ≤ 0 or base == 1 → 0.0f.
// Avoids -inf/NaN/+inf propagation; C# Math.Log returns NaN for these same edge cases.
// in: [Value, Base]
float evalLog(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  float v = in[0], b = in[1];
  if (v <= 0.0f || b <= 0.0f || b == 1.0f) return 0.0f;
  return std::log(v) / std::log(b);
}

// Cos: Math.Cos(input). TiXL Cos.cs: "Result.Value = (float)Math.Cos(Input.GetValue(context));"
// No fork: std::cos matches exactly.
float evalCos(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::cos(in[0]) : 0.0f;
}
// [overnight-math] END implementations

// [math-batch22] BEGIN implementations

// Round: quantize Value to N StepsPerUnit with RoundRatio edge smoothing.
// TiXL float/adjust/Round.cs, RoundValue2 (verbatim, lines 23-34):
//   float u = 1 / stepsPerUnit;
//   float v = stepRatio / (2 * stepsPerUnit);
//   float m = i % u;
//   float r = m - (m < v ? 0 : m > u - v ? u : (m - v) / (1 - 2 * stepsPerUnit * v));
//   float y = i - r;
// RoundRatio=0 → v=0 → middle branch always → passthrough; RoundRatio=1 → hard quantization.
// FORK (named): stepsPerUnit==0 → return Value unchanged (avoids div/0; TiXL would NaN).
// FORK (named): denom (1-2*steps*v)==0 → return m unchanged in that sub-case (TiXL: div-by-zero).
// in: [Value, StepsPerUnit, RoundRatio]; outIdx unused (single output "Result").
float evalRound(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  float i = in[0], stepsPerUnit = in[1], stepRatio = in[2];
  // FORK: stepsPerUnit==0 → passthrough (TiXL: div-by-zero → NaN).
  if (stepsPerUnit == 0.0f) return i;
  float u = 1.0f / stepsPerUnit;
  float v = stepRatio / (2.0f * stepsPerUnit);
  float m = std::fmod(i, u);
  // C++ fmod keeps sign of dividend; TiXL C# uses % which also keeps sign for floats — match.
  float tval;  // the ternary value subtracted from m to get r
  if (m < v) {
    tval = 0.0f;
  } else if (m > u - v) {
    tval = u;
  } else {
    float denom = 1.0f - 2.0f * stepsPerUnit * v;
    // FORK: denom = 1 - 2*stepsPerUnit*v = 1 - stepRatio, so denom==0 when stepRatio==1
    // (TiXL there computes (m-v)/0 -> NaN at the measure-zero point m==u/2); we return 0.
    tval = (denom == 0.0f) ? 0.0f : (m - v) / denom;
  }
  float r = m - tval;
  return i - r;
}

// Atan2: atan2(x, y) from Vec2 input. TiXL float/trigonometry/Atan2.cs:
//   "Result.Value = MathF.Atan2(v.X, v.Y);"
// NOTE: TiXL passes (X, Y) not the standard (Y, X) — the argument order is literal from
// the source. This is TiXL's intentional convention (fork-atan2-arg-order named).
// in: [Vector.x, Vector.y] (two Float ports decomposed from the Vec2 input);
// outIdx unused (single output "Result").
float evalAtan2(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  // TiXL Atan2.cs line 17: "Result.Value = MathF.Atan2(v.X, v.Y);"
  // fork-atan2-arg-order: Atan2(X, Y) not Atan2(Y, X); faithful to TiXL source.
  return std::atan2(in[0], in[1]);
}

// Sigmoid: 1/(1+e^(stretch*v)). TiXL float/adjust/Sigmoid.cs:
//   "Result.Value = 1f/(1+ MathF.Pow(MathF.E, pow * v));"
// NOTE: this is NOT the standard logistic sigmoid 1/(1+e^(-v)); TiXL uses +pow*v not -v.
// With Stretch=-1 it recovers the standard sigmoid shape.
// No fork: formula is verbatim from TiXL. No edge cases (denominator ≥ 1 always).
// in: [Value, Stretch]; outIdx unused (single output "Result").
float evalSigmoid(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  float v = in[0], s = in[1];
  // TiXL Sigmoid.cs: "1f/(1+ MathF.Pow(MathF.E, pow * v))"
  return 1.0f / (1.0f + std::exp(s * v));
}

// AddVec3: Input1 + Input2 component-wise. TiXL vec3/AddVec3.cs:
//   "Result.Value = Input1.GetValue(context) + Input2.GetValue(context);"
// Decomposed: in[0..2] = Input1.{x,y,z}; in[3..5] = Input2.{x,y,z}.
// outIdx is the spec port index; component k = outIdx - n (n=6 Float inputs).
// Result.x is port 6, Result.y is port 7, Result.z is port 8.
float evalAddVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  int k = outIdx - n;  // port index → component (0/1/2)
  if (k < 0 || k > 2) return 0.0f;
  return in[k] + in[k + 3];
}

// SubVec3: Input1 - Input2 component-wise. TiXL vec3/SubVec3.cs:
//   "Result.Value = Input1.GetValue(context) - Input2.GetValue(context);"
// Decomposed: in[0..2] = Input1.{x,y,z}; in[3..5] = Input2.{x,y,z}.
// outIdx → component k = outIdx - n (n=6).
float evalSubVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 2) return 0.0f;
  return in[k] - in[k + 3];
}

// ScaleVector3: A * B * ScaleUniform (component-wise). TiXL vec3/ScaleVector3.cs:
//   "Result.Value = a * b * u;"
// Decomposed: in[0..2] = A.{x,y,z}; in[3..5] = B.{x,y,z}; in[6] = ScaleUniform.
// outIdx → component k = outIdx - n (n=7: 6 vec components + 1 scalar).
// Result.x is port 7, Result.y is port 8, Result.z is port 9.
float evalScaleVector3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 7) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 2) return 0.0f;
  float u = in[6];
  return in[k] * in[k + 3] * u;
}

// [math-batch22] END implementations

// [math-batch23] BEGIN implementations

// Magnitude: length of Input vector. TiXL vec3/Magnitude.cs:
//   "Result.Value = Input.GetValue(context).Length();"
// C# Vector3.Length() = sqrt(x^2+y^2+z^2). No fork needed: sqrt(0) = 0 is valid.
// FORK (named): fork-magnitude-zero-guard: length of zero vector = 0 is well-defined, no guard.
// in: [Input.x, Input.y, Input.z]; outIdx unused (single scalar output "Result").
float evalMagnitude(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  float x = in[0], y = in[1], z = in[2];
  return std::sqrt(x * x + y * y + z * z);
}

// DotVec3: dot(Input1, Input2). TiXL vec3/DotVec3.cs:
//   "Result.Value = Vector3.Dot(Input1.GetValue(context), Input2.GetValue(context));"
// C# Vector3.Dot = x1*x2 + y1*y2 + z1*z2. No fork.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]; outIdx unused.
float evalDotVec3(int, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  return in[0] * in[3] + in[1] * in[4] + in[2] * in[5];
}

// Vec3Distance: distance(Input1, Input2). TiXL vec3/Vec3Distance.cs:
//   "Result.Value = Vector3.Distance(Input1.GetValue(context), Input2.GetValue(context));"
// C# Vector3.Distance = length(Input1 - Input2). No fork.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]; outIdx unused.
float evalVec3Distance(int, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  float dx = in[0] - in[3], dy = in[1] - in[4], dz = in[2] - in[5];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Vector3Components: decompose Vec3 → X/Y/Z Float outputs. TiXL vec3/Vector3Components.cs:
//   "X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z;"
// in: [Value.x, Value.y, Value.z]; outIdx 3/4/5 → component 0/1/2.
// n=3 Float inputs; output ports start at index 3.
float evalVector3Components(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  int k = outIdx - n;  // output port index → component (0=X, 1=Y, 2=Z)
  if (k < 0 || k > 2) return 0.0f;
  return in[k];
}

// RotateVector3: rotate VectorA around Axis by Angle degrees, then scale. TiXL vec3/RotateVector3.cs:
//   "var angle = Angle.GetValue(context) / 180 * MathF.PI;"
//   "var m = Matrix4x4.CreateFromAxisAngle(axis, angle);"
//   "Result.Value = Vector3.TransformNormal(vec, m) * Scale.GetValue(context);"
// Angle port is in degrees; we convert to radians (fork-angle-degrees: named).
// FORK (named): fork-axis-normalize: Matrix4x4.CreateFromAxisAngle requires normalized axis.
//   TiXL passes axis as-is; if un-normalized, results differ. We normalize axis (length>0)
//   to match the C# Matrix4x4.CreateFromAxisAngle behavior which normalizes internally.
//   Zero-axis → identity matrix → result = VectorA * Scale (safe).
// in: [VectorA.x, VectorA.y, VectorA.z, Angle, Axis.x, Axis.y, Axis.z, Scale]
// outIdx 8/9/10 → Result.x/.y/.z
float evalRotateVector3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 8) return 0.0f;
  int k = outIdx - n;  // component (0=x, 1=y, 2=z)
  if (k < 0 || k > 2) return 0.0f;

  float vx = in[0], vy = in[1], vz = in[2];
  // TiXL RotateVector3.cs line 19: "var angle = Angle.GetValue(context) / 180 * MathF.PI;"
  // fork-angle-degrees: Angle port is degrees, convert to radians.
  float angle = in[3] / 180.0f * (float)M_PI;
  float ax = in[4], ay = in[5], az = in[6];
  float scale = in[7];

  // Normalize axis (required by CreateFromAxisAngle).
  // fork-axis-normalize: C# Matrix4x4.CreateFromAxisAngle normalizes axis internally.
  // Zero axis → identity (no rotation).
  float axisLen = std::sqrt(ax * ax + ay * ay + az * az);
  if (axisLen < 1e-8f) {
    // Zero axis → identity rotation → result = VectorA * Scale
    float v[3] = {vx, vy, vz};
    return v[k] * scale;
  }
  float nx = ax / axisLen, ny = ay / axisLen, nz = az / axisLen;

  // Rodrigues' rotation formula (equivalent to Matrix4x4.CreateFromAxisAngle + TransformNormal):
  // v_rot = v*cos(a) + (n×v)*sin(a) + n*(n·v)*(1-cos(a))
  float c = std::cos(angle), s = std::sin(angle);
  float ndotv = nx * vx + ny * vy + nz * vz;
  // n × v
  float crossX = ny * vz - nz * vy;
  float crossY = nz * vx - nx * vz;
  float crossZ = nx * vy - ny * vx;

  float rx = vx * c + crossX * s + nx * ndotv * (1.0f - c);
  float ry = vy * c + crossY * s + ny * ndotv * (1.0f - c);
  float rz = vz * c + crossZ * s + nz * ndotv * (1.0f - c);

  float result[3] = {rx * scale, ry * scale, rz * scale};
  return result[k];
}

// [math-batch23] END implementations

}  // namespace sw
