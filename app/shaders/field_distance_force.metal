// TiXL FieldDistanceForce, ported from
// external/tixl Operators/Lib/Assets/shaders/particles/FieldDistanceForce.hlsl:74-101.
// A force compute pass that samples a wired SDF FIELD's distance (.w) at each particle's position,
// computes a finite-difference surface normal, and pushes the particle along it (attract outside,
// repel inside). The integrator runs after and moves position by the modified velocity.
//
// === fork-FieldDistance-baked (named) ====================================================
// FieldDistanceForce's Field input is a ShaderGraphNode (a procedural SDF) — the field-graph
// subsystem TiXL builds at runtime. With a field wired the cook compiles the RUNTIME template
// (field_distance_force_template.metal) where evalField samples the real SDF distance. This
// PRECOMPILED kernel is the BAKED FALLBACK for NO field wired (mirror fork-VFF, vector_field_force
// .metal): GetField() returns f = float4(1,1,1,1), so GetDistance == f.w == 1 EVERYWHERE. A constant
// distance field has zero gradient -> GetFieldNormal = normalize(0) = NaN -> the isnan(n.x) guard
// (kept verbatim from the HLSL) returns with NO velocity change. So with no field the force is a
// faithful no-op (no distance field -> no surface -> no attract/repel). This op becomes distinct only
// once an SDF field graph is wired (then the template path runs and samples it).
// =========================================================================================
//
// HLSL -> MSL: GetDimensions() -> host-supplied P.Count. RWStructuredBuffer<Particle> u0 ->
// device Particle*. The b1 FloatParams (the field's packed buffer) is the field path's resource;
// the baked fallback declares NO FieldParams buffer (no field) -> byte-identical binding to the
// other baked force kernels (Particles@0, Params@1).
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B packed)
#include "particle_params.h"     // FieldDistForceParams, ForceBinding
using namespace metal;

kernel void field_distance_force(device Particle*               Particles [[buffer(FORCE_Particles)]],
                                 constant FieldDistForceParams& P         [[buffer(FORCE_Params)]],
                                 uint3                          tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // Faithful + harmless: skip un-emitted slots (BirthTime == NaN). Matches the other force kernels.
  if (isnan(Particles[tid.x].BirthTime)) return;

  // No field bound (fork-FieldDistance-baked): GetField() -> f.w = 1 everywhere. A constant distance
  // field has zero gradient, so GetFieldNormal = normalize(0) = NaN. Compute it faithfully so the
  // isnan(n.x) guard below is the load-bearing no-op (matches the runtime template body exactly).
  float d = 1.0f;  // GetDistance(pos) with the baked all-ones field
  float h = P.NormalSamplingDistance;
  float3 n = normalize(
      1.0f * float3( 1, -1, -1) +   // GetDistance(p + ( h,-h,-h)) == 1 (constant field)
      1.0f * float3(-1,  1, -1) +   // GetDistance(p + (-h, h,-h)) == 1
      1.0f * float3(-1, -1,  1) +   // GetDistance(p + (-h,-h, h)) == 1
      1.0f * float3( 1,  1,  1));   // GetDistance(p + ( h, h, h)) == 1
  (void)h;

  // hlsl:85-86 — NaN guard (here n is NaN from the degenerate constant field) -> no push.
  if (isnan(d) || isnan(n.x)) return;

  float3 vel = float3(Particles[tid.x].Velocity);
  if (d > 0.0f) {
    float decay = pow(d + 1.0f, -P.DecayWithDistance);
    vel -= n * P.Attraction * P.Amount * decay;
  } else {
    vel += n * P.Repulsion * P.Amount;
  }
  Particles[tid.x].Velocity = vel;
}
