// bloom.metal — the five Bloom pixel passes + a fullscreen-triangle VS, ported 1:1 from TiXL
// Operators/Lib/Assets/shaders/img/blur/Bloom-{BrightpassPS,DownsamplePS,SeparableBlurPS,CopyPS,
// UpsamplePS}.hlsl. Driven by point_ops_bloom.cpp (the multi-pass-executor seam), which issues
// 4N+2 render passes over per-level cached scratch targets (1 Brightpass + N×{Downsample,BlurV,BlurH}
// + 1 Copy + N×Upsample-add). Cbuffers packed byte-for-byte to the HLSL register(b0) layouts; see
// bloom_params.h. Fork (named): one .metal carries all passes (vs TiXL's five shader files) — same
// algorithm, fewer files; the executor selects the fragment fn per pass.
#include <metal_stdlib>
using namespace metal;

#include "bloom_params.h"  // BloomThresholdParams, BloomBlurParams, BloomCompositeParams, bindings

struct VSOut {
  float4 position [[position]];
  float2 uv;
};

// Fullscreen triangle from vertex_id (Bloom-FullscreenVS.hlsl: uv (0,0),(2,0),(0,2); clip maps to
// the full screen). UV y is NOT flipped here — Metal's framebuffer origin matches the HLSL mapping
// used across the other image filters (blur.metal uses the same construction).
vertex VSOut bloom_vs(uint vid [[vertex_id]]) {
  VSOut o;
  o.uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  return o;
}

// 1. Brightpass (Bloom-BrightpassPS.hlsl): clamp [0,1000], luminance = dot(rgb, ColorWeights),
//    contribution = saturate(luminance - Threshold), NaN -> 0; output rgb*contribution, keep alpha.
fragment float4 bloom_bright_fs(VSOut in [[stage_in]],
                                texture2d<float> src [[texture(0)]],
                                sampler samLinear     [[sampler(0)]],
                                constant BloomThresholdParams& P [[buffer(BLOOM_ThresholdParams)]]) {
  float4 color = src.sample(samLinear, in.uv);
  color = clamp(color, float4(0.0f), float4(1000.0f, 1000.0f, 1000.0f, 1.0f));
  float3 weights = float3(P.ColorWeights);
  float brightness = dot(color.rgb, weights);
  float contribution = saturate(brightness - P.Threshold);
  if (isnan(contribution)) {
    contribution = 0.0f;
    color = float4(0.0f);
  }
  return float4(color.rgb * contribution, color.a);
}

// 2a. Downsample (Bloom-DownsamplePS.hlsl): explicit 2x2 box, ±0.5-texel 4-tap via GetDimensions.
fragment float4 bloom_down_fs(VSOut in [[stage_in]],
                              texture2d<float> src [[texture(0)]],
                              sampler samLinear     [[sampler(0)]]) {
  float w = (float)src.get_width();
  float h = (float)src.get_height();
  float2 texelSize = float2(1.0f / w, 1.0f / h);
  float2 uv00 = in.uv + texelSize * float2(-0.5f, -0.5f);
  float2 uv10 = in.uv + texelSize * float2(0.5f, -0.5f);
  float2 uv01 = in.uv + texelSize * float2(-0.5f, 0.5f);
  float2 uv11 = in.uv + texelSize * float2(0.5f, 0.5f);
  float4 color = float4(0.0f);
  color += src.sample(samLinear, uv00);
  color += src.sample(samLinear, uv10);
  color += src.sample(samLinear, uv01);
  color += src.sample(samLinear, uv11);
  return color * 0.25f;
}

// 2b. Separable blur (Bloom-SeparableBlurPS.hlsl): 5-sample linear approx of a 9-tap Gaussian,
//     offsets {0,1.3846,3.2307} weights {0.2270,0.3162,0.0703}, optional saturate clamp.
fragment float4 bloom_blur_fs(VSOut in [[stage_in]],
                              texture2d<float> src [[texture(0)]],
                              sampler samLinear     [[sampler(0)]],
                              constant BloomBlurParams& P [[buffer(BLOOM_BlurParams)]]) {
  const float O[3] = {0.0f, 1.3846153846f, 3.2307692308f};
  const float W[3] = {0.2270270270f, 0.3162162162f, 0.0702702703f};
  float2 texelSize = float2(1.0f / P.Width, 1.0f / P.Height);
  float2 blurVec = float2(P.DirX, P.DirY) * texelSize;
  float4 c = src.sample(samLinear, in.uv) * W[0];
  c += src.sample(samLinear, in.uv + blurVec * O[1]) * W[1];
  c += src.sample(samLinear, in.uv - blurVec * O[1]) * W[1];
  c += src.sample(samLinear, in.uv + blurVec * O[2]) * W[2];
  c += src.sample(samLinear, in.uv - blurVec * O[2]) * W[2];
  if (P.ClampTexture > 0) c = saturate(c);
  return c;
}

// 3. Copy base (Bloom-CopyPS.hlsl): point-sample passthrough (source -> composite).
fragment float4 bloom_copy_fs(VSOut in [[stage_in]],
                              texture2d<float> src [[texture(0)]],
                              sampler samPoint      [[sampler(0)]]) {
  return src.sample(samPoint, in.uv);
}

// 4. Upsample-add (Bloom-UpsamplePS.hlsl): bilinear lift of a low-res level, * PassIntensity *
//    PassColor; the executor turns ON additive blend (One,One) for these passes.
fragment float4 bloom_upsample_fs(VSOut in [[stage_in]],
                                  texture2d<float> lowRes [[texture(0)]],
                                  sampler samLinear        [[sampler(0)]],
                                  constant BloomCompositeParams& P
                                      [[buffer(BLOOM_CompositeParams)]]) {
  float4 color = lowRes.sample(samLinear, in.uv, level(0));
  return color * P.PassIntensity * float4(P.PassColor);
}
