// Blur: TiXL-ported directional Gaussian blur, one pass (the op runs it twice: H then V).
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Blur.hlsl. A fullscreen
// triangle samples the input texture along `Direction`, weighted by a fixed 10-entry Gauss LUT
// (bilinearly indexed by sample fraction), symmetric on both sides of the center tap.
//
// Fork (named, DX11->Metal): the HLSL relies on the host clamping the sampler address mode from
// the Wrap input (TiXL TextureAddressMode); here we bind a fixed linear+clamp-to-edge sampler in
// the op (cookBlur). Wrap=MirrorOnce (TiXL default) ~= clamp at the texel center for blur, so the
// visual result matches for the default; non-default Wrap modes are a follow-up (named gap).
#include <metal_stdlib>
#include "blur_params.h"   // BlurParams, BLUR_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut blur_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of Blur.hlsl psMain: walk NumberOfSamples taps each side along `dir`, Gauss-weighted.
fragment float4 blur_fs(VSOut in [[stage_in]],
                        texture2d<float> inputTex [[texture(0)]],
                        sampler samLinear         [[sampler(0)]],
                        constant BlurParams& P    [[buffer(BLUR_Params)]]) {
  const int WEIGHT_COUNT = 10;
  const float Gauss[WEIGHT_COUNT] = {0.93f, 0.86f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f, 0.2f, 0.1f};

  float n = max(P.NumberOfSamples, 1.0f);
  float2 dir = float2(P.DirectionX, P.DirectionY);
  dir *= 0.01f * P.Size / n;
  dir.y *= P.WidthToHeight;

  float2 pos = dir;
  float4 c = inputTex.sample(samLinear, in.texCoord);
  float totalWeight = 1.0f;
  for (int i = 0; i < (int)n; ++i) {
    float index = (float)i * (WEIGHT_COUNT - 1) / n;
    int i0 = clamp((int)index, 0, WEIGHT_COUNT - 1);
    int i1 = clamp(i0 + 1, 0, WEIGHT_COUNT - 1);
    float weight = mix(Gauss[i0], Gauss[i1], fract(index));
    c += inputTex.sample(samLinear, in.texCoord + pos) * weight;
    c += inputTex.sample(samLinear, in.texCoord - pos) * weight;
    pos += dir;
    totalWeight += 2.0f * weight;
  }

  float4 o;
  o.rgb = c.rgb / totalWeight * P.Glow2 + P.Offset;
  o.a = clamp(c.a / totalWeight, 0.0f, 1.0f);
  return o;
}
