// field_volume_force_template.metal — STRING TEMPLATE for the PF field-into-force bridge, FieldVolumeForce.
// NOT precompiled. Mirrors field_distance_force_template.metal / random_jump_force_template.metal: it carries
// /*{...}*/ hooks that runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then platform/metal_compile
// compiles the result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt
// metallib glob (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search path
// (unlike the precompiled metallib build, which passes -I src/runtime -I shaders). So — exactly like the
// VFF/FieldDistance/RandomJump templates — this template INLINES every struct/constant it needs (the Particle
// layout, the buffer-slot indices). It ALSO inlines hash41u (the runtime source path can't #include
// shared/hash.metal.h); the inlined fvfHash41u is a byte-identical port of TiXL hash-functions.hlsl:102-113
// (_PRIME0 = 13331U, GLIB-C k) — the SAME port RandomJump uses (rjfHash41u). The inlined Particle layout is
// byte-identical to tixl_point.h (packed_float3 @0, float @12, float4 @16, float4 @32, packed_float3 @48,
// float @60 = 64B); the slot indices mirror particle_params.h ForceBinding.
//
// WHY a SEPARATE template from VFF/FieldDistance/RandomJump: identical codegen MECHANISM (assembleFieldMSL only
// string-fills GLOBALS/FLOAT_PARAMS/FIELD_CALL + the three TEXTURE hooks), but the KERNEL BODY differs.
// FieldVolumeForce REFLECTS the velocity off the SDF surface (bounce) when the particle crosses the boundary
// this step, otherwise attracts (outside) / repels (inside) — see FieldVolumeForce.hlsl:91-151.
//
// PARITY (TiXL FieldVolumeForce.hlsl:91-151): kernel body ported 1:1. GetNormal (hlsl:68-75) is the 4-tap
// tetrahedral finite-difference of GetDistance==GetField().w (same as FieldDistance's fdGetFieldNormal, but
// the offset is the fixed NormalSamplingDistance). The crossing test uses posNext = pos + velocity*SpeedFactor
// *0.01*2; on a sign flip AND distance*InvertVolumeFactor>0 it reflects (lerp by EnableBounce/Amount, with
// RandomizeReflection/RandomizeBounce via hash41u(gi)); else it attracts/repels into Velocity. NaN guard on
// the final velocity write kept verbatim (hlsl:149-150). ApplyColorOnCollision writes Color.rgb on a bounce.
//
// *** PARITY ROUTING (FieldVolumeForce.t3 FloatsToBuffer connection order — traced, NOT assumed 1:1) ***
//   The b1 force cbuffer is filled by FloatsToBuffer in .t3 connection order; two slots are NOT 1:1 with the
//   .cs inputs (the routing trap, MEMORY sw-batch Cut55):
//     * Attraction (slot[1]) <- Attraction * 0.425 (a Multiply node B=0.425 sits on the Attraction path).
//       So shader Attraction = (.cs Attraction) * 0.425. The cook applies the *0.425 host-side.
//     * InvertVolumeFactor (slot[7]) <- BoolToFloat(InvertVolume): true->-1.0, false->+1.0 (NOT a raw bool).
//     * SpeedFactor (slot[9]) <- GetParticleComponents.SpeedFactor — a RUNTIME particle-system float, not an
//       inspector input. Our cook has no per-system SpeedFactor context, so it is 1.0 (== every other force's
//       SpeedFactor; the legacy turbulence pass also hardcodes 1.0). NAMED FORK fork-FieldVolume-speedfactor.
//   EnableBounce (b2 int) <- ReflectOnCollision (BoolToInt). ApplyColorOnCollision (b2 int) <- the bool.
//   These two b2 ints are carried in this struct as floats (host casts 0/1); the kernel compares !=0.
//
// *** NAMED FORK — fork-FieldVolume-baked ***: no field wired -> assembleFieldMSL is not invoked (the cook
// falls back to the baked field_volume_force.metal). There the field SEED is constant (.w degenerate), so
// GetNormal's gradient is 0 -> normalize(0) = NaN -> force = NaN -> the final isnan(velocity) guard blocks
// the write -> no-op (faithful: a constant field has no surface to bounce off / attract to). Same degenerate
// shape as fork-FieldDistance-baked.
//
// *** CRITICAL PARITY FACT (FieldVolumeForce.hlsl:102-103) ***: GetField samples the particle's RAW Position
// (no field-space remap — the render template's uv->p mapping is render-only). p.w = 0 (field-eval mode).
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
constant int FORCE_Params      = 1;  // constant FieldVolumeForceParams& (b0)
constant int FORCE_FieldParams = 2;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined FieldVolumeForceParams (mirror runtime/particle_params.h; all-scalar, no packed in cbuffer) ----
// Order == FieldVolumeForce.hlsl b1 cbuffer, with the routing forks baked by the cook (Attraction already
// *0.425, InvertVolumeFactor already -1/+1, SpeedFactor already 1.0; EnableBounce/ApplyColorOnCollision as 0/1).
struct FieldVolumeForceParams {
    float Amount;
    float Attraction;            // = (.cs Attraction) * 0.425 (cook applies the Multiply fork)
    float AttractionDecay;
    float Repulsion;
    float Bounciness;
    float RandomizeBounce;
    float RandomizeReflection;
    float InvertVolumeFactor;    // = InvertVolume ? -1 : +1 (cook applies the BoolToFloat fork)
    float NormalSamplingDistance;
    float SpeedFactor;           // = 1.0 (fork-FieldVolume-speedfactor; runtime particle-system value)
    float EnableBounce;          // = ReflectOnCollision ? 1 : 0 (b2 int carried as float)
    float ApplyColorOnCollision; // = ApplyColorOnCollision ? 1 : 0 (b2 int carried as float)
    uint  Count;
    float _pad0;
    float _pad1;
    float _pad2;                 // -> 64 bytes (16-byte multiple)
};

// --- inlined hash41u (1:1 from TiXL hash-functions.hlsl:102-113 / shared/hash.metal.h; _PRIME0 = 13331U) ----
inline float4 fvfHash41u(uint x) {
    const uint k = 1103515245U; // GLIB C
    x *= 13331U;                // _PRIME0
    x = ((x >> 8U) ^ x) * k;
    uint y = ((x >> 8U) ^ x) * k;
    uint z = ((y >> 8U) ^ x) * k;
    uint w = ((z >> 8U) ^ y) * k;
    uint4 i4 = uint4(x, y, z, w);
    return float4(i4) * (1.0f / float(0xffffffffU));
}

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = field eval,
// matching TiXL GetField's `float4(p3.xyz, 0)`). Returns f: f.w = the SDF distance, f.rgb = the field color
// (ApplyColorOnCollision reads f.rgb). The SEED is all-ones (TiXL GetField `float4 f = 1;`) — a single SDF
// leaf overwrites only f.w, so f.rgb stays the (1,1,1) seed for the SphereSDF graph the golden wires.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance — sample the field distance .w at p3 (p.w=0 field-eval mode). FieldVolumeForce.hlsl:61-64.
static inline float fvfGetDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return evalField(float4(p3, 0.0f), P/*{TEXTURE_ARGS}*/).w;
}

// GetNormal — tetrahedral 4-tap finite difference of the SDF distance. FieldVolumeForce.hlsl:68-75.
static inline float3 fvfGetNormal(float3 p, float offset, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return normalize(
        fvfGetDistance(p + float3( offset, -offset, -offset), P/*{TEXTURE_ARGS}*/) * float3( 1, -1, -1) +
        fvfGetDistance(p + float3(-offset,  offset, -offset), P/*{TEXTURE_ARGS}*/) * float3(-1,  1, -1) +
        fvfGetDistance(p + float3(-offset, -offset,  offset), P/*{TEXTURE_ARGS}*/) * float3(-1, -1,  1) +
        fvfGetDistance(p + float3( offset,  offset,  offset), P/*{TEXTURE_ARGS}*/) * float3( 1,  1,  1));
}

kernel void field_volume_force(device Particle*               Particles [[buffer(0)]],
                               constant FieldVolumeForceParams& P        [[buffer(1)]],
                               constant FieldParams&           FP        [[buffer(2)]]/*{TEXTURES}*/,
                               uint3                           tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // Faithful + harmless: skip un-emitted slots (BirthTime == NaN). Matches the VFF/FieldDistance/RandomJump
  // guard (FieldVolumeForce.hlsl comments its own BirthTime guard out, hlsl:99-100; it's the sw force-pass
  // convention, harmless to a live emitted particle).
  if (isnan(Particles[tid.x].BirthTime)) return;

  int gi = (int)tid.x;

  // hlsl:102-104 — sample the field at the particle's RAW Position (no remap), p.w = 0 (field-eval mode).
  float3 pos = float3(Particles[gi].Position);
  float  distance = fvfGetDistance(pos, FP/*{TEXTURE_ARGS}*/);
  float3 surfaceN = fvfGetNormal(pos, P.NormalSamplingDistance, FP/*{TEXTURE_ARGS}*/);

  // hlsl:107-109 — predict next position one sub-step ahead, sample the field there for the crossing test.
  float3 velocity = float3(Particles[gi].Velocity);
  float3 posNext = pos + velocity * P.SpeedFactor * 0.01f * 2.0f;  // hlsl casts to float4(...,1); .xyz used
  float  distanceNext = fvfGetDistance(posNext, FP/*{TEXTURE_ARGS}*/);

  float3 force = float3(0.0f);
  surfaceN *= P.InvertVolumeFactor;  // hlsl:112

  // hlsl:115 — reflect if the signed distance changes (a boundary crossing this step) and we are on the
  // attracting side (distance * InvertVolumeFactor > 0).
  if (sign(distance * distanceNext) < 0.0f && distance * P.InvertVolumeFactor > 0.0f) {
    float4 rand = fvfHash41u((uint)gi);                                      // hlsl:117
    float3 v = mix(velocity,                                                 // hlsl:118-120
                   reflect(velocity, surfaceN + (P.RandomizeReflection * (rand.xyz - 0.5f))),
                   P.EnableBounce);
    velocity = mix(velocity,                                                 // hlsl:122-128
                   (v * P.Bounciness * (P.RandomizeBounce * (rand.z - 0.5f) + 1.0f)),
                   P.Amount);
    if (P.ApplyColorOnCollision != 0.0f) {                                   // hlsl:130-134
      float4 surfaceColor = evalField(float4(pos, 1.0f), FP/*{TEXTURE_ARGS}*/);
      Particles[gi].Color = float4(surfaceColor.rgb, Particles[gi].Color.a);
    }
  } else {
    if (distance * P.InvertVolumeFactor < 0.0f) {                            // hlsl:138-141 — repel inside
      force = surfaceN * P.Repulsion;
    } else {                                                                 // hlsl:143-145 — attract outside
      force = -surfaceN * P.Attraction / (1.0f + distance * P.AttractionDecay);
    }
    velocity += force * P.Amount * P.SpeedFactor;                           // hlsl:146
  }

  // hlsl:149-150 — KEEP the NaN guard (degenerate normal/force -> no velocity write).
  if (!isnan(velocity.x) && !isnan(velocity.y) && !isnan(velocity.z))
    Particles[gi].Velocity = velocity;
}
