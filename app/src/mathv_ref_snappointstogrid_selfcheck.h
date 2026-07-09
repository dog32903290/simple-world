#pragma once
// mathv_ref_snappointstogrid_selfcheck.h — hand-derived sanity self-check, TRANSCRIBED from
// external/tixl (SHA 395c4c55) Operators/Lib/Assets/shaders/points/_internal/SnapPointsToGrid.hlsl
// by hand (same source as mathv_ref_snappointstogrid.h, whose provenance header this split
// inherits — see that file for the full HLSL line-by-line citation). Split out of
// mathv_ref_snappointstogrid.h (§4.3 rule 4 / ARCHITECTURE.md rule 4: the main ref header crossed
// 418 lines after the XS verdict A zero-guard pin + small-D hlslSaturate(NaN)->0 fix, over the 400
// hard cap for a file not already on the linecount-grandfather ratchet). Mechanical split, zero
// behavior change.
//
// NOT a standalone header — included from EXACTLY ONE site, the bottom of
// mathv_ref_snappointstogrid.h, still inside `namespace sw { namespace mathv_ref {`. Relies on that
// site already having SwPoint / SnapPointsToGridParams / snapPointsToGridOne / <cmath> visible; do
// not #include this file directly from anywhere else.
//
// mathvRefSnapPointsToGridSelfCheck() is NEVER CALLED anywhere in the tree (same precedent as its
// siblings mathvRefWrapPointPositionSelfCheck / mathvRefAddNoiseSelfCheck) — it exists purely as a
// hand-derive-then-assert artifact for a human/reviewer to read (re-derive by hand from the HLSL
// text, not by reading this file's own logic back), not a wired-up test. It is still compiled and
// type-checked every time mathv_ref_snappointstogrid.h is included, since it is a plain
// (non-template) inline function body — this split does not change that.

// --- self-check: hand-derived sanity values (independent of the code above; re-derive by hand
// from the HLSL text, not by reading this file's own logic back) ------------------------------
//
// 1) Mode=0 (CenterDistance), GridScale=1/GridStretch=(1,1,1)->gridSize=(1,1,1), GridOffset=0,
//    Scatter=0, GainAndBias=(0.5,0.5) [g=b=0.5 -- GetBias/GetSchlickBias both reduce to IDENTITY at
//    0.5: GetBias(0.5,y)=y/((2-2)*(1-y)+1)=y/1=y], Amount=1, StrengthFactor=0, Position=(1.3,0,0):
//    normOffsetPos=(1.8,0.5,0.5); flooredMod1(1.8)=0.8, flooredMod1(0.5)=0.5;
//    signedFraction=((0.8-0.5)*2,0,0)=(0.6,0,0). centerPoint=(1.3-0.6*0.5,0,0)=(1.0,0,0).
//    snapAmount(mode0): len(sf*gridSize)=0.6; len(gridSize)=sqrt(3)=1.732051;
//      v=saturate(0.6/1.732051)=0.346410; snapAmount=(v,v,v)=biasedSnap (identity bias).
//    strength=1*1=1. ff=(1-saturate(biasedSnap-2+1))*1=(1-saturate(-0.65359))*1=(1-0)*1=1 (all
//      lanes, since Amount=1 always fully engages regardless of biasedSnap).
//    Position_out = lerp(org,center,1) = center = (1.0,0,0).
//
// 2) Mode=2 (AxisCenterDistance), same grid as (1), Scatter=0, GainAndBias=(0.5,0.5) (identity),
//    Amount=0.3, StrengthFactor=0, Position=(1.3,1.7,0.2):
//    normOffsetPos=(1.8,2.2,0.7); flooredMod1: 1.8->0.8, 2.2->0.2, 0.7->0.7.
//    signedFraction=(0.6,-0.6,0.4). centerPoint=(1.3-0.3,1.7+0.3,0.2-0.2)=(1.0,2.0,0.0).
//    snapAmount(mode2,per-axis abs,scatter=0)=(0.6,0.6,0.4)=biasedSnap (identity bias).
//    strength=0.3*1=0.3. ff_n=(1-saturate(biased_n-0.6+1))*0.3:
//      ff_x=(1-saturate(1.0))*0.3=0; ff_y=0 (same magnitude); ff_z=(1-saturate(0.8))*0.3=0.06.
//    Position_out=(1.3+0,1.7+0,0.2+0.06*(-0.2))=(1.3,1.7,0.188) -- x/y UNCHANGED, z partially
//      snapped: demonstrates Mode 2's per-axis independence.
//
// 3) ApplyGainAndBias `return v4` BUG made concrete: Mode=1 (CornersDistance), same grid as (1),
//    Position=(0,0,0), Scatter=0, GainAndBias=(0,0) [degenerate-but-in-range post-saturate], Amount=1,
//    StrengthFactor=0: normOffsetPos=(0.5,0.5,0.5); flooredMod1(0.5)=0.5; signedFraction=(0,0,0)
//    exactly -> centerPoint == orgPosition == (0,0,0). snapAmount(mode1): len(0)/len(gridSize)=0;
//    v=1-saturate(0)=1; snapAmount=(1,1,1).
//    applyGainAndBiasVec4(1,1,1,gain=0,bias=0): g=0<0.5 -> vx=getBias(b=0,x=1)
//      = 1/((1/0-2)*(1-1)+1) = 1/((Inf-2)*0+1) = 1/(NaN+1) = NaN  [Inf*0=NaN per IEEE754].
//      vx=getSchlickBiasVec4(NaN,gain=0): NaN<0.5 is FALSE -> else arm getBias(1,NaN)=NaN/(NaN+1)
//      =NaN; /2+0.5=NaN. biasedSnap=(NaN,NaN,NaN) all lanes.
//    strength=1. ff=(1-hlslSaturate(NaN-2+1))*1=(1-hlslSaturate(NaN))*1. UPDATED (XS verdict
//      small-D, 2026-07-10): hlslSaturate(NaN) is now 0 (measured real-hardware saturate(NaN),
//      see detail::hlslSaturate's NAMED FORK note) instead of the old NaN-propagating behavior, so
//      ff=(1-0)*1=1 -- Position_out=lerp(org,center,1)=center=org=(0,0,0) (center==org exactly in
//      this case). The `return v4;` bug still fires (biasedSnap itself is still NaN, per the trace
//      above -- this fix does NOT clamp biasedSnap), but the outer hlslSaturate(NaN)=0 now happens
//      to coincide with what the "intended" (unbugged) clamp would have produced here: hiMask fires
//      >=0.999 -> biasedSnap=1 -> ff=(1-saturate(1-2+1))*1=(1-saturate(0))*1=1 -- SAME ff, same
//      Position_out. Whether this coincidence holds for every (Amount, gain, bias) combination or
//      only this Amount=1 slice is not proven by this hand-derivation alone; see
//      checkApplyGainAndBiasQuirk() (selftests_mathv_snappointstogrid.cpp) for the measured
//      GPU-vs-ref comparison across several (gain,bias,Amount) variants.
inline bool mathvRefSnapPointsToGridSelfCheck() {
  auto approxEq = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
  bool ok = true;

  {  // case 1
    SwPoint in{}, out{};
    in.Position = {1.3f, 0.0f, 0.0f};
    SnapPointsToGridParams p{1, 1, 1, 1.0f, 0, 0, 0, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0};
    snapPointsToGridOne(in, out, 0, p);
    ok &= approxEq(out.Position.x, 1.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f);
  }
  {  // case 2
    SwPoint in{}, out{};
    in.Position = {1.3f, 1.7f, 0.2f};
    SnapPointsToGridParams p{1, 1, 1, 0.3f, 0, 0, 0, 1.0f, 0.0f, 2.0f, 0.5f, 0.5f, 0};
    snapPointsToGridOne(in, out, 0, p);
    ok &= approxEq(out.Position.x, 1.3f) && approxEq(out.Position.y, 1.7f) &&
          approxEq(out.Position.z, 0.188f);
  }
  {  // case 3
    SwPoint in{}, out{};
    in.Position = {0.0f, 0.0f, 0.0f};
    SnapPointsToGridParams p{1, 1, 1, 1.0f, 0, 0, 0, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0};
    snapPointsToGridOne(in, out, 0, p);
    // UPDATED (XS verdict small-D, 2026-07-10): was isnan() before the hlslSaturate(NaN)->0 fix
    // above; now finite (0,0,0) -- see the trace update at the case-3 comment block.
    ok &= approxEq(out.Position.x, 0.0f) && approxEq(out.Position.y, 0.0f) &&
          approxEq(out.Position.z, 0.0f);
  }

  return ok;
}
