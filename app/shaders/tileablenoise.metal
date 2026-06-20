// TileableNoise image generator — TiXL port.
// Source: external/tixl/Operators/Lib/Assets/shaders/img/generate/PerlinNoise2d.hlsl
// Shader is referenced by TileableNoise.t3 via _ImageFxShaderSetupStatic:
//   "Source": "Lib:shaders/img/generate/PerlinNoise2d.hlsl"
//
// HLSL→MSL notes:
//   - frac()             → fract()   (same semantics)
//   - lerp()             → mix()     (same semantics)
//   - fmod()             → fmod() / can also use fract(a/b)*b, but MSL has fmod() directly
//   - saturate()         → saturate() (identical in MSL)
//   - normalize()        → normalize() (identical)
//   - dot()              → dot() (identical)
//   - floor()            → floor() (identical)
//   - [loop] for(...)    → standard for loop (MSL ignores HLSL [loop] attribute)
//   - ApplyGainAndBias (bias-functions.hlsl) inlined verbatim.
//
// NAMED FORKS:
//   [fork-bias-functions-inline]  ApplyGainAndBias/GetBias/GetSchlickBias inlined from
//     bias-functions.hlsl — no shared header for this, consistent with fractalnoise.metal.
//   [fork-no-sampler]   Pure generator — no image input, no sampler bound.
//   [fork-hash33-tileable]  hash33() uses frac/dot pattern from PerlinNoise2d.hlsl exactly;
//     constant 33.33 ported as 33.33f. Not merged into hash.metal.h.
//   [fork-int-buffer2]  HLSL b2 (IntParams) maps to MSL [[buffer(2)]]. Metal pipeline
//     requires host to bind all 3 constant buffers.

#include <metal_stdlib>
using namespace metal;

// ── ApplyGainAndBias (inlined from bias-functions.hlsl) ────────────────────────────────────────
// Scalar overload only — same as fractalnoise.metal.

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

// ── cbuffer structs ────────────────────────────────────────────────────────────────────────────
// Maps exactly to tileablenoise_params.h (shared host<->shader).

struct TileableNoise_FloatParams {
    float4 ColorA;       // b0[0..3]
    float4 ColorB;       // b0[4..7]
    float2 Offset;       // b0[8..9]
    float  Scale;        // b0[10]
    float  Phase;        // b0[11]   (= RandomPhase raw; used as p.z in shader)
    float2 GainAndBias;  // b0[12..13]
    float  Gain;         // b0[14]   (= per-octave amplitude decay; TiXL Gain)
    float  Lacunarity;   // b0[15]   (= frequency scale per octave; TiXL Lacunarity)
    float  Contrast;     // b0[16]   (= output contrast scalar)
    float  _pad0;        // b0[17]   (pad to 80 bytes = 5 × 16)
    float  _pad1;        // b0[18]
    float  _pad2;        // b0[19]
};

struct TileableNoise_Resolution {
    float TargetWidth;   // b1[0]
    float TargetHeight;  // b1[1]
    float _pad[2];       // pad 8 → 16 bytes
};

struct TileableNoise_IntParams {
    int Iterations;  // b2[0]  (= Octaves, clamped 1..10 by ClampInt in .t3)
    int Detail;      // b2[1]  (= Detail, tile repetition scale)
    int _pad[2];     // pad 8 → 16 bytes
};

// ── Vertex shader ──────────────────────────────────────────────────────────────────────────────

struct vsOutput {
    float4 position [[position]];
    float2 texCoord;
};

vertex vsOutput tileablenoise_vs(uint vid [[vertex_id]]) {
    float2 pos = float2((vid == 1) ? 3.0f : -1.0f,
                        (vid == 2) ? 3.0f : -1.0f);
    vsOutput o;
    o.position = float4(pos, 0.0f, 1.0f);
    o.texCoord = float2((pos.x + 1.0f) * 0.5f, (1.0f - pos.y) * 0.5f);
    return o;
}

// ── hash33 (from PerlinNoise2d.hlsl verbatim) ─────────────────────────────────────────────────
// [fork-hash33-tileable] — uses frac(p * 0.1031) + dot(p, p.yzx + 33.33) pattern.
// This is a DIFFERENT hash than hash.metal.h / FractalNoise.hlsl hash33 (which uses MOD3).
// Ported exactly — do not merge with hash.metal.h.

static float3 hash33_tileable(float3 p) {
    p = fract(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return fract((p.xxy + p.yzz) * p.zyx);
}

// ── Perlin gradient helper ─────────────────────────────────────────────────────────────────────

static float grad(float3 cell, float3 pos) {
    float3 g = normalize(hash33_tileable(cell) * 2.0f - 1.0f);
    return dot(g, pos);
}

// ── Quintic fade ──────────────────────────────────────────────────────────────────────────────

static float3 fade_tileable(float3 t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// ── perlinTileable: wrapping perlin noise (PerlinNoise2d.hlsl lines 64-101) ───────────────────

static float perlinTileable(float3 p, float3 period) {
    float3 i = floor(p);
    float3 f = fract(p);
    float3 w = fade_tileable(f);

    float3 c000 = fmod(i + float3(0, 0, 0), period);
    float3 c100 = fmod(i + float3(1, 0, 0), period);
    float3 c010 = fmod(i + float3(0, 1, 0), period);
    float3 c110 = fmod(i + float3(1, 1, 0), period);

    float3 c001 = fmod(i + float3(0, 0, 1), period);
    float3 c101 = fmod(i + float3(1, 0, 1), period);
    float3 c011 = fmod(i + float3(0, 1, 1), period);
    float3 c111 = fmod(i + float3(1, 1, 1), period);

    float n000 = grad(c000, f - float3(0, 0, 0));
    float n100 = grad(c100, f - float3(1, 0, 0));
    float n010 = grad(c010, f - float3(0, 1, 0));
    float n110 = grad(c110, f - float3(1, 1, 0));

    float n001 = grad(c001, f - float3(0, 0, 1));
    float n101 = grad(c101, f - float3(1, 0, 1));
    float n011 = grad(c011, f - float3(0, 1, 1));
    float n111 = grad(c111, f - float3(1, 1, 1));

    float nx00 = mix(n000, n100, w.x);
    float nx10 = mix(n010, n110, w.x);
    float nx01 = mix(n001, n101, w.x);
    float nx11 = mix(n011, n111, w.x);

    float nxy0 = mix(nx00, nx10, w.y);
    float nxy1 = mix(nx01, nx11, w.y);

    return mix(nxy0, nxy1, w.z);
}

// ── fbmPerlinTileable (PerlinNoise2d.hlsl lines 103-126) ──────────────────────────────────────

static float fbmPerlinTileable(float3 p, float3 basePeriod,
                               int Iterations, float Gain, float Lacunarity) {
    float sum  = 0.0f;
    float amp  = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;

    for (int i = 0; i < Iterations; i++) {
        // HLSL: float per = basePeriod * freq;
        // [NAMED FORK: hlsl-scalar-truncation] `per` is declared SCALAR float in HLSL, but
        //   `basePeriod` is float3. HLSL `float per = float3 * scalar` TRUNCATES the float3 to
        //   its .x component → per = basePeriod.x * freq (a scalar). When `per` is then passed to
        //   perlinTileable(p, float3 period), the scalar is SPLATTED to float3(per, per, per).
        //   Net: the z basePeriod (1024) is DISCARDED in TiXL — the actual period used is
        //   float3(Detail*freq, Detail*freq, Detail*freq). We replicate this truncation exactly
        //   for byte-parity with TiXL (an earlier port preserved the float3 → parity bug).
        float  perScalar = basePeriod.x * freq;       // HLSL float3→float truncation (.x)
        float3 per       = float3(perScalar);         // scalar→float3 param splat
        sum  += amp * perlinTileable(p * freq, per);
        norm += amp;
        p    += 0.77f * (float)i;    // HLSL: p += 0.77 * i (note: i is loop var, float cast)
        freq *= Lacunarity;
        amp  *= Gain;
    }

    return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

// ── Fragment shader ────────────────────────────────────────────────────────────────────────────

fragment float4 tileablenoise_fs(
    vsOutput                             in    [[stage_in]],
    constant TileableNoise_FloatParams&  fp    [[buffer(0)]],
    constant TileableNoise_Resolution&   res   [[buffer(1)]],
    constant TileableNoise_IntParams&    ip    [[buffer(2)]])
{
    // HLSL psMain: float2 uv = psInput.texCoord * Detail
    float2 uv = in.texCoord * (float)ip.Detail;

    // float3 p = float3(uv * (Scale) + (Offset + 666), Phase);
    float3 p = float3(uv * fp.Scale + (fp.Offset + 666.0f), fp.Phase);

    // HLSL: float3 basePeriod = float3(Detail.xx, 1024);
    // "Detail.xx" swizzles scalar Detail into float2; float3(float2, float) packs all three.
    float d = (float)ip.Detail;
    float3 basePeriod = float3(d, d, 1024.0f);

    float noise = fbmPerlinTileable(p, basePeriod, ip.Iterations, fp.Gain, fp.Lacunarity);

    // HLSL: noise = noise * 0.5 * Contrast + 0.5
    noise = noise * 0.5f * fp.Contrast + 0.5f;
    float f = noise;

    float fBiased = ApplyGainAndBias_fn(f, fp.GainAndBias);

    return mix(fp.ColorA, fp.ColorB, saturate(fBiased));
}
