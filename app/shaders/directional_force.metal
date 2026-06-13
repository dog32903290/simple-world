// TiXL DirectionalForce, ported 1:1 from
// external/tixl Operators/Lib/Assets/shaders/particles/DirectionalForce.hlsl.
// A force is a compute pass that ADDS a constant directional push (with optional
// per-particle random scaling) to each particle's velocity; the ParticleSystem
// integrator runs after and moves position by the modified velocity.
//
// HLSL -> MSL: GetDimensions() has no MSL equivalent -> the host supplies Count in the
// cbuffer (DirForceParams). float3 Direction is carried as 3 scalars (no packed_float3 in a
// cbuffer = zero alignment traps) and reassembled here. RWStructuredBuffer<Particle> u0 ->
// device Particle*; hash11(i.x) (HLSL implicit uint->float) -> hash11(float(tid.x)).
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B packed)
#include "particle_params.h"     // DirForceParams, ForceBinding
using namespace metal;

// hash11 — PCG-style float->float scalar hash, 1:1 from TiXL hash-functions.hlsl:10. Inlined
// here (not in shared/hash.metal.h) because that header already collides with the local
// hash11u copies in filterpoints/randomizepoints; keeping this leaf self-contained avoids
// disturbing those other-lane shaders. HLSL frac() == MSL fract().
inline float dirHash11(float p) {
    p = fract(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return fract(p);
}

kernel void directional_force(device Particle*       Particles [[buffer(FORCE_Particles)]],
                              constant DirForceParams& P       [[buffer(FORCE_Params)]],
                              uint3                   tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // DirectionalForce.hlsl:23-24 (1:1):
  //   float3 offset = Direction * Amount * (1 + hash11(i.x) * RandomAmount) * SpeedFactor;
  //   Particles[i.x].Velocity += offset;
  float3 direction = float3(P.DirX, P.DirY, P.DirZ);
  float3 offset = direction * P.Amount * (1.0f + dirHash11((float)tid.x) * P.RandomAmount) * P.SpeedFactor;
  Particles[tid.x].Velocity = float3(Particles[tid.x].Velocity) + offset;
}
