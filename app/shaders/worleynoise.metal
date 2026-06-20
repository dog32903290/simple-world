// WorleyNoise image generator/filter — TiXL port.
// Source: external/tixl/Operators/Lib/Assets/shaders/img/generate/WorleyNoise.hlsl
// Based on ShaderToy by jamelouis (https://www.shadertoy.com/view/3dXyRl),
// ported to TiXL by Newemka.
//
// HLSL→MSL notes:
//   - frac()     → fract()  (fractional part; identical semantics)
//   - lerp()     → mix()    (linear interpolation; identical semantics)
//   - mul() / *  → *        (same for scalar/vector in MSL)
//   - distance() → distance() (identical in MSL)
//   - max(), abs(), floor(), saturate(), sin(), clamp(), step(): identical in MSL
//   - uint2, uint arithmetic: identical in MSL (unsigned 32-bit wraps mod 2^32)
//   - float(0xffffffffU): valid in MSL (same as HLSL)
//   - UIF = 1.0 / float(0xffffffffU): ported verbatim
//   - fmod(x,y) helper (HLSL): inline helper ported; not needed for MSL (use built-in fmod)
//   - #define UIF: valid MSL preprocessor, identical semantics
//   - ApplyGainAndBias (bias-functions.hlsl) inlined verbatim (scalar overload only)
//
// Hash: hash22() with unsigned integer multiply+XOR chain ported verbatim.
// [NAMED FORK: hash22-verbatim] — constants (1597334673U, 3812015801U) ported exactly.
//   Not merged into hash.metal.h (different hash family, local to WorleyNoise).
//
// [NAMED FORK: no-texture-in-generator-path]
//   TiXL: when inputTexture is null → IsTextureValid=0.0 → shader returns the worley-only lerp.
//   We bind a 1×1 dummy texture (never sampled in generator path) and pass IsTextureValid=0.0.
//   The textureValue branch is present in the shader for future filter use, but does not affect
//   the generator selftest (IsTextureValid < 0.5 → early return before blend).
//
// [NAMED FORK: sampler-mirror]
//   TiXL .t3 sets Wrap=Mirror on _ImageFxShaderSetupStatic → TextureAddressMode.Mirror.
//   MSL: sampler(address::mirrored_repeat). Only relevant for filter (non-dummy) path.
//
// [NAMED FORK: fmod-builtin]
//   The HLSL defines `float fmod(float x, float y)` as a custom function. In MSL, fmod() is
//   a builtin with identical semantics (x - y*trunc(x/y) for signed). The custom HLSL fmod uses
//   floor() which matches MSL's fmod for positive inputs but differs for negatives. However,
//   WorleyNoise.hlsl's custom fmod() is defined but never called in psMain — the grid loop
//   uses integer floor() directly. The custom fmod is dead code; not ported.

#include <metal_stdlib>
using namespace metal;

#define UIF (1.0f / float(0xffffffffU))

// ── bias-functions.hlsl (scalar overload) inlined ─────────────────────────────────────────────
// Source: external/tixl/Operators/Lib/Assets/shaders/shared/bias-functions.hlsl
// HLSL→MSL: all float math identical. Only the scalar overloads are needed here.

static inline float GetBias_wn(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

static inline float GetSchlickBias_wn(float g, float x) {
    if (x < 0.5f) {
        x *= 2.0f;
        x = 0.5f * GetBias_wn(g, x);
    } else {
        x = 2.0f * x - 1.0f;
        x = 0.5f * GetBias_wn(1.0f - g, x) + 0.5f;
    }
    return x;
}

static inline float ApplyGainAndBias_wn(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) {
        value = GetBias_wn(b, value);
        value = GetSchlickBias_wn(g, value);
    } else {
        value = GetSchlickBias_wn(g, value);
        value = GetBias_wn(b, value);
    }
    return value;
}

// ── hash22 (ported verbatim from WorleyNoise.hlsl) ───────────────────────────────────────────
// Source: "Hash without Sine 2" https://www.shadertoy.com/view/XdGfRR
// [NAMED FORK: hash22-verbatim] constants 1597334673U, 3812015801U ported exactly.
static inline float2 hash22(float2 p, float Phase) {
    uint2 q = uint2(int2(p)) * uint2(1597334673U, 3812015801U);
    q = (q.x ^ q.y) * uint2(1597334673U, 3812015801U);
    return float2(q) * UIF + Phase;
}

// ── cbuffer structs ───────────────────────────────────────────────────────────────────────────

struct WorleyNoiseParams {
    float4 ColorA;          // default (1,1,1,1)
    float4 ColorB;          // default (0,0,0,1)
    float2 Offset;          // default (0,0)
    float2 Stretch;         // default (1,1)
    float  Scale;           // default 5.0
    float  Phase;           // default 5.0
    float2 Clamping;        // default (0,1)
    float2 GainAndBias;     // default (0.5,0.5)
    float  Method;          // default 0.0 (Worley_F1); IntToFloat in .t3
    float  Randomness;      // default 12.6
    float  FxTextureBlend;  // default 1.0 (TextureBlend)
    float  IsTextureValid;  // 0.0 = no input (generator path), 1.0 = filter path
    float  _pad[2];
};

struct WorleyNoiseResolution {
    float TargetWidth;
    float TargetHeight;
    float _pad[2];
};

// ── vertex shader ─────────────────────────────────────────────────────────────────────────────

struct VsOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VsOut worleynoise_vs(uint vid [[vertex_id]]) {
    // Fullscreen triangle — same pattern as fractalnoise.metal / rings.metal etc.
    float2 pos = float2((vid == 1) ? 3.0f : -1.0f,
                        (vid == 2) ? 3.0f : -1.0f);
    VsOut out;
    out.position = float4(pos, 0.0f, 1.0f);
    // texCoord in [0,1] top-left origin (matching HLSL SV_POSITION y-flip convention).
    out.texCoord = float2((pos.x + 1.0f) * 0.5f, (1.0f - pos.y) * 0.5f);
    return out;
}

// ── fragment shader ───────────────────────────────────────────────────────────────────────────

fragment float4 worleynoise_fs(
    VsOut                           in       [[stage_in]],
    texture2d<float>                inputTexture [[texture(0)]],
    sampler                         texSampler   [[sampler(0)]],
    constant WorleyNoiseParams&     p            [[buffer(0)]],
    constant WorleyNoiseResolution& res          [[buffer(1)]])
{
    // ── Ported verbatim from WorleyNoise.hlsl psMain ──────────────────────────────────────────
    float2 offset = p.Offset * float2(1.0f, -1.0f) * p.Scale * p.Stretch;

    float aspectRatio = res.TargetWidth / res.TargetHeight;
    float2 uv = in.texCoord;
    uv += 0.5f;
    uv *= p.Stretch * p.Scale;
    uv.x *= aspectRatio;

    // Method selectors (HLSL if-chain → int selectors)
    int wt  = 0;  // distance type: 0=Euclidean, 1=Manhattan, 2=Chebyshev
    int f2t = 0;  // output type: 0=F1, 1=F2-F1

    if      (p.Method < 1.0f) { wt = 0; f2t = 0; }
    else if (p.Method < 2.0f) { wt = 1; f2t = 0; }
    else if (p.Method < 3.0f) { wt = 2; f2t = 0; }
    else if (p.Method < 4.0f) { wt = 0; f2t = 1; }
    else if (p.Method < 5.0f) { wt = 1; f2t = 1; }
    else                       { wt = 2; f2t = 1; }

    float2 q = uv - offset;
    float f1 = 9e9f;
    float f2 = f1;
    float2 cellCenter = float2(0.0f);  // tracks nearest cell center (for texture sampleUV)

    for (int i = -1; i < 2; i++) {
        for (int j = -1; j < 2; j++) {
            float2 cell = floor(q) + float2(i, j);
            float2 h = hash22(cell, p.Phase);
            float2 g = cell + 0.5f + 0.5f * sin(h * p.Randomness);

            float d;
            if (wt == 0) {
                d = distance(g, q);
            } else if (wt == 2) {
                float xx = abs(q.x - g.x);
                float yy = abs(q.y - g.y);
                d = max(xx, yy);
            } else {
                float xx = abs(q.x - g.x);
                float yy = abs(q.y - g.y);
                d = xx + yy;
            }

            if (d < f2) { f2 = d; }
            if (d < f1) {
                f2 = f1;
                f1 = d;
                cellCenter = g;
            }
        }
    }

    float worleyValue = (f2t == 0) ? f1 : (f2 - f1);

    // Texture sampling (filter path: only when IsTextureValid >= 0.5).
    // sampleUV reconstruction mirrors WorleyNoise.hlsl lines 174-183 verbatim.
    float2 sampleUV = cellCenter + offset;
    sampleUV /= p.Stretch * p.Scale;
    sampleUV.x /= aspectRatio;
    sampleUV -= 0.5f;

    float4 textureValue = inputTexture.sample(texSampler, sampleUV);
    textureValue.rgb = textureValue.rgb * p.FxTextureBlend;

    worleyValue = ApplyGainAndBias_wn(worleyValue, p.GainAndBias);

    float4 worleyNoise = mix(p.ColorB, p.ColorA, clamp(worleyValue, p.Clamping.x, p.Clamping.y));

    float3 blended = worleyNoise.rgb * textureValue.rgb;

    // Generator path (IsTextureValid < 0.5): pure worley lerp — matches HLSL's early-return branch.
    // Filter path (IsTextureValid >= 0.5): multiply with input texture.
    return (p.IsTextureValid < 0.5f)
        ? mix(p.ColorB, p.ColorA, clamp(worleyValue, p.Clamping.x, p.Clamping.y))
        : float4(blended, worleyNoise.a);
}
