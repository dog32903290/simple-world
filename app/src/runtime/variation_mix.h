// runtime/variation_mix — pure math for Lane L1 (Variation / Snapshot — the VJ live-performance core).
//
// SCOPE (harness-first batch): JUST the two math primitives the snapshot system rests on, ported
// VERBATIM from TiXL, plus enough structure for the golden to bite. The pool manager, UI canvas,
// document override and crossfader command loop are LATER L1 batches — none of that lives here.
//
// ZONE: runtime (pure computation, no upward deps, no GPU, no platform). Header-only; trivially
// inlinable. Matches the surrounding runtime style (plain C++ structs + free functions).
//
// ── springDamp ───────────────────────────────────────────────────────────────────────────────
// VERBATIM port of TiXL Core/Utils/MathUtils.cs:484-498 (SpringDamp). A critically-damped spring
// smooth-damp (Unity SmoothDamp cousin). `velocity` is INOUT carried across frames. The crossfader
// (Editor/.../BlendActions.cs:245-248) drives it with springConstant=20, timeStep=1/60 and treats
// |velocity| < 0.0005 as settled.
//
//   currentToTarget = target - current
//   springForce     = currentToTarget * springConstant
//   dampingForce    = -velocity * 2 * sqrt(springConstant)
//   force           = springForce + dampingForce
//   velocity       += force * timeStep
//   displacement    = velocity * timeStep
//   return current + displacement
//
// ── mix ──────────────────────────────────────────────────────────────────────────────────────
// VERBATIM port of the per-parameter weighted-average in TiXL
// Editor/Gui/Windows/Exploration/ExplorationVariation.cs:66-191 (Mix), for one float parameter:
//
//   value = Σ(neighbourValue[i] * weight[i]);  sumWeight = Σ weight[i]
//   value *= 1 / sumWeight                                  // normalized weighted average
//   value += random(-scatter, scatter) * parameterScale * scatterStrength
//
// MISSING-NEIGHBOUR FALLBACK (faithful to TiXL): when a neighbour does not carry this parameter,
// TiXL substitutes `param.InputSlot.Input.Value` — the CURRENT value — and STILL accumulates it
// into both `value` and `sumWeight` (it is matched as the right type before the += runs). So a
// missing neighbour contributes the current value AT ITS WEIGHT, not 0, and still counts toward
// sumWeight. We model that with a per-neighbour `present` flag + a `currentValue` fallback.
//
// SCATTER: defaults to 0 here → deterministic (the golden needs an exact answer). The TiXL random
// term is intentionally NOT ported in this batch; `scatter` is wired through so a later batch can
// add the RNG without changing this signature. With scatter==0 the random term vanishes exactly.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace sw {

// VERBATIM TiXL MathUtils.SpringDamp (Core/Utils/MathUtils.cs:484-498). `velocity` is INOUT.
inline float springDamp(float target,
                        float current,
                        float& velocity,
                        float springConstant = 2.0f,
                        float timeStep = 1.0f / 60.0f) {
  const float currentToTarget = target - current;
  const float springForce = currentToTarget * springConstant;
  const float dampingForce = -velocity * 2.0f * std::sqrt(springConstant);
  const float force = springForce + dampingForce;
  velocity += force * timeStep;
  const float displacement = velocity * timeStep;
  return current + displacement;
}

// One neighbour's contribution to a single float parameter. `present`=false → the neighbour does
// not carry this parameter → TiXL falls back to the current value (see header note); we substitute
// `currentValue` for `value` but the weight is still consumed.
struct MixNeighbour {
  float value = 0.0f;    // neighbour's value for this parameter (used when present)
  float weight = 0.0f;   // this neighbour's blend weight
  bool present = true;   // does this neighbour carry the parameter? false → use currentValue
};

// Per-parameter normalized weighted average over N weighted neighbours, faithful to TiXL Mix
// (ExplorationVariation.cs:66-191), float overload. `currentValue` is the missing-neighbour
// fallback (param.InputSlot.Input.Value). `scatter`/`parameterScale`/`scatterStrength` reproduce
// the TiXL post-term; with scatter==0 the term is exactly 0 (deterministic golden).
//
// Edge case (faithful): TiXL divides by sumWeight unconditionally. If sumWeight==0 (no neighbours,
// or all weights 0) that is a divide-by-zero in TiXL too; we guard it to return currentValue so the
// pure function never emits NaN on a degenerate input — a NAMED FORK (fork-mix-zero-sumweight-guard),
// strictly safer than TiXL and unreachable on any real weighted blend.
inline float mixFloat(const std::vector<MixNeighbour>& neighbours,
                     float currentValue,
                     float scatter = 0.0f,
                     float parameterScale = 1.0f,
                     float scatterStrength = 1.0f) {
  float value = 0.0f;
  float sumWeight = 0.0f;
  for (const MixNeighbour& nb : neighbours) {
    const float v = nb.present ? nb.value : currentValue;  // missing → current value (faithful)
    value += v * nb.weight;
    sumWeight += nb.weight;
  }
  if (sumWeight == 0.0f) return currentValue;  // fork-mix-zero-sumweight-guard (see note)
  value *= 1.0f / sumWeight;
  // scatter==0 → term is exactly 0. RNG deferred to a later batch (signature stable).
  (void)scatter; (void)parameterScale; (void)scatterStrength;
  return value;
}

// ── Typed N-way Mix (per TiXL ExplorationVariation.cs Vector2/3/4 branches :108-184) ─────────────
// Each Mix branch in TiXL is structurally IDENTICAL to the float branch — the same Σ(v·w)/Σw with the
// same missing-neighbour fallback (`matchingParam = param.InputSlot.Input.Value`, the CURRENT value).
// Only the accumulator's component count differs. So the typed overloads reuse mixFloat per component:
// component i blends over each neighbour's component-i value (or currentValue[i] when that neighbour is
// missing), and the SAME per-neighbour weight + present flag drive every component (faithful to TiXL,
// where the whole vector — not a single component — is missing or present as one unit).
//
// VEC NEIGHBOUR: one neighbour's contribution to an N-component vector parameter — `value[0..n-1]`,
// one `weight`, one `present` flag (a vector is present/missing as a unit, like the TiXL TryGetValue).
struct MixNeighbourVec {
  float value[4] = {0, 0, 0, 0};
  float weight = 0.0f;
  bool present = true;  // false → use currentValue[] for every component (missing-neighbour fallback)
};

namespace detail {
// Blend one component `comp` of an N-vec by reusing mixFloat: lift each MixNeighbourVec to a scalar
// MixNeighbour carrying that component (or currentValue[comp] when absent). Identical normalize +
// fallback as the float Mix — the typed overloads are pure projections of mixFloat, no new math.
inline float mixVecComponent(const std::vector<MixNeighbourVec>& neighbours,
                             const float currentValue[4], int comp) {
  std::vector<MixNeighbour> proj;
  proj.reserve(neighbours.size());
  for (const MixNeighbourVec& nb : neighbours)
    proj.push_back(MixNeighbour{nb.value[comp], nb.weight, nb.present});
  return mixFloat(proj, currentValue[comp]);
}
}  // namespace detail

// Vec2/Vec3/Vec4 N-way weighted average — TiXL ExplorationVariation.cs Vector2/3/4 Mix branches.
// `out`/`currentValue` are [2]/[3]/[4]; the missing-neighbour fallback substitutes currentValue per
// component AT the neighbour's weight (never 0), exactly as the float Mix above.
inline void mixVec2(const std::vector<MixNeighbourVec>& neighbours, const float currentValue[2],
                    float out[2]) {
  for (int c = 0; c < 2; ++c) out[c] = detail::mixVecComponent(neighbours, currentValue, c);
}
inline void mixVec3(const std::vector<MixNeighbourVec>& neighbours, const float currentValue[3],
                    float out[3]) {
  for (int c = 0; c < 3; ++c) out[c] = detail::mixVecComponent(neighbours, currentValue, c);
}
inline void mixVec4(const std::vector<MixNeighbourVec>& neighbours, const float currentValue[4],
                    float out[4]) {
  for (int c = 0; c < 4; ++c) out[c] = detail::mixVecComponent(neighbours, currentValue, c);
}

// int N-way Mix. FAITHFUL-EXTENSION (named): TiXL's Mix() loop itself has NO int branch (only
// float/vec2/vec3/vec4 — ExplorationVariation.cs:87-184), but ValueUtils.cs:72-80 defines the int
// blend method as `(int)MathUtils.Lerp((float)a,(float)b,t)` — truncating. We carry the int Mix the
// SAME way the snapshot crossfader already truncates (variation_pool.h VariationValue int): accumulate
// the normalized weighted average in float (Σ(v·w)/Σw, same missing-neighbour fallback), then TRUNCATE
// to int once at the end — matching TiXL's int Lerp truncation discipline. Single truncation at the
// end, not per-term, so the average is computed exactly before flooring (no intermediate rounding).
inline int mixInt(const std::vector<MixNeighbour>& neighbours, int currentValue) {
  const float avg = mixFloat(neighbours, static_cast<float>(currentValue));
  return static_cast<int>(avg);  // truncating, per TiXL ValueUtils int BlendMethod (verbatim cast)
}

}  // namespace sw
