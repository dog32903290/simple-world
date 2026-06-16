// FractalNoise image generator — TiXL port.
// Source: external/tixl/Operators/Lib/Assets/shaders/img/generate/FractalNoise.hlsl
// (SHA-trace: CandyCat ShaderToy fBm, hash33 by @davidhoskins pattern, simplex by nikita).
//
// HLSL→MSL notes:
//   - frac()      → fract()   (same semantics: fractional part)
//   - lerp()      → mix()     (same semantics: linear interpolation)
//   - mul() / *   → * (same for scalar/vector in MSL)
//   - float3 constructor: identical in MSL
//   - dot(), abs(), max(), min(), floor(), saturate(), sin(), step(), clamp(): all identical in MSL
//   - HLSL comma-operator: `(a, a, a)` evaluates to last expr `a`; ported as `(a)` directly.
//     FractalNoise.hlsl line 59: `e * (1.0-e.zxy, 1.0-e.zxy, 1.0-e.zxy)` → `e * (1.0f - e.zxy)`
//   - `#define MOD3 float3(...)`: valid MSL preprocessor, identical semantics.
//   - ApplyGainAndBias (bias-functions.hlsl) inlined verbatim (GetBias + GetSchlickBias + ApplyGainAndBias).
//
// Hash: hash33() with MOD3 constants ported verbatim (not hash.metal.h — TiXL uses a different
// hash than the Dave Hoskins hash31/hash22/hash42 already in hash.metal.h).
// [NAMED FORK: hash33-verbatim] — hash33 uses frac/dot/float3 pattern from CandyCat ShaderToy;
//   constants (0.1031, 0.11369, 0.13787) ported exactly. Not merged into hash.metal.h to avoid
//   polluting the shared header with a shader-local hash.
//
// Generator: no input texture (FractalNoise.cs has no Image [Input] slot). Dummy 1×1 tex not
// needed — the shader does not use inputTexture at all (no sampler, no tex register read).
// However, the host must still bind the dummy tex to satisfy the metal pipeline (see cpp file).

#include <metal_stdlib>
using namespace metal;
#include "shared/hash.metal.h"

// ── ApplyGainAndBias (inlined from bias-functions.hlsl) ────────────────────────────────────────
// HLSL→MSL: GetBias, GetSchlickBias, ApplyGainAndBias ported verbatim. All float math identical.
// The scalar (float) overload of ApplyGainAndBias is used in psMain.

static inline float GetBias_fn(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

static inline float GetSchlickBias_fn(float g, float x) {
    if (x < 0.5f) {
        x *= 2.0f;
        x = 0.5f * GetBias_fn(g, x);
    } else {
        x = 2.0f * x - 1.0f;
        x = 0.5f * GetBias_fn(1.0f - g, x) + 0.5f;
    }
    return x;
}

static inline float ApplyGainAndBias_fn(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);

    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;

    if (g < 0.5f) {
        value = GetBias_fn(b, value);
        value = GetSchlickBias_fn(g, value);
    } else {
        value = GetSchlickBias_fn(g, value);
        value = GetBias_fn(b, value);
    }
    return value;
}

// ── cbuffer ────────────────────────────────────────────────────────────────────────────────────

struct FractalNoise_Params {
    float4 ColorA;         // [0]
    float4 ColorB;         // [1]
    float2 Offset;         // [2].xy
    float2 Stretch;        // [2].zw
    float  Scale;          // [3].x
    float  Phase;          // [3].y  (= RandomPhase; shader uses Phase/10)
    float  Iterations;     // [3].z  (float from IntToFloat; clamped 1..5 in shader)
    float  __padding;      // [3].w  (explicit pad — maps to __padding child in .t3)
    float2 GainAndBias;    // [4].xy
    float2 WarpOffsetXY;   // [4].zw
    float  WarpOffsetZ;    // [5].x
    float  _pad1, _pad2, _pad3;  // [5].yzw — fill 6th 16-byte register
};

struct FractalNoise_Resolution {
    float TargetWidth;
    float TargetHeight;
};

// ── Vertex shader ──────────────────────────────────────────────────────────────────────────────

struct vsOutput {
    float4 position [[position]];
    float2 texCoord;
};

vertex vsOutput fractalnoise_vs(uint vid [[vertex_id]]) {
    // Full-screen triangle (vertices 0,1,2 → NDC [-1..3]).
    float2 pos = float2((vid == 1) ? 3.0f : -1.0f,
                        (vid == 2) ? 3.0f : -1.0f);
    vsOutput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.texCoord = float2((pos.x + 1.0f) * 0.5f, (1.0f - pos.y) * 0.5f);
    return o;
}

// ── Hash / Noise functions (ported verbatim from FractalNoise.hlsl) ────────────────────────────

// from https://www.shadertoy.com/view/4djSRW
// [NAMED FORK: hash33-verbatim] — MOD3 and hash33 constants ported exactly from TiXL HLSL.
// Do NOT replace with hash.metal.h functions (different hash family).
#define MOD3 float3(0.1031f, 0.11369f, 0.13787f)

static float3 hash33(float3 p3) {
    p3 = fract(p3 * MOD3);
    p3 += dot(p3, p3.yxz + 19.19f);
    return -1.0f + 2.0f * fract(float3((p3.x + p3.y) * p3.z,
                                        (p3.x + p3.z) * p3.y,
                                        (p3.y + p3.z) * p3.x));
}

static float simplex_noise(float3 p) {
    const float K1 = 0.333333333f;
    const float K2 = 0.166666667f;

    float3 i  = floor(p + (p.x + p.y + p.z) * K1);
    float3 d0 = p - (i - (i.x + i.y + i.z) * K2);

    // thx nikita: https://www.shadertoy.com/view/XsX3zB
    // HLSL line 59: `float3 i1 = e * (1.0 - e.zxy, 1.0 - e.zxy, 1.0 - e.zxy);`
    // Comma-operator → evaluates to last expr. MSL: `e * (1.0f - e.zxy)`.
    float3 e  = step(float3(0.0f), d0 - d0.yzx);
    float3 i1 = e * (1.0f - e.zxy);
    float3 i2 = 1.0f - e.zxy * (1.0f - e);

    float3 d1 = d0 - (i1 - 1.0f * K2);
    float3 d2 = d0 - (i2 - 2.0f * K2);
    float3 d3 = d0 - (1.0f - 3.0f * K2);

    float4 h = max(0.6f - float4(dot(d0, d0), dot(d1, d1), dot(d2, d2), dot(d3, d3)), 0.0f);
    float4 n = h * h * h * h *
               float4(dot(d0, hash33(i)),
                      dot(d1, hash33(i + i1)),
                      dot(d2, hash33(i + i2)),
                      dot(d3, hash33(i + 1.0f)));

    return dot(float4(31.316f, 31.316f, 31.316f, 31.316f), n);
}

static float noise_sum_abs(float3 p) {
    float f = 0.0f;
    p = p * 1.0f;
    f += 1.0000f * abs(simplex_noise(p)); p = 2.0f * p;
    f += 0.5000f * abs(simplex_noise(p)); p = 2.0f * p;
    f += 0.2500f * abs(simplex_noise(p)); p = 2.0f * p;
    f += 0.1250f * abs(simplex_noise(p)); p = 2.0f * p;
    f += 0.0625f * abs(simplex_noise(p));
    return f;
}

// ── Fragment shader ────────────────────────────────────────────────────────────────────────────

fragment float4 fractalnoise_fs(vsOutput       in       [[stage_in]],
                                constant FractalNoise_Params& p   [[buffer(0)]],
                                constant FractalNoise_Resolution& res [[buffer(1)]]) {
    float aspectRatio = res.TargetWidth / res.TargetHeight;
    float2 uv = in.texCoord;
    uv -= 0.5f;
    uv *= p.Stretch * p.Scale;
    uv += p.Offset * float2(-1.0f / aspectRatio, 1.0f) * p.Scale * p.Stretch;
    uv.x *= aspectRatio;

    float3 pos = float3(uv, p.Phase / 10.0f);

    int steps = (int)clamp(p.Iterations + 0.5f, 1.1f, 5.1f);

    float f = 0.7f;
    float scaleFactor = 1.0f;
    for (int i = 0; i < steps; i++) {
        float f1 = noise_sum_abs(pos * scaleFactor + float3(12.4f, 3.0f, 0.0f) * (float)i);
        pos += f * float3(p.WarpOffsetXY, p.WarpOffsetZ);
        f *= sin(f1) / 2.0f + 0.5f;
        f += 0.2f;
    }
    f = 2.0f * f - 1.0f;

    float fBiased = ApplyGainAndBias_fn(f, p.GainAndBias);

    return mix(p.ColorA, p.ColorB, saturate(fBiased));
}
