// TiXL TurbulenceForce, ported 1:1 from
// external/tixl .../particles/TurbulanceForce.hlsl (the field/shader-graph path
// omitted -> no field bound means GetField()==1, fieldAmount==1). A force is a
// compute pass that ADDS curl-noise to each particle's velocity; the integrator
// runs after and moves position by the modified velocity.
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B packed)
#include "particle_params.h"     // TurbParams, ForceBinding
#include "shared/hash.metal.h"   // hash41u
#include "shared/noise.metal.h"  // curlNoise
using namespace metal;

kernel void turbulence_force(device Particle*    Particles [[buffer(FORCE_Particles)]],
                             constant TurbParams& P        [[buffer(FORCE_Params)]],
                             uint3                tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  uint vgc = (uint)(P.VariationGroupCount + 0.5f);
  uint mod = vgc == 0 ? maxParticleCount : vgc;
  float3 variationOffset = hash41u(tid.x % mod).xyz * P.Variation;

  float3 pos = float3(Particles[tid.x].Position) * 0.9f;  // avoid simplex glitch at -1,0,0
  float3 noiseLookup = (pos + variationOffset + P.Phase * float3(1, -1, 0)) * P.Frequency;
  float3 velocity = float3(Particles[tid.x].Velocity);

  float fieldAmount = 1.0f;  // no field bound
  float amount = P.Amount / 100.0f * fieldAmount * P.SpeedFactor;
  Particles[tid.x].Velocity = velocity + curlNoise(noiseLookup) * amount;
}
