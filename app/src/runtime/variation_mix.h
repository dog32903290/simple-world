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

}  // namespace sw
