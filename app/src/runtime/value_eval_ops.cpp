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

}  // namespace sw
