// TiXL AxisStepForce, ported 1:1 from
// external/tixl Operators/Lib/Assets/shaders/particles/AxisStepForce.hlsl
// (.cs: external/tixl Operators/Lib/particle/force/AxisStepForce.cs).
// A force compute pass that, per particle, hashes (index + Seed*k) to pick a random DOMINANT axis
// (x/y/z) weighted by AxisDistribution, a signed strength, and a SelectRatio gate; the velocity is
// lerp'd toward (origVelocity*AddOriginalVelocity + direction*f) by (ApplyTrigger*selected).
// Stateless: reads Particle.Velocity (+ Particle.Rotation in RotationSpace mode) + pure value
// params + per-particle hash; no field, no extra buffer.
//
// HLSL -> MSL: GetDimensions() -> host P.Count. RWStructuredBuffer<Particle> u0 -> device
// Particle*. hash41u(uint) ported in shared/hash.metal.h (1:1). Vec3 inputs carried as 3 scalars
// each. AxisSpace/Seed are float in the cbuffer (the .hlsl reads them as float and casts
// (uint)Seed / compares AxisSpace<0.5,<1.5). HLSL lerp == MSL mix.
//
// NOTE (faithful TiXL quirk): AxisStepForce.hlsl applies `f` TWICE — once at
//   direction *= sign * StrengthDistribution * f;   (line 42)
// and again inside the lerp:
//   ... origVelocity*AddOriginalVelocity + direction * f, ApplyTrigger*selected);  (line 56)
// This is exactly what the .hlsl does; ported verbatim (f² scaling on the directional term).
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B)
#include "particle_params.h"     // AxisStepForceParams, ForceBinding
#include "shared/hash.metal.h"   // hash41u, _PRIME0
#include "shared/quat.metal.h"   // qRotateVec3 (RotationSpace mode)
using namespace metal;

kernel void axis_step_force(device Particle*             Particles [[buffer(FORCE_Particles)]],
                            constant AxisStepForceParams& P        [[buffer(FORCE_Params)]],
                            uint3                         tid       [[thread_position_in_grid]]) {
  uint gi = tid.x;
  uint maxParticleCount = P.Count;
  if (gi >= maxParticleCount) return;

  // hlsl:34-35 — hash41u(gi + (uint)Seed*1103515245U) and (...+83339).
  float4 randForPos     = hash41u(gi + (uint)P.Seed * 1103515245U);
  float4 randForEffects = hash41u(gi + (uint)P.Seed * 1103515245U + 83339U);

  // hlsl:37-38
  float selected = randForPos.w < P.SelectRatio ? 1.0f : 0.0f;
  float f = selected * P.Strength * (1.0f + P.RandomizeStrength * (randForEffects.r - 0.5f));

  // hlsl:40-43 — pick dominant axis weighted by AxisDistribution.
  float3 axisDistribution = float3(P.AxisDistributionX, P.AxisDistributionY, P.AxisDistributionZ);
  float3 axis = abs(randForPos.zyx * axisDistribution);
  float3 direction = axis.x > axis.y ? (axis.x > axis.z ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f))
                                     : (axis.y > axis.z ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f));

  float3 strengthDistribution = float3(P.StrengthDistributionX, P.StrengthDistributionY, P.StrengthDistributionZ);
  // hlsl:45 — direction *= (sign) * StrengthDistribution * f;  (first f application)
  direction *= (randForEffects.g < 0.5f ? 1.0f : -1.0f) * strengthDistribution * f;

  // hlsl:47-53 — AxisSpace: 0=ObjectSpace (no-op), 1=RotationSpace (rotate by particle's Rotation).
  if (P.AxisSpace < 0.5f) {
    // ObjectSpace — direction stays as-is.
  } else if (P.AxisSpace < 1.5f) {
    direction = qRotateVec3(direction, float4(Particles[gi].Rotation));
  }

  // hlsl:55-57 — Velocity = lerp(origVelocity, origVelocity*AddOriginalVelocity + direction*f,
  //                              ApplyTrigger*selected);   (second f application — faithful quirk)
  float3 origVelocity = float3(Particles[gi].Velocity);
  float3 mixed = origVelocity * P.AddOriginalVelocity + direction * f;
  Particles[gi].Velocity = mix(origVelocity, mixed, P.ApplyTrigger * selected);
}
