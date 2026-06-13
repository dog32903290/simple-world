// TiXL VectorFieldForce, ported from
// external/tixl Operators/Lib/Assets/shaders/particles/VectorFieldForce-sg.hlsl.
// A force compute pass that samples a VECTOR FIELD at each particle's position and pushes the
// particle along the sampled vector. The integrator runs after and moves position by the
// modified velocity.
//
// === fork-VFF (named) ====================================================================
// VectorFieldForce's input is a ShaderGraphNode (a procedural field) — the field-graph
// subsystem TiXL builds at runtime. We have no field-graph (same gap TurbulenceForce's
// ValueField was omitted for: turbulence_force.metal "no field bound -> fieldAmount==1").
// With no field the HLSL's GetField() returns f = float4(1,1,1,1), so:
//   velocity = f.xyz * Amount * f.w * variationFactor  ==  (1,1,1) * Amount * 1 * variation
// i.e. a constant diagonal (1,1,1) push, jittered per-particle by Variation. This op becomes
// distinct from DirectionalForce ONLY once a field graph lands (then GetField samples it);
// until then it is a fixed-direction push along (1,1,1). The rest of the HLSL math
// (Variation jitter, the NaN guards on BirthTime + velocity) is ported 1:1.
// =========================================================================================
//
// HLSL -> MSL: GetDimensions() -> host-supplied P.Count. RWStructuredBuffer<Particle> u0 ->
// device Particle*; the t0/t1 Vertices/Indices + b2 ParticleCount/EnableBounce are field/mesh
// resources, all dropped with the field path (fork-VFF). hash11u(i.x) ported in hash.metal.h.
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B packed)
#include "particle_params.h"     // VecFieldForceParams, ForceBinding
using namespace metal;

// hash11u — uint->float single-channel LCG cascade, 1:1 from TiXL hash-functions.hlsl:115
// (TWO rounds; same GLIBC k + 13331 prime spine as hash41u). Inlined here rather than added to
// shared/hash.metal.h: that header would then double-define against the EXISTING local hash11u
// copies in filterpoints/randomizepoints (different, single-round variants — unifying them would
// silently change those other-lane ops' output). Self-contained leaf = no cross-lane disturbance.
inline float vffHash11u(uint x) {
    const uint k = 1103515245U; // GLIB C
    x *= 13331U;                // _PRIME0 (== shared/hash.metal.h)
    x = ((x >> 8U) ^ x) * k;
    x = ((x >> 8U) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffU));
}

kernel void vector_field_force(device Particle*             Particles [[buffer(FORCE_Particles)]],
                               constant VecFieldForceParams& P        [[buffer(FORCE_Params)]],
                               uint3                         tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // VectorFieldForce-sg.hlsl:55-56 guard: skip un-emitted slots (BirthTime == NaN). Our cycle
  // buffer seeds the whole pool, but the guard is faithful + harmless (matches the integrator's
  // own dead-slot handling).
  if (isnan(Particles[tid.x].BirthTime)) return;

  // No field bound (fork-VFF): GetField() -> f = float4(1,1,1,1).
  float4 f = float4(1.0f, 1.0f, 1.0f, 1.0f);

  // hlsl:61 — float3 variationFactor = hash11u(i.x) * Variation + 1;
  float3 variationFactor = vffHash11u(tid.x) * P.Variation + 1.0f;

  // hlsl:63 — float3 velocity = f.xyz * Amount * f.w * variationFactor;
  // SpeedFactor is the system's global speed multiplier (== TurbulenceForce's, set 1.0 by the
  // cook); not in the HLSL's b0 but applied here to match the force-pass convention.
  float3 velocity = f.xyz * P.Amount * f.w * variationFactor * P.SpeedFactor;

  // hlsl:65-66 — NaN guard before accumulate.
  if (!isnan(velocity.x) && !isnan(velocity.y) && !isnan(velocity.z))
    Particles[tid.x].Velocity = float3(Particles[tid.x].Velocity) + velocity;
}
