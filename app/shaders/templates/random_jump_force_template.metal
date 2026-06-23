// random_jump_force_template.metal — STRING TEMPLATE for the PF field-into-force bridge, RandomJumpForce.
// NOT precompiled. Mirrors field_distance_force_template.metal / vector_field_force_template.metal: it
// carries /*{...}*/ hooks that runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then
// platform/metal_compile compiles the result via newLibrary(source). It lives under shaders/templates/
// so the app/CMakeLists.txt metallib glob (`shaders/*.metal`, NON-recursive) does NOT pick it up (the
// hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search path
// (unlike the precompiled metallib build, which passes -I src/runtime -I shaders). So — exactly like the
// VFF/FieldDistance templates — this template INLINES every struct/constant it needs (the Particle layout,
// the buffer-slot indices) instead of #include "tixl_point.h" / "particle_params.h". It ALSO inlines the
// curlNoise dependency closure + hash41u + qRotateVec3 (the runtime source path can't #include
// shared/noise.metal.h / shared/hash.metal.h / shared/quat.metal.h either). Those inlined helpers are
// byte-identical ports of the precompiled-side originals (shared/noise.metal.h, shared/hash.metal.h,
// shared/quat.metal.h) — NOT a divergent re-impl. The inlined Particle layout is byte-identical to
// tixl_point.h (packed_float3 @0, float @12, float4 @16, float4 @32, packed_float3 @48, float @60 = 64B);
// the slot indices mirror particle_params.h ForceBinding.
//
// WHY a SEPARATE template from VFF/FieldDistance: identical codegen MECHANISM (assembleFieldMSL only
// string-fills GLOBALS/FLOAT_PARAMS/FIELD_CALL + the three TEXTURE hooks), but the KERNEL BODY differs.
// VectorFieldForce reads f.xyz as a velocity vector; FieldDistanceForce finite-differences the SDF normal;
// RandomJumpForce reads only the field's COLOR magnitude ((f.r+f.g+f.b)/3) to MODULATE a curlNoise jump.
//
// *** NAMED FORK — fork-RandomJump-position-write (RandomJumpForceTemplate.hlsl:75-77) ***: this kernel
// writes the particle's POSITION (not Velocity, unlike every other ported force), after rotating the jump
// vector by the particle's own Rotation quat (qRotateVec3). The golden reads back Position, not Velocity.
//
// PARITY (RandomJumpForceTemplate.hlsl:52-77): kernel body ported 1:1.
//   variationOffset = hash41u(i).xyz * Variation; pos = Position * 0.9 (sic: hlsl avoids the simplex glitch
//   at -1,0,0); noiseLookup = (pos + variationOffset + Phase*(1,-1,0)) * Frequency;
//   fieldAmount = (f.r+f.g+f.b)/3; amount = Amount/100 * fieldAmount; noise3 = curlNoise(noiseLookup);
//   noise3 *= AmountDistribution; noise3 = qRotateVec3(noise3, normalize(Rotation)); Position += noise3*amount.
// GetField samples the particle's RAW pos (== Position*0.9), p.w = 0 (field-eval mode).
//
// Param mapping discipline (NO invented ports): RandomJumpForce.cs exposes DirectionDistribution (Vector3)
// -> the template's float3 AmountDistribution (map DirectionDistribution -> AmountDistribution). The .cs
// also exposes AmountFromVelocity, which has NO slot in RandomJumpForceTemplate.hlsl (it belongs to a
// separate non-template variant) -> baked to 0, no invented port (same discipline as SnapToAnglesForce's
// RandomSeed in point_ops.cpp). AmountDistribution carried as 3 SCALARS (no packed_float3 in a cbuffer =
// zero alignment traps, matching AxisStep/SnapAngles); the kernel reassembles float3(ADx,ADy,ADz).
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
constant int FORCE_Params      = 1;  // constant RandomJumpForceParams& (b0)
constant int FORCE_FieldParams = 2;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined RandomJumpForceParams (mirror runtime/particle_params.h; all-scalar, no packed in cbuffer) ----
struct RandomJumpForceParams {
    float Amount;
    float Frequency;
    float Phase;
    float Variation;
    float AmountDistributionX;  // = DirectionDistribution.x (.cs) — float3 reassembled in-kernel
    float AmountDistributionY;
    float AmountDistributionZ;
    uint  Count;
};

// --- inlined hash41u (1:1 from shared/hash.metal.h; _PRIME0 = 13331U) ----
inline float4 rjfHash41u(uint x) {
    const uint k = 1103515245U; // GLIB C
    x *= 13331U;                // _PRIME0
    x = ((x >> 8U) ^ x) * k;
    uint y = ((x >> 8U) ^ x) * k;
    uint z = ((y >> 8U) ^ x) * k;
    uint w = ((z >> 8U) ^ y) * k;
    uint4 i4 = uint4(x, y, z, w);
    return float4(i4) * (1.0f / float(0xffffffffU));
}

// --- inlined curlNoise dependency closure (1:1 from shared/noise.metal.h) ----
inline float3 rjfMod289(float3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
inline float4 rjfMod289(float4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
inline float4 rjfPermute(float4 x) { return rjfMod289(((x * 34.0) + 1.0) * x); }
inline float4 rjfTaylorInvSqrt(float4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
inline float rjfSnoise(float3 v) {
    const float2 C = float2(1.0 / 6.0, 1.0 / 3.0);
    const float4 D = float4(0.0, 0.5, 1.0, 2.0);
    float3 i  = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);
    float3 g  = step(x0.yzx, x0.xyz);
    float3 l  = 1.0 - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);
    float3 x1 = x0 - i1 + C.xxx;
    float3 x2 = x0 - i2 + C.yyy;
    float3 x3 = x0 - D.yyy;
    i = rjfMod289(i);
    float4 p = rjfPermute(rjfPermute(rjfPermute(
                   i.z + float4(0.0, i1.z, i2.z, 1.0)) +
                   i.y + float4(0.0, i1.y, i2.y, 1.0)) +
                   i.x + float4(0.0, i1.x, i2.x, 1.0));
    float  n_ = 0.142857142857;
    float3 ns = n_ * D.wyz - D.xzx;
    float4 j  = p - 49.0 * floor(p * ns.z * ns.z);
    float4 x_ = floor(j * ns.z);
    float4 y_ = floor(j - 7.0 * x_);
    float4 x = x_ * ns.x + ns.yyyy;
    float4 y = y_ * ns.x + ns.yyyy;
    float4 h = 1.0 - abs(x) - abs(y);
    float4 b0 = float4(x.xy, y.xy);
    float4 b1 = float4(x.zw, y.zw);
    float4 s0 = floor(b0) * 2.0 + 1.0;
    float4 s1 = floor(b1) * 2.0 + 1.0;
    float4 sh = -step(h, float4(0, 0, 0, 0));
    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;
    float3 p0 = float3(a0.xy, h.x);
    float3 p1 = float3(a0.zw, h.y);
    float3 p2 = float3(a1.xy, h.z);
    float3 p3 = float3(a1.zw, h.w);
    float4 norm = rjfTaylorInvSqrt(float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    float4 m = max(0.6 - float4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, float4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}
inline float3 rjfSnoiseVec3(float3 p) {
    float s  = rjfSnoise(float3(p.x + 0.0001, p.y,         p.z));
    float s1 = rjfSnoise(float3(p.y - 19.1,   p.z + 33.4,  p.x + 47.2));
    float s2 = rjfSnoise(float3(p.z + 74.2,   p.x - 124.5, p.y + 99.4));
    return float3(s, s1, s2);
}
inline float3 rjfCurlNoise(float3 p) {
    const float e = 0.001;
    float3 dx = float3(e, 0.0, 0.0);
    float3 dy = float3(0.0, e, 0.0);
    float3 dz = float3(0.0, 0.0, e);
    float3 p_x0 = rjfSnoiseVec3(p - dx);
    float3 p_x1 = rjfSnoiseVec3(p + dx);
    float3 p_y0 = rjfSnoiseVec3(p - dy);
    float3 p_y1 = rjfSnoiseVec3(p + dy);
    float3 p_z0 = rjfSnoiseVec3(p - dz);
    float3 p_z1 = rjfSnoiseVec3(p + dz);
    float x = p_y1.z - p_y0.z - p_z1.y + p_z0.y;
    float y = p_z1.x - p_z0.x - p_x1.z + p_x0.z;
    float z = p_x1.y - p_x0.y - p_y1.x + p_y0.x;
    const float divisor = 1.0 / (2.0 * e);
    return normalize(float3(x, y, z) * divisor);
}

// --- inlined qRotateVec3 (1:1 from shared/quat.metal.h) ----
inline float3 rjfQRotateVec3(float3 v, float4 q) {
    float3 t = 2 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = field eval,
// matching TiXL GetField's `float4(p3.xyz, 0)`). Returns f: f.rgb = the field color (RandomJumpForce reads
// only its magnitude), f.w = the field weight/decay. The SEED is all-ones (TiXL GetField `float4 f = 1;`) —
// a single SDF leaf overwrites only f.w, so f.rgb stays the (1,1,1) seed (fieldAmount=1) for the SphereSDF
// graph the golden wires.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

kernel void random_jump_force(device Particle*               Particles [[buffer(0)]],
                              constant RandomJumpForceParams& P         [[buffer(1)]],
                              constant FieldParams&           FP        [[buffer(2)]]/*{TEXTURES}*/,
                              uint3                           tid       [[thread_position_in_grid]]) {
  uint maxParticleCount = P.Count;
  if (tid.x >= maxParticleCount) return;

  // Faithful + harmless: skip un-emitted slots (BirthTime == NaN). Matches the VFF/FieldDistance guard
  // (RandomJumpForceTemplate.hlsl has no such guard; it's the sw force-pass convention, harmless).
  if (isnan(Particles[tid.x].BirthTime)) return;

  // hlsl:61-63 — variation offset + the *0.9 simplex-glitch avoidance + the lookup.
  float3 variationOffset = rjfHash41u(tid.x).xyz * P.Variation;
  float3 pos = float3(Particles[tid.x].Position) * 0.9f;  // avoid simplex noise glitch at -1,0,0
  float3 noiseLookup = (pos + variationOffset + P.Phase * float3(1, -1, 0)) * P.Frequency;

  // hlsl:67-70 — sample the field at pos (p.w = 0 field-eval mode), modulate amount by its color magnitude.
  float4 field = evalField(float4(pos, 0.0f), FP/*{TEXTURE_ARGS}*/);
  float fieldAmount = (field.r + field.g + field.b) / 3.0f;
  float amount = P.Amount / 100.0f * fieldAmount;

  // hlsl:71-77 — curl-noise jump, scaled by AmountDistribution, rotated by the particle's own Rotation,
  // then ADDED to POSITION (the named fork-RandomJump-position-write).
  float3 noise3 = rjfCurlNoise(noiseLookup);
  Particle p = Particles[tid.x];
  noise3 *= float3(P.AmountDistributionX, P.AmountDistributionY, P.AmountDistributionZ);
  noise3 = rjfQRotateVec3(noise3, normalize(p.Rotation));

  Particles[tid.x].Position = float3(Particles[tid.x].Position) + noise3 * amount;
}
