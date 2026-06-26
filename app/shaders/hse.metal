// HSE (Hue/Saturation/Exposure): TiXL-ported image color filter (lane multi-image, image/color).
// Faithful 1:1 port of external/tixl Operators/Lib/Assets/shaders/img/fx/HueShift.hlsl. A fullscreen
// triangle samples `Image` (t0) + `FxTexture` (t1) at the SAME UV, converts to HSB, scales brightness
// by Exposure, shifts hue by (Hue + FxTexture.g), scales saturation by Saturation, converts back.
// The SECOND input (FxTexture) modulates the hue PER-PIXEL via its .g channel — the Fx-modulation
// pattern (same 2-texture shape as Displace).
//
// Fork (named, DX11->Metal):
//   (1) Sampler: TiXL .t3 default WrapMode "Wrap" + TextureFilter "MinMagPointMipLinear". Both inputs
//       are sampled at the SAME in-[0,1] texCoord (psInput.texCoord) with NO warp/OOB and explicit
//       mip 0 -> wrap mode is NOT load-bearing (mirror of CMC's mirror-sampler note). We bind a
//       faithful Point min/mag + Repeat sampler in cookHse; mip filter is moot (single level).
//   (2) HLSL SampleLevel(s, uv, 0.0) -> Metal sample(s, uv, level(0)) (explicit mip 0).
//   Everything else (the rgb2hsb / hsb2rgb math, the mod() macro, the saturate/clamp guards) is
//   ported verbatim for byte-parity.
#include <metal_stdlib>
#include "hse_params.h"   // HseParams, HSE_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// HLSL: #define mod(x, y) (x - y * floor(x / y))  — verbatim (NOT metal's fmod, which truncates toward 0).
static inline float hmod(float x, float y) { return x - y * floor(x / y); }

// hsb2rgb (HueShift.hlsl) — verbatim.
static float3 hsb2rgb(float3 c) {
  float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
  float3 p = abs(fract(c.xxx + K.xyz) * 6.0f - K.www);
  return c.z < 0.5f
             ? c.z * 2.0f * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), c.y)
             : mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), mix(c.y, 0.0f, (c.z * 2.0f - 1.0f)));
}

// rgb2hsb (HueShift.hlsl) — verbatim. HLSL lerp(a,b,t) == metal mix(a,b,t); step(edge,x) identical.
static float3 rgb2hsb(float3 c) {
  float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
  float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));

  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10f;
  return float3(
      abs(q.z + (q.w - q.y) / (6.0f * d + e)),
      d / (q.x + e),
      q.x * 0.5f);
}

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (same as blur_vs).
vertex VSOut hse_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of HueShift.hlsl psMain.
fragment float4 hse_fs(VSOut in [[stage_in]],
                       texture2d<float> inputTexture   [[texture(0)]],
                       texture2d<float> inputFxTexture [[texture(1)]],
                       sampler texSampler              [[sampler(0)]],
                       constant HseParams& P           [[buffer(HSE_Params)]]) {
  float2 uv = in.texCoord;
  float4 c = inputTexture.sample(texSampler, uv, level(0));
  float4 fx = inputFxTexture.sample(texSampler, uv, level(0));

  // float a = saturate(c.a);  // HLSL computes but never uses `a`; kept as a comment for parity.
  c.rgb = clamp(c.rgb, 0.000001f, 1000.0f);
  c.a = saturate(c.a);

  float3 hsb = rgb2hsb(c.rgb);

  // Exposure
  hsb.z *= P.Exposure;

  // Shift Hue (FxTexture.g modulates the shift per-pixel)
  float hueShift = P.Hue + fx.g;
  hsb.x = hmod((hsb.x + hueShift / 1.0f), 1.0f);

  // Adjust saturation
  hsb.y = saturate(hsb.y * P.Saturation);

  c.rgb = hsb2rgb(hsb);

  return c;
}
