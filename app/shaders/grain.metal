// Grain: TiXL-ported animated colour-noise image filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/generate/Grain.hlsl.
// Per pixel: build an animated hash noise (grayscale<->colour by Color, contrast by Exponent,
// bias by Brightness), then add noise*Amount to the source rgb. A Scale>1 branch quantises the
// noise into pixel-aligned blocks (blocky film grain).
//
// Fork (named, DX11->Metal):
//   - HLSL `hash12` is not in shared/hash.metal.h; ported inline below (verbatim Dave Hoskins,
//     hash-functions.hlsl:20-25 — frac()->fract()). `hash42` reused from shared/hash.metal.h.
//   - HLSL b1 was the (commented-out) TimeConstants; the live shader takes Time from b0. The host
//     supplies Time (= Animate*time + RandomPhase) directly into GrainParams.Time. We bind a
//     Resolution cbuffer (b1) for the Scale>1 branch's per-pixel step.
//   - HLSL `%`/`mod` on floats -> MSL fmod via the same `mod` macro the HLSL declared.
//   - Sampler: fixed linear+clamp (TiXL host provides texSampler without an explicit wrap).
#include <metal_stdlib>
#include "shared/hash.metal.h"
#include "grain_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers viewport, texCoord 0..1.
vertex VSOut grain_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL frac()-domain mod (matches Grain.hlsl's `#define mod(x,y) (x - y*floor(x/y))`).
static inline float gmod(float x, float y) { return x - y * floor(x / y); }
static inline float2 gmod2(float2 x, float2 y) { return x - y * floor(x / y); }

// Verbatim port of hash-functions.hlsl hash12 (lines 20-25): float2 -> float.
static inline float hash12(float2 p) {
  float3 p3 = fract(float3(p.xyx) * 0.1031f);
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.x + p3.y) * p3.z);
}

// Mirror of Grain.hlsl GetNoiseFromRandom.
static inline float4 getNoiseFromRandom(float2 uv, constant GrainParams& P) {
  // Animation
  float pxHash = hash12(uv * 431.0f + 111.0f);
  float t = P.Time + pxHash;

  // Color noise: blend two per-frame hashes.
  float4 hash1 = hash42((uv * 431.0f + (float)(int)t));
  float4 hash2 = hash42((uv * 431.0f + (float)((int)t + 1)));
  float4 h = mix(hash1, hash2, gmod(t, 1.0f));

  float4 grayScale = (h.r + h.g + h.b) / 3.0f;
  float4 noise = (mix(grayScale, h, P.Color) - 0.5f) * 2.0f;

  // Signed contrast curve (preserve sign, raise magnitude to Exponent).
  noise = select(pow(noise, float4(P.Exponent)),
                 -pow(-noise, float4(P.Exponent)),
                 noise < 0.0f);

  noise += P.Brightness;
  return noise;
}

// Mirror of Grain.hlsl psMain.
fragment float4 grain_fs(VSOut in [[stage_in]],
                         texture2d<float> ImageA      [[texture(0)]],
                         sampler texSampler           [[sampler(0)]],
                         constant GrainParams& P      [[buffer(GRAIN_Params)]],
                         constant GrainResolution& R  [[buffer(GRAIN_Resolution)]]) {
  float2 uv = in.texCoord;
  float4 orgColor = ImageA.sample(texSampler, uv);

  if (P.Scale > 1.0f) {
    float2 pixelStep = float2(1.0f / R.TargetWidth, 1.0f / R.TargetHeight);
    float2 offset = P.Scale * pixelStep;
    float2 fraction = gmod2(uv, offset);
    float4 n1 = getNoiseFromRandom(uv - fraction + 0.001f * pixelStep, P);
    float4 n2 = getNoiseFromRandom(uv - fraction + 0.004f * pixelStep + offset, P);
    float4 noise = mix(n1, n2, 0.0f);  // HLSL lerp(n1, n2, 0) == n1 (verbatim)
    return float4(orgColor.rgb + noise.rgb * P.Amount, 1.0f);
  } else {
    float4 noise = getNoiseFromRandom(uv, P);
    return float4(orgColor.rgb + noise.rgb * P.Amount, 1.0f);
  }
}
