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


}  // namespace sw
