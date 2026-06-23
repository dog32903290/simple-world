// vector_field_force_template.metal — STRING TEMPLATE for the PF-a field-into-force bridge.
// NOT precompiled. Mirrors field_render_template.metal: it carries /*{...}*/ hooks that
// runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then platform/metal_compile compiles
// the result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt
// metallib glob (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL
// until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search
// path (unlike the precompiled metallib build, which passes -I src/runtime -I shaders). So — exactly
// like field_render_template.metal — this template INLINES every struct/constant it needs (the Particle
// layout, the buffer-slot indices) instead of #include "tixl_point.h" / "particle_params.h". The inlined
// Particle layout is byte-identical to tixl_point.h (packed_float3 @0, float @12, float4 @16, float4 @32,
// packed_float3 @48, float @60 = 64 bytes); the slot indices mirror particle_params.h ForceBinding.
//
// WHY a SEPARATE template from field_render_template: the field RENDER path is a fullscreen DRAW
// (vertex+fragment, writes f.w into a texture). The field-into-FORCE path is a COMPUTE kernel
// (per-particle GetField sample, writes Velocity). The codegen MECHANISM is identical (assembleFieldMSL
// is template-agnostic — it only string-fills GLOBALS/FLOAT_PARAMS/FIELD_CALL + the three TEXTURE hooks),
// but the kernel body differs, so this is its own template.
//
// PARITY (TiXL VectorFieldForce-sg.hlsl): kernel body ported 1:1 from vector_field_force.metal:40-64,
// with the baked `float4 f = float4(1,1,1,1)` (fork-VFF) replaced by `evalField(...)` running the
// assembled field tree. TiXL's b1 cbuffer (the field's FloatParams) becomes a `constant FieldParams&`
// bound at [[buffer(2)]] (== particle_params.h FORCE_FieldParams; == HLSL register(b1)).
//
// *** CRITICAL PARITY FACT (VectorFieldForce-sg.hlsl:60-61) ***: GetField samples the particle's RAW
// Position — `float4 f = GetField(float4(pos, 0));`. There is NO field-space remap (the render
// template's uv->p mapping is render-only, FieldToImageTemplate-specific). The particle's world
// position IS the field sample point, and p.w = 0 (field-eval mode, matching GetField's float4(pos,0)).
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
constant int FORCE_Params      = 1;  // constant VecFieldForceParams& (b0)
constant int FORCE_FieldParams = 2;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined VecFieldForceParams (mirror runtime/particle_params.h) ----
struct VecFieldForceParams {
    float Amount;
    float Variation;
    float SpeedFactor;
    uint  Count;
};

// hash11u — uint->float single-channel LCG cascade, 1:1 from TiXL hash-functions.hlsl:115 (TWO rounds,
// GLIBC k + 13331 prime). Inlined here exactly as in vector_field_force.metal (self-contained leaf).
inline float vffHash11u(uint x) {
    const uint k = 1103515245U; // GLIB C
    x *= 13331U;                // _PRIME0
    x = ((x >> 8U) ^ x) * k;
    x = ((x >> 8U) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffU));
}

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = field eval,
// matching TiXL GetField's `float4(p3.xyz, 0)`). Returns f: f.w = field weight/decay, f.xyz = the field
// vector (TiXL convention). The SEED is all-ones (TiXL GetField `float4 f = 1;`, VectorFieldForce-sg
// .hlsl:42) — a node reads f before it writes (load-bearing for combiners); a single ToroidalVortexField
// leaf unconditionally overwrites the whole f, so the seed is invisible for the current single-leaf graph.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

kernel void vector_field_force(device Particle*             Particles [[buffer(0)]],
                               constant VecFieldForceParams& P        [[buffer(1)]],
                               constant FieldParams&         FP        [[buffer(2)]]/*{TEXTURES}*/,
                               uint3                         tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // VectorFieldForce-sg.hlsl:57 guard: skip un-emitted slots (BirthTime == NaN). Faithful + harmless.
  if (isnan(Particles[tid.x].BirthTime)) return;

  // hlsl:60-61 — sample the field at the particle's RAW Position (no remap), p.w = 0 (field-eval mode).
  float3 pos = float3(Particles[tid.x].Position);
  float4 f = evalField(float4(pos, 0.0f), FP/*{TEXTURE_ARGS}*/);

  // hlsl:63 — float3 variationFactor = hash11u(i.x) * Variation + 1;
  float3 variationFactor = vffHash11u(tid.x) * P.Variation + 1.0f;

  // hlsl:65 — float3 velocity = f.xyz * Amount * f.w * variationFactor;
  // SpeedFactor is the sw force-pass convention's global speed multiplier (== TurbulenceForce's, set 1.0
  // by the cook); not in the HLSL b0 but applied here so every force kernel shares the convention.
  float3 velocity = f.xyz * P.Amount * f.w * variationFactor * P.SpeedFactor;

  // hlsl:67-68 — NaN guard before accumulate.
  if (!isnan(velocity.x) && !isnan(velocity.y) && !isnan(velocity.z))
    Particles[tid.x].Velocity = float3(Particles[tid.x].Velocity) + velocity;
}
