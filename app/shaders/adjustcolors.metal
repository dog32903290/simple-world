// AdjustColors: TiXL-ported comprehensive color grading filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/AdjustColors.hlsl.
// Performs (in order): rgb clamp, HSB conversion, vignette, exposure, hue shift, saturation,
// prevent-clamping tone map, S-curve contrast, brightness, HSB back to RGB, colorize blend,
// background composite.
//
// Fork (named, DX11->Metal): HLSL `#define mod(x,y)` replaced by Metal's `fmod`. The
// HLSL `static float PI = 3.141578` is unused in the kernel (no explicit PI use). Vec4
// operations (clamp, lerp, rgb2hsb/hsb2rgb, colorize) are preserved verbatim. Sampler: fixed
// linear+clamp (AdjustColors.t3 host provides texSampler without explicit wrap; clamp is safe
// for full-frame filters).
#include <metal_stdlib>
#include "adjustcolors_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut adjustcolors_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of AdjustColors.hlsl hsb2rgb / rgb2hsb helpers.
static inline float3 hsb2rgb(float3 c) {
  float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
  float3 p = abs(fract(c.xxx + K.xyz) * 6.0f - K.www);
  return c.z < 0.5f
      ? c.z * 2.0f * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), c.y)
      : mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), mix(c.y, 0.0f, c.z * 2.0f - 1.0f));
}

static inline float3 rgb2hsb(float3 c) {
  float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
  float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10f;
  return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x * 0.5f);
}

// Mirror of AdjustColors.hlsl SCurve(value, amount, correction).
static inline float SCurve(float value, float amount, float correction) {
  float curve = (value < 0.5f)
      ? pow(value, amount) * pow(2.0f, amount) * 0.5f
      : 1.0f - pow(1.0f - value, amount) * pow(2.0f, amount) * 0.5f;
  return pow(curve, correction);
}

// Mirror of AdjustColors.hlsl psMain.
fragment float4 adjustcolors_fs(VSOut in [[stage_in]],
                                texture2d<float> inputTex      [[texture(0)]],
                                sampler samLinear               [[sampler(0)]],
                                constant AdjustColorsParams& P  [[buffer(ADJUSTCOLORS_Params)]]) {
  float2 uv = in.texCoord;
  float4 c = inputTex.sample(samLinear, uv);

  c.rgb = clamp(c.rgb, 0.000001f, 1000.0f);
  c.a = saturate(c.a);

  float3 hsb = rgb2hsb(c.rgb);

  // Vignette: radial darkening.
  float distFromCenter = length(float2(0.5f, 0.5f) - uv) * P.Vignette;
  hsb.z *= saturate(1.0f - distFromCenter);

  hsb.z *= P.Exposure;

  // Hue shift (degrees, wrapping via fmod).
  hsb.x = fmod(hsb.x + P.Hue / 360.0f + 1.0f, 1.0f);  // +1 before fmod avoids negative fmod

  // Saturation.
  hsb.y = saturate(hsb.y * P.Saturation);

  // Prevent clamping (tone mapping headroom).
  float power = 6.0f;
  float clampingBlendRange = clamp(P.PreventClampX, 0.001f, 1.0f);
  if (hsb.z * 2.0f > 1.0f - clampingBlendRange) {
    float pa = 1.0f - clampingBlendRange;
    float t2 = saturate((hsb.z * 2.0f - pa) / (P.PreventClampY - 1.0f + clampingBlendRange));
    t2 = 1.0f - pow(1.0f - t2, power);
    float2 P1B = float2(pa, pa);
    float2 P1A = float2(1.0f, 1.0f);
    float2 P2B = float2(1.0f + P.PreventClampY, 1.0f);
    float2 P3 = mix(mix(P1B, P1A, t2), mix(P1A, P2B, t2), t2);
    float xx = P3.y;
    float3 fixedHsb = rgb2hsb(float3(xx, xx, xx));
    hsb.z = fixedHsb.z;
  }

  // S-curve contrast (Contrast+1 as the exponent; correction=1).
  hsb.z = SCurve(saturate(hsb.z * 2.0f), P.Contrast + 1.0f, 1.0f) / 2.0f;

  // Brightness: > 0 lifts to white, < 0 crushes to black.
  hsb.z = (P.Brightness > 0.0f)
      ? mix(hsb.z, 1.0f, P.Brightness)
      : mix(0.0f, hsb.z, P.Brightness + 1.0f);
  hsb.z = clamp(hsb.z, 0.0f, 1.0f);

  c.rgb = hsb2rgb(hsb);

  // Colorize blend (only if Colorize.a > 0).
  float4 colorize = float4(P.ColorizeR, P.ColorizeG, P.ColorizeB, P.ColorizeA);
  if (colorize.a > 0.0f) {
    float t3 = (c.r + c.g + c.b) / 3.0f + 0.0001f;
    float3 colorizeHsb = rgb2hsb(colorize.rgb);
    float complementary = fmod(colorizeHsb.x + 0.5f, 1.0f);
    float3 darks = hsb2rgb(float3(complementary, colorizeHsb.y, P.OrangeTeal)).rgb;
    if (t3 < 0.5f) {
      float3 mapped = mix(darks, colorize.rgb, t3 * 2.0f);
      c.rgb = mix(c.rgb, mapped, colorize.a);
    } else {
      float3 mapped = mix(colorize.rgb, float3(1.0f, 1.0f, 1.0f), t3 * 2.0f - 1.0f);
      c.rgb = mix(c.rgb, mapped, colorize.a);
    }
  }

  // Background composite (alpha-over).
  float4 bg = float4(P.BackgroundR, P.BackgroundG, P.BackgroundB, P.BackgroundA);
  float a2 = clamp(bg.a + c.a - bg.a * c.a, 0.0f, 1.0f);
  float3 rgb2 = clamp((1.0f - c.a) * bg.rgb + c.a * c.rgb, 0.00001f, 100.0f);
  return float4(rgb2, a2);
}
