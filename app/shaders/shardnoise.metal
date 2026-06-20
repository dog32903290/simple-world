// ShardNoise image generator — TiXL port.
// Source: external/tixl/Operators/Lib/Assets/shaders/img/generate/ShardNoise.hlsl
// Based on Shard Noise by @ENDESGA https://www.shadertoy.com/view/dlKyWw
// Ported with love to TiXL by Newemka, then ported to MSL here.
//
// HLSL→MSL translation notes:
//   frac()       → fract()    (fractional part, same semantics)
//   lerp()       → mix()      (linear interpolation, same semantics)
//   exp2()       → exp2()     (identical in MSL)
//   dot()        → dot()      (identical in MSL)
//   floor()      → floor()    (identical in MSL)
//   rsqrt()      → rsqrt()    (identical in MSL)
//   float3 constructor / swizzles: identical in MSL
//   #define tau: valid MSL preprocessor constant
//   Texture2D<float4> with sampler: kept for pipeline compatibility (dummy bound, not sampled)
//   cbuffer → constant buffer struct parameter [[buffer(n)]]
//   SV_POSITION → [[position]]
//   SV_TARGET   → (return type of fragment function)
//
// ApplyGainAndBias (bias-functions.hlsl) inlined verbatim — same GetBias+GetSchlickBias pattern
// already used in fractalnoise.metal. [fork-bias-functions-inline]
//
// [fork-no-sampler-read]: ShardNoise has no Image input in TiXL — the Texture2D<float4> ImageA
//   register(t0) exists in the HLSL for _ImageFxShaderSetupStatic infrastructure but is never
//   sampled. We bind a 1×1 dummy texture at t0 to satisfy the Metal pipeline; no sampler state
//   needed (shader never calls .sample()). [generator-dummy convention, Cut 61]
//
// [fork-direction-flip-verbatim]: TiXL flips Direction.x as a UX improvement so horizontal flow
//   matches mouse movement. `float2 _direction = Direction * float2(-1, 1)`. Ported verbatim.
//
// [fork-offset-flip-verbatim]: TiXL flips Offset.x: `float2 offset = Offset * float2(-1, 1)`.
//   Ported verbatim.

#include <metal_stdlib>
using namespace metal;

// ── ApplyGainAndBias (inlined from bias-functions.hlsl) ────────────────────────────────────────
// Matches fractalnoise.metal verbatim (same source). [fork-bias-functions-inline]

static inline float GetBias_sn(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

static inline float GetSchlickBias_sn(float g, float x) {
    if (x < 0.5f) {
        x *= 2.0f;
        x = 0.5f * GetBias_sn(g, x);
    } else {
        x = 2.0f * x - 1.0f;
        x = 0.5f * GetBias_sn(1.0f - g, x) + 0.5f;
    }
    return x;
}

static inline float ApplyGainAndBias_sn(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);

    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;

    if (g < 0.5f) {
        value = GetBias_sn(b, value);
        value = GetSchlickBias_sn(g, value);
    } else {
        value = GetSchlickBias_sn(g, value);
        value = GetBias_sn(b, value);
    }
    return value;
}

// ── cbuffer structs ────────────────────────────────────────────────────────────────────────────
//
// IMPORTANT: Use all individual scalar floats — NO float2/float4 members.
// MSL aligns float2 to 8 bytes and float4 to 16 bytes, which inserts implicit padding
// between scalars and vectors that breaks parity with the HLSL cbuffer flat float packing.
// (Same trap as packed_float3 — the host ShardNoiseParams uses the same all-scalar layout.)
// 24 floats × 4 bytes = 96 bytes, matching the HLSL cbuffer exactly.
struct ShardNoise_Params {
    // ColorA (4 floats, HLSL float4, offset 0-3)
    float ColorAR, ColorAG, ColorAB, ColorAA;
    // ColorB (4 floats, HLSL float4, offset 4-7)
    float ColorBR, ColorBG, ColorBB, ColorBA;
    // Direction (2 floats, HLSL float2, offset 8-9)
    float DirectionX, DirectionY;
    // Stretch (2 floats, HLSL float2, offset 10-11)
    float StretchX, StretchY;
    // Scalars (offset 12-15, one 16-byte register)
    float Scale, Sharpness, Phase, Rate;
    // Method + GainAndBias + Offset (offset 16-20, next register)
    float Method;
    float GainX, BiasY;
    float OffsetX, OffsetY;
    // Explicit pad + implicit pad (offset 21-23, completes 6th register)
    float _pad, _pad2, _pad3;
};

struct ShardNoise_Resolution {
    float TargetWidth;
    float TargetHeight;
};

// ── Vertex shader ──────────────────────────────────────────────────────────────────────────────

struct vsOutput {
    float4 position [[position]];
    float2 texCoord;
};

vertex vsOutput shardnoise_vs(uint vid [[vertex_id]]) {
    // Full-screen triangle (vertices 0,1,2 → NDC [-1..3]).
    float2 pos = float2((vid == 1) ? 3.0f : -1.0f,
                        (vid == 2) ? 3.0f : -1.0f);
    vsOutput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.texCoord = float2((pos.x + 1.0f) * 0.5f, (1.0f - pos.y) * 0.5f);
    return o;
}

// ── Noise functions (ported verbatim from ShardNoise.hlsl) ─────────────────────────────────────

// Spatial hash (ported verbatim from ShardNoise.hlsl lines 42-46).
// Constants load-bearing for parity — do NOT change.
static float3 sn_hash(float3 p) {
    p = float3(dot(p, float3(127.1f, 311.7f, 74.7f)),
               dot(p, float3(269.5f, 183.3f, 246.1f)),
               dot(p, float3(113.5f, 271.9f, 124.6f)));
    return fract(sin(p) * 43758.5453123f);
}

#define tau 6.283185307179586f

// shard_noise: 3D Voronoi-style noise with exp2-weighted tanh approximation.
// Ported verbatim from ShardNoise.hlsl lines 50-76.
static float shard_noise(float3 p, float sharpness) {
    float3 ip = floor(p);
    float3 fp = fract(p);

    float v = 0.0f, t = 0.0f;
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                float3 o  = float3(x, y, z);
                float3 io = ip + o;
                float3 h  = sn_hash(io);
                float3 r  = fp - (o + h);

                float w = exp2(-tau * dot(r, r));
                // tanh deconstruction and optimization by @Xor (ported verbatim):
                float s = sharpness * dot(r, sn_hash(io + float3(11.0f, 31.0f, 47.0f)) - 0.5f);
                v += w * s * rsqrt(1.0f + s * s);
                t += w;
            }
        }
    }
    return ((v / t) * 0.5f) + 0.5f;
}

// ── Fragment shader ────────────────────────────────────────────────────────────────────────────

fragment float4 shardnoise_fs(
    vsOutput                         in   [[stage_in]],
    texture2d<float>                 tex  [[texture(0)]],
    constant ShardNoise_Params&      p    [[buffer(0)]],
    constant ShardNoise_Resolution&  res  [[buffer(1)]])
{
    float aspectRatio = res.TargetWidth / res.TargetHeight;

    // Reconstruct float2/float4 from individual scalars (no MSL alignment padding).
    float4 colorA = float4(p.ColorAR, p.ColorAG, p.ColorAB, p.ColorAA);
    float4 colorB = float4(p.ColorBR, p.ColorBG, p.ColorBB, p.ColorBA);
    float2 gainBias = float2(p.GainX, p.BiasY);

    // [fork-offset-flip-verbatim]: TiXL flips Offset.x for UX consistency.
    float2 offset = float2(p.OffsetX, p.OffsetY) * float2(-1.0f, 1.0f);
    float2 uv = in.texCoord + offset;
    uv -= 0.5f;
    uv.x *= aspectRatio;
    uv /= float2(p.StretchX, p.StretchY);

    // [fork-direction-flip-verbatim]: TiXL flips Direction.x so horizontal flow matches mouse.
    float2 _direction = float2(p.DirectionX, p.DirectionY) * float2(-1.0f, 1.0f);

    // Build 3D coordinate: (xy + direction*phase, phase*0.05*rate).
    float3 coord = float3(uv + _direction * p.Phase, p.Phase * 0.05f * p.Rate);

    float _sharpness = p.Sharpness * 128.0f;

    float c = 0.0f;
    float sn = ApplyGainAndBias_sn(shard_noise(p.Scale * coord, _sharpness), gainBias);

    // Method switch — ported verbatim from ShardNoise.hlsl lines 103-133.
    // [fork-method-round]: IntToFloat in .t3; round to nearest int before switch.
    int method = (int)(p.Method + 0.5f);
    if (method == 0) {
        // Cubism
        c = sn;
    } else if (method == 1) {
        // Cubism × Octaves
        float o_val = ApplyGainAndBias_sn(
            (shard_noise(64.0f * coord, 4.0f) * 0.03125f) +
            (shard_noise(32.0f * coord, 4.0f) * 0.0625f)  +
            (shard_noise(16.0f * coord, 4.0f) * 0.125f)   +
            (shard_noise( 8.0f * coord, 4.0f) * 0.25f)    +
            (shard_noise( 4.0f * coord, 4.0f) * 0.5f),
            gainBias);
        c = sn * o_val;
    } else {
        // Octaves
        c = ApplyGainAndBias_sn(
            (shard_noise(64.0f * coord, 4.0f) * 0.03125f) +
            (shard_noise(32.0f * coord, 4.0f) * 0.0625f)  +
            (shard_noise(16.0f * coord, 4.0f) * 0.125f)   +
            (shard_noise( 8.0f * coord, 4.0f) * 0.25f)    +
            (shard_noise( 4.0f * coord, 4.0f) * 0.5f),
            gainBias);
    }

    return mix(colorA, colorB, c);
}
