// TiXL SnapToAnglesForce, ported from
// external/tixl Operators/Lib/Assets/shaders/particles/SnapOrientationForce.hlsl
// (.cs: external/tixl Operators/Lib/particle/force/SnapToAnglesForce.cs;
//  input->cbuffer wiring authority: SnapToAnglesForce.t3 FloatsToBuffer connection ORDER).
// A force compute pass that QUANTIZES each particle's velocity DIRECTION, projected onto a chosen
// plane, to the nearest of (360/AngleCount) discrete angles, lerp'd by Amount; a per-frame
// PhaseAngle (Twist) is added, KeepPlanar damps the off-plane axis, and Variation jitters the
// snap on a per-particle hash gate. Stateless: reads only Particle.Velocity + pure value params +
// per-particle hash41u; no extra buffer.
//
// === NAMED FORK — snapangles-camera ======================================================
// SnapOrientationForce.hlsl Mode 0 (CameraSpace, the .cs enum DEFAULT) transforms velocity by
// WorldToCamera, snaps, then transforms back by CameraToWorld (b2 Transforms cbuffer: 10 view/
// proj matrices). Our PointCookCtx has NO camera/view seam (same gap that BLOCKED batch23
// TransformFromClipSpace; orientpoints.metal bakes its camera modes the same way). We bake
// WorldToCamera = CameraToWorld = IDENTITY. With identity cameras the .hlsl reduces EXACTLY:
//   mul(float4(v,0), I).xyz == v ; mul(float4(newV,0), I).xyz == newV
// so CameraSpace becomes mathematically identical to WorldXY (planeCoords=v.xy, remaining=v.z).
// This is a faithful analytic reduction (not a no-op): the snap math runs in full. The three
// WorldSpace modes (XY/XZ/YZ) never touch the camera and are ported 1:1. Once a camera transform
// seam lands, restore the b2 matrices to make CameraSpace track the live camera.
// =========================================================================================
//
// HLSL -> MSL: GetDimensions() -> host P.Count. RWStructuredBuffer<Particle> u0 -> device
// Particle*. hash41u(uint) ported in shared/hash.metal.h. HLSL `% 1` (float mod) -> fmod(x,1).
// `(int)(t+0.5)` truncation toward zero -> (int)(t+0.5). lerp == mix. PI -> M_PI_F. The
// noise-functions.hlsl include is DROPPED — the .hlsl body makes no noise calls.
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B)
#include "particle_params.h"     // SnapAnglesForceParams, ForceBinding
#include "shared/hash.metal.h"   // hash41u, _PRIME0
using namespace metal;

// GetPlaneCoordinates — SnapOrientationForce.hlsl:39-61 (1:1). Camera (0) and WorldXY (1) share
// the xy/z plane (with identity camera they ARE the same, see NAMED FORK).
static void getPlaneCoordinates(float3 v, float spaceAndPlane, thread float2& planeCoords,
                                thread float& remainingAxis) {
  if (spaceAndPlane < 0.5f)      { planeCoords = v.xy; remainingAxis = v.z; }  // Camera (== WorldXY)
  else if (spaceAndPlane < 1.5f) { planeCoords = v.xy; remainingAxis = v.z; }  // World XY
  else if (spaceAndPlane < 2.5f) { planeCoords = v.xz; remainingAxis = v.y; }  // World XZ
  else                           { planeCoords = v.yz; remainingAxis = v.x; }  // World YZ
}

// SetPlaneCoordinates — SnapOrientationForce.hlsl:64-83 (1:1).
static float3 setPlaneCoordinates(float2 planeCoords, float remainingAxis, float spaceAndPlane,
                                  float3 originalV) {
  if (spaceAndPlane < 0.5f)      return float3(planeCoords, remainingAxis);             // Camera
  else if (spaceAndPlane < 1.5f) return float3(planeCoords, remainingAxis);             // World XY
  else if (spaceAndPlane < 2.5f) return float3(planeCoords.x, remainingAxis, planeCoords.y); // XZ
  else                           return float3(remainingAxis, planeCoords);             // World YZ
}

kernel void snaptoanglesforce(device Particle*              Particles [[buffer(FORCE_Particles)]],
                              constant SnapAnglesForceParams& P       [[buffer(FORCE_Params)]],
                              uint3                          tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;
  int id = (int)tid.x;

  float3 vInObject = float3(Particles[tid.x].Velocity);

  // hlsl:99-108 — CameraSpace transforms by WorldToCamera (baked IDENTITY -> v unchanged);
  // WorldSpace works directly in object space. With the bake, both branches yield v = vInObject.
  float3 v = vInObject;  // (Camera path: mul(float4(v,0), I).xyz == vInObject; see NAMED FORK)

  float2 planeCoords;
  float remainingAxis;
  getPlaneCoordinates(v, P.SpaceAndPlane, planeCoords, remainingAxis);

  float lengthXY = length(planeCoords);
  if (lengthXY < 0.00001f) return;

  float2 normalizedV = normalize(planeCoords);

  // hlsl:123 — atan2(x, y) (NOTE arg order: x first, y second — faithful).
  float a = atan2(normalizedV.x, normalizedV.y);

  // hlsl:125-126 — float `% 1` -> fmod(x, 1).
  float aNormalized = fmod((a + M_PI_F) / (M_PI_F * 2.0f), 1.0f);
  float subdivisions = 360.0f / P.SnapAngle;   // SnapAngle = AngleCount (.cs)

  // hlsl:128-131 — per-particle variation gate.
  float4 hash = hash41u((uint)(id + (int)P.RandomSeed * (int)_PRIME0));
  if (hash.x < P.VariationRatio) {             // VariationRatio = VariationThreshold (.cs)
    aNormalized += (hash.y - 0.5f) * P.Variation;
  }
  float t = aNormalized * subdivisions;

  // hlsl:133 — (int)(t+0.5) truncation, then /subdivisions.
  float tRounded = (float)((int)(t + 0.5f)) / subdivisions;

  float newAngle = mix(aNormalized, tRounded, P.Amount);

  // hlsl:137 — PhaseAngle = Twist (.cs), in degrees -> /360 here (matches .hlsl exactly).
  float alignedRotation = (newAngle - 0.5f) * 2.0f * M_PI_F + (P.PhaseAngle / 360.0f);

  float2 newPlaneCoords = float2(sin(alignedRotation), cos(alignedRotation)) * lengthXY;

  // hlsl:142 — KeepPlanar damps the off-plane axis.
  remainingAxis *= (1.0f - P.KeepPlanar);

  float3 newV = setPlaneCoordinates(newPlaneCoords, remainingAxis, P.SpaceAndPlane, v);

  // hlsl:147-156 — CameraSpace transforms back by CameraToWorld (baked IDENTITY -> newV
  // unchanged); WorldSpace uses newV directly. Final lerp factor is a literal 1 in the .hlsl.
  float3 newVelocity = newV;  // (Camera path: mul(float4(newV,0), I).xyz == newV; see NAMED FORK)

  Particles[tid.x].Velocity = mix(vInObject, newVelocity, 1.0f);
}
