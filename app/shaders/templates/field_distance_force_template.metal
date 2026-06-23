// field_distance_force_template.metal — STRING TEMPLATE for the PF field-into-force bridge, FieldDistanceForce.
// NOT precompiled. Mirrors vector_field_force_template.metal: it carries /*{...}*/ hooks that
// runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then platform/metal_compile compiles
// the result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt
// metallib glob (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL
// until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search
// path (unlike the precompiled metallib build, which passes -I src/runtime -I shaders). So — exactly
// like vector_field_force_template.metal — this template INLINES every struct/constant it needs (the
// Particle layout, the buffer-slot indices) instead of #include "tixl_point.h" / "particle_params.h".
// The inlined Particle layout is byte-identical to tixl_point.h (packed_float3 @0, float @12, float4 @16,
// float4 @32, packed_float3 @48, float @60 = 64 bytes); the slot indices mirror particle_params.h ForceBinding.
//
// WHY a SEPARATE template from vector_field_force_template: identical codegen MECHANISM (assembleFieldMSL
// only string-fills GLOBALS/FLOAT_PARAMS/FIELD_CALL + the three TEXTURE hooks), but the KERNEL BODY differs.
// VectorFieldForce reads f.xyz as a velocity vector; FieldDistanceForce instead samples the SDF distance
// f.w four times (GetFieldNormal finite-diff) and pushes the particle along the surface normal.
//
// PARITY (TiXL FieldDistanceForce.hlsl:74-101): kernel body ported 1:1. GetFieldNormal (hlsl:65-72) is the
// 4-tap tetrahedral finite-difference of GetDistance==GetField().w. d>0 attract toward surface scaled by
// pow(d+1,-DecayWithDistance); d<=0 repel. NaN guards on d and n.x kept verbatim.
//
// *** CRITICAL PARITY FACT (FieldDistanceForce.hlsl:81) ***: GetField samples the particle's RAW Position
// (float3 pos = p.Position; GetDistance(pos) == GetField(float4(pos,0)).w). NO field-space remap (the render
// template's uv->p mapping is render-only). p.w = 0 (field-eval mode, matching GetField's float4(p3.xyz,0)).
#include <metal_stdlib>
using namespace metal;

// --- inlined Particle (byte-identical to runtime/tixl_point.h Particle, 64B packed) ----
struct Particle {
    packed_float3 Position;   // @0
    float         Radius;     // @12
    float4        Rotation;   // @16
    float4        Color;      // @32
    packed_float3 Velocity;   // @48
    float         BirthTime;  // @60
};                            // 64

// --- inlined force buffer-slot indices (mirror runtime/particle_params.h ForceBinding) ----
constant int FORCE_Particles   = 0;  // device Particle* (u0)
constant int FORCE_Params      = 1;  // constant FieldDistForceParams& (b0)
constant int FORCE_FieldParams = 2;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined FieldDistForceParams (mirror runtime/particle_params.h) ----
struct FieldDistForceParams {
    float Amount;
    float Attraction;
    float Repulsion;
    float NormalSamplingDistance;
    float DecayWithDistance;
    uint  Count;
    float _pad0;
    float _pad1;
};

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = field eval,
// matching TiXL GetField's `float4(p3.xyz, 0)`). Returns f: f.w = the SDF distance (field_ops_spheresdf.cpp:56
// emits f.w = length(p-Center)-Radius). The SEED is all-ones (TiXL GetField `float4 f = 1;`) — a single SDF
// leaf unconditionally overwrites f.w, so the seed is invisible for the current single-leaf graph (no field
// wired -> f.w stays 1 -> the baked fallback degenerate, see point_ops.cpp).
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance — sample the field distance .w at p3 (p.w=0 field-eval mode). FieldDistanceForce.hlsl:44-47.
static inline float fdGetDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return evalField(float4(p3, 0.0f), P/*{TEXTURE_ARGS}*/).w;
}

// GetFieldNormal — tetrahedral 4-tap finite difference of the SDF distance. FieldDistanceForce.hlsl:65-72.
static inline float3 fdGetFieldNormal(float3 p, float h, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return normalize(
        fdGetDistance(p + float3( h, -h, -h), P/*{TEXTURE_ARGS}*/) * float3( 1, -1, -1) +
        fdGetDistance(p + float3(-h,  h, -h), P/*{TEXTURE_ARGS}*/) * float3(-1,  1, -1) +
        fdGetDistance(p + float3(-h, -h,  h), P/*{TEXTURE_ARGS}*/) * float3(-1, -1,  1) +
        fdGetDistance(p + float3( h,  h,  h), P/*{TEXTURE_ARGS}*/) * float3( 1,  1,  1));
}

kernel void field_distance_force(device Particle*             Particles [[buffer(0)]],
                                 constant FieldDistForceParams& P        [[buffer(1)]],
                                 constant FieldParams&         FP        [[buffer(2)]]/*{TEXTURES}*/,
                                 uint3                         tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // Faithful + harmless: skip un-emitted slots (BirthTime == NaN). Matches the VFF template guard.
  if (isnan(Particles[tid.x].BirthTime)) return;

  // hlsl:81-83 — sample the field at the particle's RAW Position (no remap), p.w = 0 (field-eval mode).
  float3 pos = float3(Particles[tid.x].Position);
  float3 n = fdGetFieldNormal(pos, P.NormalSamplingDistance, FP/*{TEXTURE_ARGS}*/);
  float  d = fdGetDistance(pos, FP/*{TEXTURE_ARGS}*/);

  // hlsl:85-86 — KEEP the NaN guards (degenerate normal / distance -> no push).
  if (isnan(d) || isnan(n.x)) return;

  float3 vel = float3(Particles[tid.x].Velocity);
  if (d > 0.0f) {
    // hlsl:89-93 — attract outside: pull toward the surface, decay with distance.
    float decay = pow(d + 1.0f, -P.DecayWithDistance);
    vel -= n * P.Attraction * P.Amount * decay;
  } else {
    // hlsl:95-97 — repell inside: push away from the surface.
    vel += n * P.Repulsion * P.Amount;
  }
  Particles[tid.x].Velocity = vel;
}
