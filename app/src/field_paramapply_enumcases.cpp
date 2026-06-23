// field_paramapply_enumcases — the DATA half of the enum-selector MSL-text asserts (see
// field_paramapply_enumcases.h). One row per wave-3 enum-selector field op; the golden TU assembles each
// through the REAL graph path and asserts the assembled MSL switched to the non-default selector's text.
// Peeled out of the golden to keep that TU ≤400 lines (ARCHITECTURE.md rule 4, NO grandfather bump).
//
// Anchor discipline (load-bearing): wantNonDefault / wantAbsent are chosen to appear ONLY in the per-node
// CALL line, never inside a helper body or comment. So each anchor pins the swizzle/fn/fold to a fixed
// neighbor (the helper-fn name + "(", or the "P.<Type>" param prefix) — e.g. `pMod1(p.x, P.RepeatAxis`
// avoids the CommonHgSdf comment `pMod1(p.x,5)` (which lacks the P.<Type> tail).
#include "field_paramapply_enumcases.h"

namespace sw {

std::vector<EnumCase> fieldParamApplyEnumCases() {
  return {
      // --- swizzle-selector SDF leaves + modifiers (Axis switches the emitted swizzle in the call) ---
      // BendField default Axis=0 -> "yzx"; non-default 2 -> "xyz". Anchor on opBend(p.<swiz>,.
      {"BendField", false, "Axis", 2.0f, "opBend(p.xyz,", "opBend(p.yzx,", "Axis swizzle yzx->xyz"},
      // TwistField default Axis=0 -> "yzx"; non-default 2 -> "xyz".
      {"TwistField", false, "Axis", 2.0f, "opTwist(p.xyz,", "opTwist(p.yzx,", "Axis swizzle yzx->xyz"},
      // CappedTorusSDF default Axis=2 -> "xyz"; non-default 0 -> "yzx".
      {"CappedTorusSDF", false, "Axis", 0.0f, "fCappedTorus(p.yzx", "fCappedTorus(p.xyz",
       "Axis swizzle xyz->yzx"},
      // CylinderSDF default Axis=1 -> "xyz"; non-default 0 -> "yxz".
      {"CylinderSDF", false, "Axis", 0.0f, "fRoundedCyl(p.yxz,", "fRoundedCyl(p.xyz,",
       "Axis swizzle xyz->yxz"},
      // PlaneSDF default Axis=1 -> "y" sign ""; non-default 3 (NegX) -> "x" sign "-". The call swizzle +
      // the param prefix pin it past the (helper-less) inline body.
      {"PlaneSDF", false, "Axis", 3.0f, "p.x - P.PlaneSDF", "p.y - P.PlaneSDF", "Axis swizzle/sign Y->NegX"},
      // PrismSDF default Axis=1 -> "xzy" (hex branch); non-default 0 -> "yzx".
      {"PrismSDF", false, "Axis", 0.0f, "fHexPrism(p.yzx", "fHexPrism(p.xzy", "Axis swizzle xzy->yzx"},
      // PyramidSDF default Axis=1 -> "xyz"; non-default 0 -> "yxz".
      {"PyramidSDF", false, "Axis", 0.0f, "fPyramid(p.yxz,", "fPyramid(p.xyz,", "Axis swizzle xyz->yxz"},
      // RepeatAxis default Axis=0 -> "x" (pMod1, mirror off); non-default 2 -> "z". P.<Type> tail avoids
      // the CommonHgSdf comment `pMod1(p.x,5)`.
      {"RepeatAxis", false, "Axis", 2.0f, "pMod1(p.z, P.RepeatAxis", "pMod1(p.x, P.RepeatAxis",
       "Axis swizzle x->z"},
      // RepeatFieldLimit default Axis=0 -> "x"; non-default 2 -> "z".
      {"RepeatFieldLimit", false, "Axis", 2.0f, "pModLimited2(p.z, P.RepeatFieldLimit",
       "pModLimited2(p.x, P.RepeatFieldLimit", "Axis swizzle x->z"},
      // RepeatPolar default Axis=0 -> "zy" (pModPolar, mirror off); non-default 2 -> "yx".
      {"RepeatPolar", false, "Axis", 2.0f, "pModPolar(p.yx, P.RepeatPolar",
       "pModPolar(p.zy, P.RepeatPolar", "Axis swizzle zy->yx"},
      // RotateAxis default Axis=0 -> "zy"; non-default 2 -> "yx".
      {"RotateAxis", false, "Axis", 2.0f, "pRotateAxis(p.yx,", "pRotateAxis(p.zy,", "Axis swizzle zy->yx"},
      // FractalSDF Iterations = the compile-time loop-bound selector wave 2 deferred (NOT packed; baked as
      // a literal into the fMandelBulbFractal for-loop bound). Default 8 -> `< 8; i++)`; non-default 5 ->
      // `< 5; i++)`. The selector re-emits the helper body text (a different srcHash), never the buffer.
      {"FractalSDF", false, "Iterations", 5.0f, "< 5; i++)", "< 8; i++)", "Iterations loop bound 8->5"},

      // --- fold-selector combiners (CombineMethod switches the post-line fold; needs 2 wired children) ---
      // CombineFieldColor default CombineMethod=0 (Mix) -> mix(...); non-default 2 (Multiply) -> "f.. * f..".
      // ND anchor: a multiply fold token; ABS: the Mix `mix(` (gone after the switch).
      {"CombineFieldColor", true, "CombineMethod", 2.0f, " * f", "mix(f", "fold Mix->Multiply"},
      // StairCombineSDF default CombineMethod=3 (UnionStairs) -> fOpUnionStairs(...); non-default 0
      // (UnionColumns) -> fOpUnionColumns(...). Anchor on the called fold fn name in the post line.
      {"StairCombineSDF", true, "CombineMethod", 0.0f, "fOpUnionColumns(f", "fOpUnionStairs(f",
       "fold UnionStairs->UnionColumns"},
  };
}

}  // namespace sw
