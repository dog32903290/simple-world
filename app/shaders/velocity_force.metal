// TiXL VelocityForce, ported 1:1 from
// external/tixl Operators/Lib/Assets/shaders/particles/VelocityForce.hlsl
// (.cs: external/tixl Operators/Lib/particle/force/VelocityForce.cs).
// A force compute pass that reads each particle's CURRENT velocity, keeps its DIRECTION, and
// rescales its SPEED (magnitude): speed += Accelerate*0.02*strength, then clamp[MinSpeed,MaxSpeed].
// strength = Amount * variationFactor, where variationFactor jitters per-particle via a hash
// run through ApplyGainAndBias(VariationGainAndBias) and Variation. The ParticleSystem integrator
// runs after and moves position by the rescaled velocity. Stateless: reads only Particle.Velocity
// + pure value params; no field, no extra buffer.
//
// HLSL -> MSL: GetDimensions() -> host-supplied P.Count. RWStructuredBuffer<Particle> u0 ->
// device Particle*. float2 VariationGainAndBias carried as 2 scalars (GainBiasX/Y). HLSL frac()
// == MSL fract(); saturate/normalize/length/clamp identical. hash11u(i.x) ported below (1:1 from
// hash-functions.hlsl:115). ApplyGainAndBias ported VERBATIM from bias-functions.hlsl (scalar
// form, same as snaptogrid.metal / softtransformpoints.metal).
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B)
#include "particle_params.h"     // VelForceParams, ForceBinding
using namespace metal;

// hash11u — uint->float single-channel LCG cascade, 1:1 from TiXL hash-functions.hlsl:115 (TWO
// rounds; GLIBC k + _PRIME0 13331). Inlined here rather than shared/hash.metal.h: that header has
// no hash11u (only hash41u/hash31), and the other-lane copies in filterpoints/randomizepoints are
// different single-round variants — a self-contained leaf avoids disturbing them.
inline float velHash11u(uint x) {
    const uint k = 1103515245U;  // GLIB C
    x *= 13331U;                 // _PRIME0
    x = ((x >> 8U) ^ x) * k;
    x = ((x >> 8U) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffU));
}

// ApplyGainAndBias scalar — VERBATIM from bias-functions.hlsl (same as snaptogrid.metal).
inline float velGetBias(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
inline float velGetSchlickBias(float g, float x) {
    if (x < 0.5f) { x *= 2.0f; x = 0.5f * velGetBias(g, x); }
    else          { x = 2.0f * x - 1.0f; x = 0.5f * velGetBias(1.0f - g, x) + 0.5f; }
    return x;
}
inline float velApplyGainAndBias(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) { value = velGetBias(b, value); value = velGetSchlickBias(g, value); }
    else          { value = velGetSchlickBias(g, value); value = velGetBias(b, value); }
    return value;
}

kernel void velocity_force(device Particle*       Particles [[buffer(FORCE_Particles)]],
                           constant VelForceParams& P       [[buffer(FORCE_Params)]],
                           uint3                   tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // VelocityForce.hlsl:30-46 (1:1):
  float3 velocity = float3(Particles[tid.x].Velocity);
  float speed = length(velocity);
  if (speed < 0.0001f) return;  // "Rather do nothing..."

  float3 dir = normalize(velocity);

  float rand = velHash11u(tid.x);
  float randBias = velApplyGainAndBias(rand, float2(P.GainBiasX, P.GainBiasY));
  // hlsl: float3 variationFactor = (1 - (1-randBias) * saturate(Variation));  (scalar broadcast)
  float variationFactor = 1.0f - (1.0f - randBias) * saturate(P.Variation);

  float strength = P.Amount * variationFactor;

  speed += P.Accelerate * 0.02f * strength;
  speed = clamp(speed, P.MinSpeed, P.MaxSpeed);

  Particles[tid.x].Velocity = dir * speed;
}
