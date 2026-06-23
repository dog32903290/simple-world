// TiXL FieldVolumeForce, ported from
// external/tixl Operators/Lib/Assets/shaders/particles/FieldVolumeForce.hlsl:91-151.
// A force compute pass that samples a wired SDF FIELD's distance (.w) at each particle's position,
// reflects the velocity off the surface on a boundary crossing (bounce), else attracts (outside) /
// repels (inside). The integrator runs after and moves position by the modified velocity.
//
// === fork-FieldVolume-baked (named) ======================================================
// FieldVolumeForce's Field input is a ShaderGraphNode (a procedural SDF) — the field-graph subsystem
// TiXL builds at runtime. With a field wired the cook compiles the RUNTIME template
// (field_volume_force_template.metal) where evalField samples the real SDF distance. This PRECOMPILED
// kernel is the BAKED FALLBACK for NO field wired (mirror fork-FieldDistance-baked, field_distance_force
// .metal): GetField() returns f = float4(1,1,1,1), so GetDistance == f.w == 1 EVERYWHERE. A constant
// distance field has zero gradient -> GetNormal = normalize(0) = NaN -> force = NaN -> the final
// isnan(velocity) guard (kept verbatim from the HLSL) returns with NO velocity change. So with no field
// the force is a faithful no-op (no distance field -> no surface -> no bounce/attract/repel). This op
// becomes distinct only once an SDF field graph is wired (then the template path runs and samples it).
// =========================================================================================
//
// HLSL -> MSL: GetDimensions() -> host-supplied P.Count. RWStructuredBuffer<Particle> u0 ->
// device Particle*. The b1/b2 FloatParams (the field's packed buffer) is the field path's resource; the
// baked fallback declares NO FieldParams buffer (no field) -> byte-identical binding to the other baked
// force kernels (Particles@0, Params@1).
#include <metal_stdlib>
#include "tixl_point.h"          // Particle (64B packed)
#include "particle_params.h"     // FieldVolumeForceParams, ForceBinding
using namespace metal;

kernel void field_volume_force(device Particle*                Particles [[buffer(FORCE_Particles)]],
                               constant FieldVolumeForceParams& P        [[buffer(FORCE_Params)]],
                               uint3                            tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // Faithful + harmless: skip un-emitted slots (BirthTime == NaN). Matches the other force kernels.
  if (isnan(Particles[tid.x].BirthTime)) return;

  // No field bound (fork-FieldVolume-baked): GetField() -> f.w = 1 everywhere. distance == distanceNext == 1
  // (constant field, identical at pos and posNext), so sign(distance*distanceNext) = +1 -> no crossing ->
  // the else (attract/repel) branch. GetNormal of a constant distance field has zero gradient, so
  // surfaceN = normalize(0) = NaN. Compute it faithfully so the final isnan(velocity) guard is the
  // load-bearing no-op (matches the runtime template body exactly).
  float distance = 1.0f;  // GetDistance(pos) with the baked all-ones field
  float3 surfaceN = normalize(
      1.0f * float3( 1, -1, -1) +   // GetDistance(p + ( h,-h,-h)) == 1 (constant field)
      1.0f * float3(-1,  1, -1) +   // GetDistance(p + (-h, h,-h)) == 1
      1.0f * float3(-1, -1,  1) +   // GetDistance(p + (-h,-h, h)) == 1
      1.0f * float3( 1,  1,  1));   // GetDistance(p + ( h, h, h)) == 1
  surfaceN *= P.InvertVolumeFactor;

  float3 velocity = float3(Particles[tid.x].Velocity);
  float3 force = float3(0.0f);
  // distance*distanceNext = 1 > 0 -> no crossing -> else branch (attract/repel); both produce NaN via surfaceN.
  if (distance * P.InvertVolumeFactor < 0.0f) {
    force = surfaceN * P.Repulsion;
  } else {
    force = -surfaceN * P.Attraction / (1.0f + distance * P.AttractionDecay);
  }
  velocity += force * P.Amount * P.SpeedFactor;

  // hlsl:149-150 — NaN guard (velocity is NaN from the degenerate constant field) -> no write.
  if (!isnan(velocity.x) && !isnan(velocity.y) && !isnan(velocity.z))
    Particles[tid.x].Velocity = velocity;
}
