// blendpoints.metal — faithful Metal port of TiXL BlendPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/combine/BlendPoints.hlsl
//
// Per-thread (one output point per PointsA element): index-paired blend of PointsA[i] toward
// PointsB[i] by a factor f selected by BlendMode, with optional Scatter jitter, then lerp every
// SwPoint channel (Rotation via qSlerp).  resultCount = countA.
//
// TiXL kernel logic (verbatim, BlendPoints.hlsl lines 24-106):
//   t = i.x / (resultCount-1)
//   PairingMode>0.5 && countA!=countB -> Adjust thinning early-return
//   f from BlendMode (Mix / UseA_F1 / UseB_F1 / Ranged / RangedSmooth)
//   f += (hash11(t)-0.5) * Scatter * smoothstep(0,1, 1-abs(f-0.5)*2)
//   noBlend = isnan(A.Scale.x * B.Scale.x);  f = noBlend ? (f<0.5?0:1) : f
//   Scale     = noBlend ? (f<0.1 ? A.Scale : B.Scale) : lerp(A.Scale,B.Scale,f)
//   Rotation  = qSlerp(A.Rotation, B.Rotation, f)
//   FX1/FX2/Color/Position = lerp(A.*,B.*,f);  THEN FX1 = f  (final overwrite, faithful)
//
// ALIGNED TO TiXL (no longer a fork): TiXL reads PointsB[i.x] with no bounds check (HLSL OOB
//   StructuredBuffer read -> all-zero element, D3D SRV contract). We zero-fill B the same way
//   (bIndex<countB ? PointsB[bIndex] : SwPoint{}) instead of clamping to the last element -- an
//   earlier revision clamped, which diverged from TiXL whenever countB<countA; see
//   selftests_mathv_blendpoints_probes.cpp's checkPairingCountsTooth for the confirming assertion
//   (GPU now agrees with the CPU ref on every b-OOB row, no divergence left to record).
//
// NAMED FORKS:
//   fork[t-singular]: t = i.x/(resultCount-1) divides by zero when resultCount==1; we keep the
//     exact float division (no special-case) to preserve byte-parity with the HLSL.
//   fork[guard-off-by-one]: TiXL's dispatch guard is `if (i.x > resultCount) return;` (STRICT
//     '>') -- itself a genuine off-by-one in the source: valid ResultPoints indices are only
//     [0, resultCount-1], so a tail thread with i.x==resultCount slips PAST this guard and issues
//     a real write one slot past the array -- silently discarded under D3D's RWStructuredBuffer
//     OOB-write contract (same "OOB is a no-op" family the b-count-guard alignment above relies
//     on). We guard `i >= resultCount` below instead: stricter than the HLSL text, but produces
//     the SAME observable result for every real dispatch (the extra HLSL write, if it ran, would
//     be discarded anyway) -- a defensive rewrite of an unreachable-in-practice source bug, not a
//     math divergence. Probed directly by selftests_mathv_blendpoints_probes.cpp's
//     checkOffByOneTooth (over-dispatch to idx==resultCount).
//
// SwPoint layout (tixl_point.h, 64B): Position@0 packed3 | FX1@12 | Rotation@16 f4 |
//   Color@32 f4 | Scale@48 packed3 | FX2@60.  TiXL Point.W == SwPoint.FX1.
#include <metal_stdlib>
#include "tixl_point.h"           // SwPoint (64B), packed_float3
#include "blendpoints_params.h"   // BlendPointsParams, BlendPointsBinding
#include "shared/quat.metal.h"    // qSlerp (faithful TiXL qSlerp port; used by orientpoints)
#include "shared/hash.metal.h"    // hash11 (verbatim TiXL Dave-Hoskins port)
using namespace metal;

// TiXL SmootherStep (BlendPoints.hlsl lines 18-22), verbatim.
inline float bp_smootherStep(float x) {
  x = saturate(x);
  return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

kernel void blendpoints(
    device const SwPoint*        PointsA [[buffer(BLENDPOINTS_PointsA)]],
    device const SwPoint*        PointsB [[buffer(BLENDPOINTS_PointsB)]],
    device       SwPoint*        Result  [[buffer(BLENDPOINTS_Result)]],
    constant BlendPointsParams&  P       [[buffer(BLENDPOINTS_Params)]],
    uint3 tid [[thread_position_in_grid]])
{
  uint i = tid.x;
  uint resultCount = P.CountA;
  uint countA = P.CountA;
  uint countB = P.CountB;

  // fork[guard-off-by-one]: TiXL: if (i.x > resultCount) return; (strict '>', a genuine upstream
  // off-by-one -- see NAMED FORKS above). We guard i>=resultCount to keep Metal in-bounds.
  if (i >= resultCount) return;

  uint aIndex = i;
  uint bIndex = i;

  // fork[t-singular]: faithful float divide (resultCount==1 -> div by 0, as in HLSL).
  float t = (float)i / ((float)resultCount - 1.0f);

  // Adjust pairing: count-mismatch thinning early-return (BlendPoints.hlsl lines 39-52).
  if (P.PairingMode > 0.5f && countA != countB) {
    uint firstIxA = (uint)(((float)aIndex * (float)resultCount) / (float)countA);
    uint firstIxB = (uint)(((float)bIndex * (float)resultCount) / (float)countB);
    uint firstIx  = max(firstIxA, firstIxB);
    if (i > firstIx) return;
  }

  SwPoint A = PointsA[aIndex];

  // b-count-guard, aligned to TiXL: HLSL's PointsB[bIndex] is an unclamped StructuredBuffer read;
  // D3D's SRV OOB-read contract returns an all-zero element (never garbage, never a clamp to the
  // last valid element). Zero-fill here reproduces that exactly -- this is no longer a NAMED FORK
  // (an earlier revision clamped to PointsB[countB-1], which diverged from TiXL whenever
  // countB < countA; see blendpoints_params.h's b-count-guard note for the alignment record).
  SwPoint B = (bIndex < countB) ? PointsB[bIndex] : SwPoint{};

  float f = 0.0f;
  if (P.BlendMode < 0.5f) {
    f = P.BlendFactor;
  } else if (P.BlendMode < 1.5f) {
    f = A.FX1;
  } else if (P.BlendMode < 2.5f) {
    f = (1.0f - B.FX1);
  } else if (P.BlendMode < 3.5f) {
    // Ranged
    f = 1.0f - saturate((t - P.BlendFactor) / P.Width - P.BlendFactor + 1.0f);
  } else {
    // RangedSmooth
    float b = fmod(P.BlendFactor, 2.0f);
    if (b > 1.0f) {
      b = 2.0f - b;
      t = 1.0f - t;
    }
    f = 1.0f - bp_smootherStep(saturate((t - b) / P.Width - b + 1.0f));
  }

  float fallOffFromCenter = smoothstep(0.0f, 1.0f, 1.0f - fabs(f - 0.5f) * 2.0f);
  f += (hash11(t) - 0.5f) * P.Scatter * fallOffFromCenter;

  float3 aScale = float3(A.Scale.x, A.Scale.y, A.Scale.z);
  float3 bScale = float3(B.Scale.x, B.Scale.y, B.Scale.z);
  bool noBlend = isnan(aScale.x * bScale.x);

  f = noBlend ? (f < 0.5f ? 0.0f : 1.0f) : f;

  SwPoint out;
  float3 outScale = noBlend ? (f < 0.1f ? aScale : bScale) : mix(aScale, bScale, f);
  out.Scale    = packed_float3(outScale.x, outScale.y, outScale.z);
  out.Rotation = qSlerp(A.Rotation, B.Rotation, f);
  out.FX1      = mix(A.FX1, B.FX1, f);
  out.FX2      = mix(A.FX2, B.FX2, f);
  out.Color    = mix(A.Color, B.Color, f);
  float3 aPos  = float3(A.Position.x, A.Position.y, A.Position.z);
  float3 bPos  = float3(B.Position.x, B.Position.y, B.Position.z);
  float3 outPos = mix(aPos, bPos, f);
  out.Position = packed_float3(outPos.x, outPos.y, outPos.z);
  out.FX1      = f;  // TiXL final overwrite: ResultPoints[i].FX1 = f (line 105)

  Result[i] = out;
}
