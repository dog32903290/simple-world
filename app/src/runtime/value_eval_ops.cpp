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
// Closed form matches TiXL exactly — including the min>max asymmetric case where Max(v,min) >= min
// > max so the outer Min always collapses to max (result = max for every v when min > max).
float evalClamp(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  float v = in[0], lo = in[1], hi = in[2];
  return fminf(fmaxf(v, lo), hi);  // T.Min(T.Max(v,min),max) verbatim — MathUtils.cs:253
}

// Remap: maps value from [RangeInMin, RangeInMax] → [RangeOutMin, RangeOutMax] with optional
// BiasAndGain shaping and Mode (Normal/Clamped/Modulo). TiXL Remap.cs (adjust/).
// TiXL Remap.cs l.17-56 (verbatim logic):
//   normalized = (value - inMin) / (inMax - inMin)
//   if (normalized > 0 && normalized < 1) normalized = normalized.ApplyGainAndBias(bg.X, bg.Y)
//   v = normalized * (outMax - outMin) + outMin
//   case Clamped: v = Clamp(v, min(outMin,outMax), max(outMin,outMax))
//   case Modulo:  v = Fmod(v, max-min)
// BiasAndGain default = (0.5, 0.5) [Remap.t3: identity, i.e. no reshaping].
// Mode default = 0 (Normal = unclamped/unmodulo'd passthrough).
// in: [Value, RangeInMin, RangeInMax, RangeOutMin, RangeOutMax, BiasAndGain.x, BiasAndGain.y, Mode]
// (n<5 → return 0; n<6 → BiasAndGain defaults to identity (0.5,0.5); n<8 → Mode defaults to 0)

// ApplyGainAndBias helpers (MathUtils.cs verbatim — same algebra as point_ops_boxgradient.cpp).
static float remapGetBias(float b, float x) {
  return x / ((1.0f / b - 2.0f) * (1.0f - x) + 1.0f);
}
static float remapGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * remapGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * remapGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
// MathUtils.ApplyGainAndBias(value, gain, bias): identity at gain=0.5, bias=0.5.
// Clamps g and b to [0,1]; near-zero/near-one value shortcuts.
static float remapApplyGainAndBias(float value, float gain, float bias) {
  float b = (bias  < 0.0f ? 0.0f : (bias  > 1.0f ? 1.0f : bias));
  float g = (gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain));
  if (value > 0.999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = remapGetBias(b, value); value = remapGetSchlickBias(g, value); }
  else          { value = remapGetSchlickBias(g, value); value = remapGetBias(b, value); }
  return value;
}

float evalRemap(int, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const float value  = in[0];
  const float inMin  = in[1], inMax  = in[2];
  const float outMin = in[3], outMax = in[4];
  const float bgX = (n >= 7) ? in[5] : 0.5f;  // BiasAndGain.x — default 0.5 (identity)
  const float bgY = (n >= 7) ? in[6] : 0.5f;  // BiasAndGain.y — default 0.5 (identity)
  const int   mode = (n >= 8) ? (int)in[7] : 0;  // Mode — default 0 (Normal)

  const float dIn = inMax - inMin;
  float t = (dIn == 0.0f) ? 0.0f : (value - inMin) / dIn;
  // Remap.cs l.28-31: apply only when 0 < normalized < 1.
  if (t > 0.0f && t < 1.0f) {
    t = remapApplyGainAndBias(t, bgX, bgY);
  }
  float v = t * (outMax - outMin) + outMin;
  if (mode == 1) {  // Clamped: Clamp(v, min(outMin,outMax), max(outMin,outMax))
    const float lo = outMin < outMax ? outMin : outMax;
    const float hi = outMin < outMax ? outMax : outMin;
    v = v < lo ? lo : (v > hi ? hi : v);
  } else if (mode == 2) {  // Modulo: MathUtils.Fmod(v, max-min) = v - floor(v/delta)*delta
    const float lo = outMin < outMax ? outMin : outMax;
    const float hi = outMin < outMax ? outMax : outMin;
    const float delta = hi - lo;
    if (delta != 0.0f) v = v - delta * std::floor(v / delta);
  }
  return v;
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
// TiXL Lerp.cs l.19-21: if (Clamp.GetValue(context)) { f = f.Clamp(0, 1); }
// in: [A, B, F, Clamp]  (Clamp = bool dissolved to Float >0.5; default false = 0.0f)
float evalLerp(int, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  float f = in[2];
  if (n >= 4 && in[3] > 0.5f) {
    // Clamp.GetValue(context) true: f = MathUtils.Clamp(f, 0, 1)
    if (f < 0.0f) f = 0.0f;
    else if (f > 1.0f) f = 1.0f;
  }
  return in[0] + (in[1] - in[0]) * f;
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

// [math-batch24] BEGIN implementations

// InvertFloat: sign * A, sign = Invert ? -1 : 1. TiXL float/adjust/InvertFloat.cs:
//   "var sign = shouldInvert ? -1 : 1; Result.Value = sign * value;"
// Invert is a bool port (TiXL InputSlot<bool>); we store as float, interpret non-zero = true.
// TiXL InvertFloat.t3: A default=1.0, Invert default=true (1.0).
// in: [A, Invert]; outIdx unused (single scalar output "Result").
float evalInvertFloat(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  float a = in[0];
  // TiXL InvertFloat.cs line 17: "var sign = shouldInvert ? -1 : 1;"
  bool shouldInvert = (in[1] != 0.0f);
  float sign = shouldInvert ? -1.0f : 1.0f;
  return sign * a;
}

// CrossVec3: Vector3.Cross(Input1, Input2). TiXL vec3/CrossVec3.cs:
//   "Result.Value = Vector3.Cross(Input1.GetValue(context), Input2.GetValue(context));"
// C# Vector3.Cross: (a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X).
// TiXL CrossVec3.t3: both inputs default {X:0, Y:0, Z:0}.
// in: [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]
// outIdx → component k = outIdx - n (n=6); Result.x/y/z
float evalCrossVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 2) return 0.0f;
  float a[3] = {in[0], in[1], in[2]};
  float b[3] = {in[3], in[4], in[5]};
  // TiXL CrossVec3.cs: Vector3.Cross follows right-hand rule:
  // cross.x = a.y*b.z - a.z*b.y
  // cross.y = a.z*b.x - a.x*b.z
  // cross.z = a.x*b.y - a.y*b.x
  float cross[3] = {
    a[1]*b[2] - a[2]*b[1],
    a[2]*b[0] - a[0]*b[2],
    a[0]*b[1] - a[1]*b[0]
  };
  return cross[k];
}

// LerpVec3: Vector3.Lerp(A, B, F) with optional Clamp on F. TiXL vec3/LerpVec3.cs:
//   "if (Clamp.GetValue(context)) { f = f.Clamp(0, 1); }"
//   "Result.Value = Vector3.Lerp(A.GetValue(context), B.GetValue(context), f);"
// C# Vector3.Lerp = A + (B-A)*f (unclamped when Clamp=false).
// TiXL LerpVec3.t3: A default {0,0,0}, B default {0,0,0}, F default=0, Clamp default=false(0).
// in: [A.x,A.y,A.z, B.x,B.y,B.z, F, Clamp]
// outIdx → component k = outIdx - n (n=8); Result.x/y/z
float evalLerpVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 8) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 2) return 0.0f;
  float ax = in[0], ay = in[1], az = in[2];
  float bx = in[3], by = in[4], bz = in[5];
  float f = in[6];
  bool clamp = (in[7] != 0.0f);
  // TiXL LerpVec3.cs line 20-22: "if (Clamp.GetValue(context)) { f = f.Clamp(0, 1); }"
  if (clamp) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
  }
  // TiXL: Vector3.Lerp = A + (B-A)*f
  float res[3] = {ax + (bx-ax)*f, ay + (by-ay)*f, az + (bz-az)*f};
  return res[k];
}

// NormalizeVector3: normalize(A)*Factor with zero-guard. TiXL vec3/NormalizeVector3.cs:
//   "var length = a.Length();"
//   "if (length > 0.001f) { a /= length; }"
//   "Result.Value = a * f;"
// FORK (named): fork-normalize-zero-guard: length ≤ 0.001f → return A*Factor unchanged (no divide).
//   TiXL uses > 0.001f threshold explicitly. This preserves A as-is for near-zero vectors.
// TiXL NormalizeVector3.t3: A default {0,0,0}, Factor default=1.0.
// in: [A.x, A.y, A.z, Factor]; outIdx → component k = outIdx - n (n=4); Result.x/y/z
float evalNormalizeVector3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 2) return 0.0f;
  float ax = in[0], ay = in[1], az = in[2];
  float factor = in[3];
  float length = std::sqrt(ax*ax + ay*ay + az*az);
  // TiXL NormalizeVector3.cs line 22: "if (length > 0.001f) { a /= length; }"
  // fork-normalize-zero-guard: TiXL's explicit 0.001 threshold.
  if (length > 0.001f) {
    ax /= length;
    ay /= length;
    az /= length;
  }
  float res[3] = {ax * factor, ay * factor, az * factor};
  return res[k];
}

// RoundVec3: per-component Round/Floor/Ceil scaled by Precision. TiXL vec3/RoundVec3.cs:
//   "MathF.Round(v.X * precision.X) / precision.X"  (Mode=0 Round)
//   "MathF.Floor(v.X * precision.X) / precision.X"  (Mode=1 Floor)
//   "MathF.Ceiling(v.X * precision.X) / precision.X" (Mode=2 Ceiling)
// FORK (named): fork-roundvec3-precision-zero: Precision component==0 → return 0
//   (TiXL: Precision=0 → scale by 0 → div-by-zero → NaN; we return 0 for GPU/UI safety).
// TiXL RoundVec3.t3: Value default {0,0,0}, Precision default {1,1,1}, Mode default=0 (Round).
// in: [Value.x,Value.y,Value.z, Precision.x,Precision.y,Precision.z, Mode]
// outIdx → component k = outIdx - n (n=7); Result.x/y/z
float evalRoundVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 7) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 2) return 0.0f;
  float v = in[k];
  float p = in[3 + k];
  int mode = (int)in[6];
  // fork-roundvec3-precision-zero: Precision=0 → NaN in TiXL; return 0 here.
  if (p == 0.0f) return 0.0f;
  float scaled = v * p;
  float result;
  switch (mode) {
    case 0:  // Round — TiXL RoundVec3.cs: MathF.Round(v.X * precision.X) / precision.X
      result = std::round(scaled) / p;
      break;
    case 1:  // Floor
      result = std::floor(scaled) / p;
      break;
    case 2:  // Ceiling
      result = std::ceil(scaled) / p;
      break;
    default:
      result = 0.0f;
      break;
  }
  return result;
}

// AddVec2: Input1+Input2 (component-wise). TiXL vec2/AddVec2.cs:
//   "Result.Value = Input1.GetValue(context) + Input2.GetValue(context);"
// TiXL AddVec2.t3: both inputs default {X:0, Y:0}.
// in: [Input1.x, Input1.y, Input2.x, Input2.y]; outIdx → component k = outIdx - n (n=4); Result.x/y
float evalAddVec2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 1) return 0.0f;
  return in[k] + in[k + 2];
}

// DotVec2: Vector2.Dot(Input1, Input2). TiXL vec2/DotVec2.cs:
//   "Result.Value = Vector2.Dot(Input1.GetValue(context), Input2.GetValue(context));"
// C# Vector2.Dot = x1*x2 + y1*y2. No fork.
// TiXL DotVec2.t3: both inputs default {X:0, Y:0}.
// in: [Input1.x, Input1.y, Input2.x, Input2.y]; outIdx unused (single scalar output).
float evalDotVec2(int, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  return in[0]*in[2] + in[1]*in[3];
}

// Vec2Magnitude: length(Input). TiXL vec3/Vec2Magnitude.cs (lives in vec3/ folder in TiXL):
//   "Result.Value = Input.GetValue(context).Length();"
// C# Vector2.Length() = sqrt(x^2+y^2). No fork.
// TiXL Vec2Magnitude.t3: Input default {X:0, Y:0}.
// in: [Input.x, Input.y]; outIdx unused (single scalar output "Result").
float evalVec2Magnitude(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  float x = in[0], y = in[1];
  return std::sqrt(x*x + y*y);
}

// Vector2Components: decompose Vec2 → X, Y. TiXL vec2/Vector2Components.cs:
//   "X.Value = value.X; Y.Value = value.Y;"
// TiXL Vector2Components.t3: Value default {X:0, Y:0}.
// in: [Value.x, Value.y]; outIdx → component k = outIdx - n (n=2); X output = port 2, Y = port 3.
float evalVector2Components(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  int k = outIdx - n;  // 0 = X, 1 = Y
  if (k < 0 || k > 1) return 0.0f;
  return in[k];
}

// ScaleVector2: A*B*UniformScale (component-wise). TiXL vec2/ScaleVector2.cs:
//   "Result.Value = a * b * u;"
// TiXL ScaleVector2.t3: A default {0,0}, B default {1,1}, UniformScale default=1.0.
// in: [A.x, A.y, B.x, B.y, UniformScale]; outIdx → component k = outIdx - n (n=5); Result.x/y
float evalScaleVector2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  int k = outIdx - n;
  if (k < 0 || k > 1) return 0.0f;
  float u = in[4];
  return in[k] * in[k + 2] * u;
}

// [math-batch24] END implementations

// [math-batch25] BEGIN implementations
// Sum: Σ of all gathered MultiInput values. The resident gather expands the one multiInput port
// into in[0..n-1] (primary + extraConns). TiXL float/basic/Sum.cs: Result=0; foreach += input.
float evalSum(int, const float* in, int n, const EvaluationContext&) {
  float s = 0.0f;
  for (int i = 0; i < n; ++i) s += in[i];
  return s;
}
// [math-batch25] END implementations

// [logic-batch27] BEGIN implementations

// IsGreater: Result = (Value > Threshold) ? 1.0f : 0.0f.
// TiXL float/logic/IsGreater.cs: "var result = v > t; Result.Value = result;"
// Bool output dissolved to Float 0/1 (Cut 32 decision: no Bool port type in this runtime).
//
// NAMED FORK — fork-isgreater-stateless:
//   TiXL keeps a _lastResult field and only writes Result.Value when the bool flips
//   (lines 22-26: "if (result == _lastResult) return;"). This is a dirty-flag
//   optimization to avoid re-triggering downstream subscribers — NOT load-bearing
//   for the output value itself. Our frame model evaluates every node every frame
//   and produces the identical value regardless; storing _lastResult across frames
//   would require stateful infrastructure for what is semantically a pure function.
//   Therefore: implemented as pure STATELESS (no _lastResult), output is identical
//   every frame to what TiXL would emit once it has written the value.
//
// TiXL ports (IsGreater.cs):
//   Input:  Value (Float, no source default → 0.0f); Threshold (Float, no source default → 0.0f)
//   Output: Result (Slot<bool> → Float 0/1 here)
// in[0]=Value, in[1]=Threshold
float evalIsGreater(int, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  // TiXL IsGreater.cs line 20: "var result = v > t;"
  return (in[0] > in[1]) ? 1.0f : 0.0f;
}

// Compare: IsTrue = compare(Value, TestValue) based on Mode. Output 1.0f/0.0f.
// TiXL float/logic/Compare.cs: switch on Mode clamped to [0, len-1].
// Bool output dissolved to Float 0/1 (Cut 32 decision).
// Modes (TiXL Compare.Modes enum, zero-indexed):
//   0=IsSmaller: v < test
//   1=IsEqual:   fabs(v-test) < Precision
//   2=IsLarger:  v > test
//   3=IsNotEqual: fabs(v-test) >= Precision
//
// TiXL ports (Compare.cs):
//   Input:  Value (Float, no source default → 0.0f)
//           TestValue (Float, no source default → 0.0f)
//           Mode (InputSlot<int> mapped to Modes enum, default 0 = IsSmaller)
//           Precision (Float, default 0.001f — from InputSlot<float> ctor in .cs)
//   Output: IsTrue (Slot<bool> → Float 0/1 here)
// in[0]=Value, in[1]=TestValue, in[2]=Mode(as float enum index), in[3]=Precision
float evalCompare(int, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  float v = in[0], test = in[1], precision = in[3];
  // TiXL Compare.cs line 21: "(Modes)Mode.GetValue(context).Clamp(0, Enum.GetValues(...).Length-1)"
  // Length=4, so clamp to [0,3].
  int mode = (int)std::round(in[2]);
  if (mode < 0) mode = 0;
  if (mode > 3) mode = 3;
  switch (mode) {
    case 0:  // IsSmaller: v < test
      return (v < test) ? 1.0f : 0.0f;
    case 1:  // IsEqual: fabs(v-test) < Precision
      return (std::fabs(v - test) < precision) ? 1.0f : 0.0f;
    case 2:  // IsLarger: v > test
      return (v > test) ? 1.0f : 0.0f;
    case 3:  // IsNotEqual: fabs(v-test) >= Precision
      return (std::fabs(v - test) >= precision) ? 1.0f : 0.0f;
    default:
      return 0.0f;
  }
}

// [logic-batch27] END implementations

// [vec-batch31] BEGIN implementations — clean stateless vec value ops (completeness tier).
// Multi-output vec convention (same as evalAddVec3): inputs gathered into in[] in port order,
// outputs declared AFTER inputs, so component k = outIdx - n.

// DivideVector2 — Result = (A / B) / UniformScale, component-wise. TiXL vec2/DivideVector2.cs.
// in: A.x,A.y,B.x,B.y,UniformScale. Fork (fork-divvec2-divzero): B component==0 or U==0 → 0
// (same precedent as evalDiv; TiXL would yield ±inf/NaN).
float evalDivideVector2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const int k = outIdx - n;  // 0=x, 1=y
  if (k < 0 || k > 1) return 0.0f;
  const float a = in[k];
  const float b = in[2 + k];
  const float u = in[4];
  if (b == 0.0f || u == 0.0f) return 0.0f;
  return (a / b) / u;
}

// Vec2ToVec3 — Result = (XY.x, XY.y, Z). TiXL vec2/Vec2ToVec3.cs. in: XY.x,XY.y,Z.
float evalVec2ToVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int k = outIdx - n;  // 0=x,1=y,2=z
  if (k < 0 || k > 2) return 0.0f;
  return in[k];  // in[0]=XY.x, in[1]=XY.y, in[2]=Z map straight through
}

// EulerToAxisAngle — Rotation(radians: heading=X, attitude=Y, bank=Z) → Axis(unit vec3) + Angle(rad).
// TiXL vec3/EulerToAxisAngle.cs (euclideanspace conversion). Outputs: Axis.x/.y/.z (k 0/1/2), Angle (k 3).
// Computed in double like TiXL (Math.Cos/Sin/Acos/Sqrt) for parity. norm<0.001 → axis=(1,0,0) (gimbal guard).
float evalEulerToAxisAngle(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int k = outIdx - n;  // 0=Axis.x,1=Axis.y,2=Axis.z,3=Angle
  if (k < 0 || k > 3) return 0.0f;
  const double heading = in[0], attitude = in[1], bank = in[2];
  const double c1 = std::cos(heading / 2), s1 = std::sin(heading / 2);
  const double c2 = std::cos(attitude / 2), s2 = std::sin(attitude / 2);
  const double c3 = std::cos(bank / 2), s3 = std::sin(bank / 2);
  const double c1c2 = c1 * c2, s1s2 = s1 * s2;
  const double w = c1c2 * c3 - s1s2 * s3;
  double x = c1c2 * s3 + s1s2 * c3;
  double y = s1 * c2 * c3 + c1 * s2 * s3;
  double z = c1 * s2 * c3 - s1 * c2 * s3;
  const double angle = 2.0 * std::acos(w);
  double norm = x * x + y * y + z * z;
  if (norm < 0.001) { x = 1; y = 0; z = 0; }
  else { norm = std::sqrt(norm); x /= norm; y /= norm; z /= norm; }
  switch (k) {
    case 0: return (float)x;
    case 1: return (float)y;
    case 2: return (float)z;
    default: return (float)angle;  // k==3
  }
}
// [vec-batch31] END implementations

// [vec-batch32] BEGIN implementations.
// RemapVec2 — component-wise remap with Mode(Normal/Clamped/Modulo). TiXL vec2/RemapVec2.cs.
// in: Value.xy(0,1), RangeInMin.xy(2,3), RangeInMax.xy(4,5), RangeOutMin.xy(6,7), RangeOutMax.xy(8,9),
// Mode(10). Per component: factor=(v-inMin)/(inMax-inMin); out=factor*(outMax-outMin)+outMin; then mode.
// Fork: inMax==inMin → factor 0 (TiXL would div-by-zero); Modulo delta==0 → passthrough.
float evalRemapVec2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 11) return 0.0f;
  const int k = outIdx - n;  // 0=x, 1=y
  if (k < 0 || k > 1) return 0.0f;
  const float value = in[k];
  const float inMin = in[2 + k], inMax = in[4 + k];
  const float outMin = in[6 + k], outMax = in[8 + k];
  const int mode = (int)std::lround(in[10]);
  const float denom = inMax - inMin;
  const float factor = (denom != 0.0f) ? (value - inMin) / denom : 0.0f;
  float v = factor * (outMax - outMin) + outMin;
  if (mode == 1) {  // Clamped (TiXL MathUtils.Clamp(v, outMin, outMax) — assumes outMin<outMax, faithful)
    v = v < outMin ? outMin : (v > outMax ? outMax : v);
  } else if (mode == 2) {  // Modulo: MathUtils.Fmod(v, outMax-outMin)
    const float delta = outMax - outMin;
    if (delta != 0.0f) v = v - delta * std::floor(v / delta);
  }
  return v;  // Normal(0): unclamped passthrough
}
// PadVec2Range — pads/scales a [min,max] range (A.x=min, A.y=max) about its center: optional
// GuaranteedRange widens it, UniformScale scales each side from center, ClampMinExtend forces a
// minimum half-extent. TiXL vec2/PadVec2Range.cs. in: A.x,A.y, UniformScale, GR.x,GR.y, ClampMinExtend.
// Result.x and Result.y use DIFFERENT formulas (min side vs max side) — not symmetric component-wise.
float evalPadVec2Range(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  const int k = outIdx - n;  // 0=Result.x (min side), 1=Result.y (max side)
  if (k < 0 || k > 1) return 0.0f;
  float lo = in[0], hi = in[1];  // A.x=min, A.y=max
  const float u = in[2];
  const float grx = in[3], gry = in[4];
  const float minExtend = in[5];
  if (grx != 0.0f || gry != 0.0f) {  // TiXL: GuaranteedRange != Vector2.Zero
    lo = std::fmin(lo, grx);
    hi = std::fmax(hi, gry);
  }
  const float center = (lo + hi) * 0.5f;
  if (k == 0) return center + std::fmin((lo - center) * u, -minExtend);
  return center + std::fmax((hi - center) * u, minExtend);
}
// [vec-batch32] END implementations

// [blend-batch35] BlendValues — blend between a MultiInput<float> list by F. TiXL float/process/
// BlendValues.cs. The resident gather lays out in[] = [Values… (multiInput prefix), F (trailing
// regular port)], so the Values count = n-1 and F = in[n-1] (the eval-side mixed-multiInput
// convention: multiInput is the prefix, K trailing regular ports → segment = n-K). No gather change.
// count==0 (nothing wired) can't occur here — the gather always yields the primary default slot
// (in[0]=0) → blends to 0 = TiXL's empty→0, faithful. Fmod is the positive modulo (MathUtils.Fmod).
float evalBlendValues(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return n == 1 ? in[0] : 0.0f;  // need ≥1 Value + F
  const int count = n - 1;                   // exclude the trailing F
  const float f = in[n - 1];
  auto fmodPos = [](float v, float m) { return m != 0.0f ? v - m * std::floor(v / m) : 0.0f; };
  int i1 = (int)fmodPos((float)(int)f, (float)count);
  int i2 = (int)fmodPos((float)((int)f + 1), (float)count);
  const float mix = fmodPos(f, 1.0f);
  if (i1 < 0) i1 = 0; else if (i1 >= count) i1 = count - 1;  // safety clamp
  if (i2 < 0) i2 = 0; else if (i2 >= count) i2 = count - 1;
  return in[i1] + (in[i2] - in[i1]) * mix;  // MathUtils.Lerp(Values[i1], Values[i2], mix)
}
// [blend-batch35] END

}  // namespace sw
