// spatialaudio_golden — --selftest-spatialaudio. CLOSED-FORM golden for the SpatialAudioPlayer gain spine
// (TiXL Operators/Lib/io/audio/SpatialAudioPlayer.cs + Core/Audio/SpatialOperatorAudioStream.cs). Proves
// the value-exact parts sw can pin without BASS's 3D sound field (the per-ear pan/HRTF is deferred-hw-verify):
//   (1) linear distance attenuation with both clamp layers,
//   (2) effective volume = mute/beyond-max gate × attenuation,
//   (3) listener/source Euler→forward/up direction vectors.
//
// EXPECTED VALUES (independent-of-impl, hand-derived from the .cs — GOLDEN_STANDARD 三特徵 #1):
//   Attenuation (SpatialOperatorAudioStream.cs:257-269): min=2, max=10.
//     dist=2   → 1.0 (at min)                         dist=10 → 0.0 (at max)
//     dist=6   → 1-(6-2)/(10-2)  = 0.5  (midpoint)    dist=4  → 1-(4-2)/8 = 0.75 (OFF-CENTER — catches the
//                                                     1-t↔t swap bug, which is symmetric at the midpoint)
//   Operator clamp (cs:193-195): min<=0 → 1; max<=min → min+10. Probe: min=0, max=3 → clamped min=1,max=3;
//     dist=2 → 1-(2-1)/(3-1) = 0.5.  (proves the operator-layer clamp, not just the stream layer.)
//   Effective volume (cs:451-459): vol=0.8, atten=0.5 → 0.4; mute → 0; beyond-max (atten=0) → 0.
//   Euler→forward (cs:189, Transform((0,0,1),R)): yaw=90° about Y → forward ≈ (+1,0,0) (right-handed,
//     row-vector .NET Transform). yaw=0 → forward=(0,0,1). up at yaw=0 → (0,1,0).
//
// DIVERGING-MIDDLE (三特徵 #2): dist=4 (t=0.25 → 0.75) and dist=6 (t=0.5 → 0.5) leave the endpoints; the
// off-center probe pins the falloff SHAPE, not just the symmetric midpoint. The Euler probe uses a 90°
// non-identity rotation (not the yaw=0 no-op).
//
// TEETH (三特徵 #3): setSpatialAudioBug(true) drops the `1.0 -` in the REAL attenuation (returns t) so the
// off-center probe (0.75 → 0.25) goes RED on the actual math, NOT by flipping the expected value.
// did-not-trip → return 0 (--bite NO-BITE list catches a dead tooth).
#include <cmath>
#include <cstdio>

#include "runtime/spatial_audio.h"

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

bool near3(simd::float3 v, float x, float y, float z) {
  return nearf(v.x, x) && nearf(v.y, y) && nearf(v.z, z);
}

}  // namespace

int runSpatialAudioSelfTest(bool injectBug) {
  setSpatialAudioBug(injectBug);
  bool ok = true;

  // ---- distance attenuation (min=2, max=10) --------------------------------------------------------
  float aMin  = spatialDistanceAttenuation(2.0f, 2.0f, 10.0f);   // at min → 1.0
  float aMax  = spatialDistanceAttenuation(10.0f, 2.0f, 10.0f);  // at max → 0.0
  float aMid  = spatialDistanceAttenuation(6.0f, 2.0f, 10.0f);   // midpoint → 0.5
  float aOff  = spatialDistanceAttenuation(4.0f, 2.0f, 10.0f);   // OFF-CENTER t=0.25 → 0.75
  float aNear = spatialDistanceAttenuation(1.0f, 2.0f, 10.0f);   // inside min → 1.0
  float aFar  = spatialDistanceAttenuation(20.0f, 2.0f, 10.0f);  // beyond max → 0.0
  // operator clamp probe: min=0 → clamped 1, max=3; dist=2 → 1-(2-1)/(3-1)=0.5
  float aOpClamp = spatialDistanceAttenuation(2.0f, 0.0f, 3.0f);

  if (injectBug) {
    // TEETH: bug returns t instead of 1-t. The OFF-CENTER probe (want 0.75) becomes 0.25 → tripped.
    if (nearf(aOff, 0.75f)) {  // did-not-trip: bug did not corrupt the off-center attenuation
      std::printf("[selftest-spatialaudio] BUG did-not-trip (off-center still %.3f)\n", aOff);
      setSpatialAudioBug(false);
      return 0;  // dead tooth → NO-BITE list, not a false exit 1
    }
    setSpatialAudioBug(false);
    std::printf("[selftest-spatialaudio] BUG bit (off-center %.3f != 0.75) — RED as required\n", aOff);
    return 1;
  }

  ok = ok && nearf(aMin, 1.0f) && nearf(aMax, 0.0f) && nearf(aMid, 0.5f) && nearf(aOff, 0.75f)
          && nearf(aNear, 1.0f) && nearf(aFar, 0.0f) && nearf(aOpClamp, 0.5f);

  // ---- effective volume ---------------------------------------------------------------------------
  float vNorm = spatialEffectiveVolume(0.8f, 0.5f, /*mute=*/false);  // 0.8*0.5 = 0.4
  float vMute = spatialEffectiveVolume(0.8f, 0.5f, /*mute=*/true);   // muted → 0
  float vFar  = spatialEffectiveVolume(0.8f, 0.0f, /*mute=*/false);  // beyond max (atten 0) → 0
  ok = ok && nearf(vNorm, 0.4f) && nearf(vMute, 0.0f) && nearf(vFar, 0.0f);

  // ---- Euler → direction vectors ------------------------------------------------------------------
  simd::float3 fwd0  = spatialForwardFromEuler(simd::make_float3(0, 0, 0));   // (0,0,1)
  simd::float3 up0   = spatialUpFromEuler(simd::make_float3(0, 0, 0));         // (0,1,0)
  simd::float3 fwd90 = spatialForwardFromEuler(simd::make_float3(0, 90, 0));  // yaw 90° about Y → (+1,0,0)
  ok = ok && near3(fwd0, 0, 0, 1) && near3(up0, 0, 1, 0) && near3(fwd90, 1, 0, 0);

  std::printf("[selftest-spatialaudio] atten(mid=%.3f off=%.3f opclamp=%.3f) vol(%.3f) fwd90=(%.2f,%.2f,%.2f) %s\n",
              aMid, aOff, aOpClamp, vNorm, fwd90.x, fwd90.y, fwd90.z, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
