// lightraysfx.metal — TiXL LightRaysFx, ported 1:1 from the `psMain` entrypoint of
// external/tixl Operators/Lib/Assets/shaders/img/fx/LightRayFx.hlsl. SINGLE pass (the .t3 wires the
// Output only from psMain; the shader's Pass2Refine is dead — see lightraysfx_params.h backward-trace
// note). Driven by point_ops_lightraysfx.cpp via the standard single-pass tex-op idiom (one fullscreen
// draw, cached PSO). Cbuffer packed byte-for-byte to LightRaysFxParams (lightraysfx_params.h).
#include <metal_stdlib>
using namespace metal;

#include "lightraysfx_params.h"  // LightRaysFxParams + bindings

struct VSOut {
  float4 position [[position]];
  float2 uv;
};

// Fullscreen triangle from vertex_id — identical construction to blur.metal/bloom.metal (Metal
// framebuffer origin matches the HLSL texCoord mapping the other image filters use).
vertex VSOut lightraysfx_vs(uint vid [[vertex_id]]) {
  VSOut o;
  o.uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  return o;
}

// psMain (LightRayFx.hlsl): radial light-ray accumulation from a source point along the line
// source→pixel, decaying per step. Ported line-for-line.
//   centerproof = Center*(1,-1) + 0.5        (Direction → screen-space light source)
//   orgColor    = Image(uv) * lerp(1, FxImage(uv), ApplyFx)
//   delta       = (uv - centerproof) * Length / NumSamples
//   loop i=1..N-1: uv -= delta;  colorSum += Image(uv)*FxImage(uv)*pow(1 - i/N, Decay)
//   out = clamp(orgColor + colorSum/N * RayColor * Amount, 0, (1000,1000,1000,1))
// HasFx (host): when no TextureFX input is wired, FxImage is treated as white(1,1,1,1) —
// byte-identical to TiXL's FirstValidTexture(TextureFX, white-pixel.png) default.
fragment float4 lightraysfx_fs(VSOut in [[stage_in]],
                               texture2d<float> Image   [[texture(0)]],
                               texture2d<float> FxImage [[texture(1)]],
                               sampler texSampler       [[sampler(0)]],
                               constant LightRaysFxParams& P [[buffer(LIGHTRAYSFX_Params)]]) {
  float2 centerproof = P.Center * float2(1.0f, -1.0f) + float2(0.5f, 0.5f);

  float2 uv = in.uv;

  // FxImage: real texture if wired (HasFx==1), else white (TiXL FirstValidTexture → white-pixel).
  float4 fx0 = (P.HasFx > 0.5f) ? FxImage.sample(texSampler, uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
  float4 orgColor = Image.sample(texSampler, uv) * mix(float4(1.0f), fx0, P.ApplyFx);

  float N = P.NumSamples;
  float2 delta = (uv - centerproof) * P.Length / N;
  float4 colorSum = float4(0.0f);

  for (int i = 1; i < (int)N; i++) {
    uv -= delta;
    float4 img = Image.sample(texSampler, uv);
    float4 fx = (P.HasFx > 0.5f) ? FxImage.sample(texSampler, uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    colorSum += img * fx * pow(1.0f - (float)i / N, P.Decay);
  }

  return clamp(orgColor + colorSum / N * P.RayColor * P.Amount,
               float4(0.0f), float4(1000.0f, 1000.0f, 1000.0f, 1.0f));
}
