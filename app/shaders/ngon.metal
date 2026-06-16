// NGon: TiXL-ported N-sided polygon generator, single pass.
// Faithful line-by-line port of external/tixl Operators/Lib/Assets/shaders/img/generate/NGon.hlsl.
// Generates an N-gon shape (triangle/hexagon/etc.) with optional blend over an upstream image.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-mod-floor]  NGon.hlsl defines `float mod(float x, float y) { return (x - y*floor(x/y)); }`
//   (lines 38-41) and calls it inside sdNgon (line 52). MSL `%` is fmod (truncation), not floor-mod.
//   → sw_mod(x,y) = x - y*floor(x/y) for all mod() call sites (Cut 61 discipline).
// [fork-blend-inline]  BlendColors inlined verbatim from shared/blend-functions.hlsl (same as
//   rings.metal / starglowstreaks.metal / dither.metal). Default BlendMode=0 (normal blend).
// [fork-sampler-repeat]  Sampler for upstream ImageA is linear+Repeat, matching TiXL
//   _ImageFxShaderSetupStatic.t3 defaults: AddressU/V=Wrap (DX TextureAddressMode.Wrap).
// [fork-IsTextureValid]  _ImageFxShaderSetupStatic injects IsTextureValid into cbuffer at runtime.
//   Host sets IsTextureValid=1.0 if Image wired, else 0.0. Replicates framework behaviour.
// [fork-cbuffer-binding]  HLSL b1 Resolution(TargetWidth/TargetHeight) is framework-injected;
//   bound at Metal fragment cbuffer index 1 (same pattern as Rings/VoronoiCells/SinForm).
// [fork-generator-dummy]  When no Image wired, host binds a 1×1 transparent-black dummy texture
//   so ImageA always has a valid texture2d handle (same as SinForm/Rings generator convention).
#include <metal_stdlib>
#include "ngon_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as rings_vs / voronoicells_vs / sinform_vs.
vertex VSOut ngon_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);       // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// sw_mod: floor-based modulo — mirrors NGon.hlsl `float mod(float x, float y)` (lines 38-41).
// Used where NGon.hlsl calls mod() inside sdNgon (line 52). [fork-mod-floor]
static inline float sw_mod(float x, float y) { return x - y * floor(x / y); }

// BlendColors — verbatim port of external/tixl Operators/Lib/Assets/shaders/shared/blend-functions.hlsl.
// [fork-blend-inline] Same inline as rings.metal / starglowstreaks.metal / dither.metal.
static inline float4 BlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);
  float a = tA.a + tB.a - tA.a * tB.a;
  float normalRatio = saturate(tB.a * 2.0f - 1.0f);  // declared in .hlsl (unused below); parity
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);
  switch (blendMode) {
    case 0:  rgb = rgbNormalBlended; break;
    case 1:  rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a); break;
    case 2:  rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a); break;
    case 3:
      rgb = float3(
        tA.r < 0.5f ? (2.0f*tA.r*tB.r) : (1.0f - 2.0f*(1.0f-tA.r)*(1.0f-tB.r)),
        tA.g < 0.5f ? (2.0f*tA.g*tB.g) : (1.0f - 2.0f*(1.0f-tA.g)*(1.0f-tB.g)),
        tA.b < 0.5f ? (2.0f*tA.b*tB.b) : (1.0f - 2.0f*(1.0f-tA.b)*(1.0f-tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a); break;
    case 4:  rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a); break;
    case 5:  rgb = tA.rgb; break;
    case 6:  rgb = tB.rgb; break;
    case 7:  rgb = tA.rgb / (1.0001f - saturate(tB.rgb)); break;
    case 8:  rgb = tA.rgb + tB.rgb; break;
    case 9:  a = tA.a * tB.a; break;
  }
  return float4(rgb, a);
}

// File-scope constants (MSL: 'constant' only valid at file scope, not inside functions).
constant float NGON_TAU = 3.1415926535f * 2.0f;
constant float NGON_PI  = 3.141578f;  // matches NGon.hlsl's literal "3.141578" (verbatim)

// sdNgon — verbatim port of NGon.hlsl lines 44-63.
// Signed distance field of an N-gon. Positive = outside, negative = inside.
// [fork-mod-floor] mod() calls → sw_mod().
static inline float sdNgon(float2 p, float r, float n,
                            float Blades, float Curvature) {
  float inv_n = 1.0f / n;

  // Radial repeat into polar coords
  float2 rp = float2(atan2(p.y, p.x), length(p));
  rp.x /= NGON_TAU;
  // [fork-mod-floor] mod() call (NGon.hlsl line 52) → sw_mod
  rp.x = sw_mod(rp.x + inv_n * 0.5f, inv_n) - 0.5f * inv_n;
  rp.x *= rp.x > 0.0f ? (1.0f - saturate(Blades)) : 1.0f;
  rp.y = saturate(mix(rp.y, r, Curvature));
  rp.x *= NGON_TAU;

  p = float2(cos(rp.x), sin(rp.x)) * rp.y;  // back to cartesian
  float2 b = float2(r, r);
  b.y = b.x * tan(NGON_TAU * inv_n * 0.5f);
  float2 d = abs(p) - b;

  float sd = length(max(d, float2(0.0f, 0.0f))) + min(d.x, 0.0f);
  return sd;
}

// psMain — verbatim port of NGon.hlsl lines 66-96.
// [fork-cbuffer-binding] Resolution at buffer index NGON_Resolution (=1).
fragment float4 ngon_fs(VSOut input                    [[stage_in]],
                        texture2d<float> ImageA         [[texture(0)]],
                        sampler texSampler              [[sampler(0)]],
                        constant NGonParams& P          [[buffer(NGON_Params)]],
                        constant NGonResolution& Res    [[buffer(NGON_Resolution)]]) {
  float aspectRatio = Res.TargetWidth / Res.TargetHeight;
  float2 p = input.texCoord;
  p -= 0.5f;
  p.x *= aspectRatio;

  // Rotate (NGon.hlsl lines 74-82)
  float imageRotationRad = (-P.Rotate - 90.0f) / 180.0f * NGON_PI;
  float sina = sin(-imageRotationRad - NGON_PI / 2.0f);
  float cosa = cos(-imageRotationRad - NGON_PI / 2.0f);
  p = float2(cosa * p.x - sina * p.y,
             cosa * p.y + sina * p.x);

  // NGon.hlsl line 83: p += Position.yx * float2(1, 1)
  // Position.yx = (P.PositionY, P.PositionX) — note .yx swap (verbatim).
  p += float2(P.PositionY, P.PositionX);

  float d = sdNgon(p, P.Radius, P.Sides, P.Blades, P.Curvature);
  d = smoothstep(P.Round / 2.0f - P.Feather / 4.0f,
                 P.Round / 2.0f + P.Feather / 4.0f, d);

  // GradientBias (NGon.hlsl lines 88-90: FeatherBias in .cs → GradientBias in .hlsl)
  float dBiased;
  if (P.GradientBias >= 0.0f) {
    dBiased = pow(d, P.GradientBias + 1.0f);
  } else {
    dBiased = 1.0f - pow(clamp(1.0f - d, 0.0f, 10.0f), -P.GradientBias + 1.0f);
  }

  float4 fill       = float4(P.FillR, P.FillG, P.FillB, P.FillA);
  float4 background = float4(P.BgR,   P.BgG,   P.BgB,   P.BgA);

  float4 c = mix(fill, background, dBiased);
  float4 orgColor = ImageA.sample(texSampler, input.texCoord);
  float a = clamp(orgColor.a + c.a - orgColor.a * c.a, 0.0f, 1.0f);

  // [fork-IsTextureValid] When no Image wired, return c directly (IsTextureValid < 0.5).
  return (P.IsTextureValid < 0.5f) ? c : BlendColors(orgColor, c, (int)P.BlendMode);
}
