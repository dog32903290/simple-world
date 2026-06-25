// HoneyCombTiles: TiXL-ported hexagonal-tile stylize filter (lane stylize). Faithful 1:1 port of
// external/tixl Operators/Lib/Assets/shaders/img/fx/HexGridDisplace.hlsl psMain(). A fullscreen
// triangle tiles ImageA into a hex grid; each hex cell samples ImageA at the cell center, remaps the
// cell luminance through the Effects curve LUT (t1), and fills via lerp(Background, Fill, c). Based on
// "ShaderToy Tutorial - Hexagonal Tiling" by Martijn Steinrucken (BigWings) — preserved verbatim.
//
// Forks (named, DX11->Metal):
//   (1) Sampler s0 = the WrapMode-driven SamplerState (_multiImageFxSetupStatic sampler MultiInput[0]).
//       HoneyCombTiles does NOT wire WrapMode -> default "Wrap" => D3D11 Wrap == Metal Repeat address;
//       Filter default "MinMagPointMipLinear" => min/mag Linear. Bound in the op (cookHoneyCombTiles).
//       Both ImageA and Effects sample through this single s0 (the .hlsl uses one texSampler).
//   (2) HLSL atan2(y,x) arg order: the .hlsl writes atan2(gv.x, gv.y) (note: x,y order) — preserved
//       verbatim as atan2(gv.x, gv.y); the hc.x channel is unused downstream so it is harmless either
//       way, but kept byte-faithful.
//   (3) The Effects LUT (t1) is a 2-row R32_Float texture baked in-cook from the two embedded curves.
//       FAITHFUL: the .hlsl samples Effects through the SAME single texSampler (s0 = Wrap/Linear) it
//       uses for ImageA — there is exactly one `sampler texSampler : register(s0)`. So the LUT v
//       coordinate (0 / 0.75) and the U coordinate (`value` = length(rgb), can exceed 1) BOTH use
//       Repeat addressing. We replicate with the single texSampler bound at sampler(0). (No separate
//       clamp sampler — that would diverge from TiXL whenever value>1 or v wraps the 2-row LUT.)
#include <metal_stdlib>
#include "honeycombtiles_params.h"   // HoneyCombTilesParams, HONEYCOMBTILES_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as displace_vs / distortandshade_vs.
vertex VSOut honeycombtiles_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL mod(x,y) = x - y*floor(x/y) (NOT Metal fmod, which truncates toward zero). The .hlsl declares
// both a scalar and a float2 overload; both kept verbatim for parity (only hcmod2 is used below — the
// scalar is parity-kept, same convention as the other ported fx shaders' unused .hlsl helpers).
static inline float hcmod(float x, float y) { return x - y * floor(x / y); }
static inline float2 hcmod2(float2 x, float2 y) { return x - y * floor(x / y); }

// = HexDist (HexGridDisplace.hlsl).
static inline float HexDist(float2 p) {
  p = abs(p);
  float c = dot(p, normalize(float2(1.0f, 1.73f)));
  c = max(c, p.x);
  return c;
}

// = HexCoords (HexGridDisplace.hlsl). Returns (x=atan2(gv.x,gv.y), y=.5-HexDist(gv), zw=cell id).
static inline float4 HexCoords(float2 uv) {
  float2 r = float2(1.0f, 1.73f);
  float2 h = r * 0.5f;
  float2 a = hcmod2(uv, r) - h;
  float2 b = hcmod2(uv - h, r) - h;
  float2 gv = dot(a, a) < dot(b, b) ? a : b;
  float x = atan2(gv.x, gv.y);
  float y = 0.5f - HexDist(gv);
  float2 id = uv - gv;
  return float4(x, y, id.x, id.y);
}

// Mirror of HexGridDisplace.hlsl psMain(). TargetWidth/TargetHeight are the render-target size — the
// .hlsl reads them from a SEPARATE cbuffer b1 (TimeConstants), framework-injected. We pass them as a
// float2 `texSize` at buffer(HONEYCOMBTILES_TexSize) (the op sets it from c.output dims) so the aspect
// math is byte-identical to TiXL.
fragment float4 honeycombtiles_fs(VSOut in [[stage_in]],
                                  texture2d<float> imageA [[texture(0)]],
                                  texture2d<float> effects [[texture(1)]],
                                  sampler texSampler       [[sampler(0)]],
                                  constant HoneyCombTilesParams& P [[buffer(HONEYCOMBTILES_Params)]],
                                  constant float2& texSize         [[buffer(HONEYCOMBTILES_TexSize)]]) {
  const float PI = 3.141578f;  // NB: the .hlsl uses 3.141578 (truncated), NOT M_PI — kept verbatim.

  float2 uv = in.texCoord;
  float aspectRatio = texSize.x / texSize.y;          // TargetWidth/TargetHeight
  float2 divisions = float2(P.Divisions * aspectRatio, P.Divisions);
  float2 p = in.texCoord;
  float2 cellOffset = float2(P.OffsetX, P.OffsetY) / P.Divisions;

  p -= 0.5f;
  p += cellOffset;

  // Rotate
  float imageRotationRad = (-P.Rotation - 90.0f) / 180.0f * PI;
  float sina = sin(-imageRotationRad - PI / 2.0f);
  float cosa = cos(-imageRotationRad - PI / 2.0f);

  p.x *= aspectRatio;
  p = float2(cosa * p.x - sina * p.y,
             cosa * p.y + sina * p.x);
  p.x /= aspectRatio;

  p *= divisions;

  float4 col = float4(0.0f, 0.0f, 0.0f, 0.0f);
  float4 hc = HexCoords(p);

  float2 pCel = (hc.zw + float2(P.OffsetX, P.OffsetY)) / divisions;
  float sina2 = sin(-(-imageRotationRad - PI / 2.0f));
  float cosa2 = cos(-(-imageRotationRad - PI / 2.0f));

  pCel.x *= aspectRatio;
  pCel = float2(cosa2 * pCel.x - sina2 * pCel.y,
                cosa2 * pCel.y + sina2 * pCel.x);
  pCel.x /= aspectRatio;
  pCel += 0.5f;

  float4 imgColorForCel = imageA.sample(texSampler, pCel);
  float value = length(imgColorForCel.rgb);

  // Effects LUT (t1, 2-row R32). float2(value, 0.75) -> row 1 edgeEffect; float2(value, 0) -> row 0.
  // R32_Float -> the value is in .r (HLSL Texture2D<float>.Sample returns a scalar in .x; Metal
  // sample() of an r-only texture also fills .r). Same texSampler (s0 = Wrap/Linear) as ImageA.
  float edgeEffect = effects.sample(texSampler, float2(value, 0.75f)).r;
  value = effects.sample(texSampler, float2(value, 0.0f)).r;

  float c = smoothstep(0.001f, P.LineThickness / 100.0f + edgeEffect, hc.y * value) * value;
  c = clamp(c, 0.0f, 1.0f);

  float4 background = float4(P.BackgroundR, P.BackgroundG, P.BackgroundB, P.BackgroundA);
  float4 fill = float4(P.FillR, P.FillG, P.FillB, P.FillA);
  col = mix(background, fill, c);

  float4 orgColorWithDisplacement = imageA.sample(texSampler, uv);
  col = mix(col, orgColorWithDisplacement, P.MixOriginal);
  return col;
}
