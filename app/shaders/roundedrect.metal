// RoundedRect: TiXL-ported rounded-rectangle generator, single pass.
// Faithful line-by-line port of:
//   external/tixl Operators/Lib/Assets/shaders/img/generate/RoundedRect.hlsl
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-sampler-repeat]  Sampler for upstream ImageA is linear+Repeat, matching TiXL
//   _ImageFxShaderSetupStatic.t3 defaults (AddressU/V=Wrap, Filter=MinMagMipLinear).
// [fork-IsTextureValid]  _ImageFxShaderSetupStatic injects IsTextureValid into cbuffer at
//   runtime. Host sets 1.0 if Image wired, else 0.0. Replicates framework behaviour.
// [fork-cbuffer-binding]  HLSL b1 Resolution(TargetWidth/TargetHeight) is framework-injected;
//   bound at Metal fragment cbuffer index RRECT_Resolution (=1).
// [fork-generator-dummy]  When no Image wired, host binds a 1×1 transparent-black dummy texture
//   so ImageA always has a valid texture2d handle (same as NGon/SinForm generator convention).
// [fork-sdBox-inline]  sdBox is a local helper function ported verbatim from RoundedRect.hlsl
//   lines 35-41. No additional dependencies — uses only float2 arithmetic.
// [fork-GradientBias-branch]  TiXL default FeatherBias = -0.001 (negative branch).
//   pow(d, GradientBias+1) for >= 0; 1-pow(clamp(1-d,0,10), -GradientBias+1) for < 0.
//   Ported verbatim (lines 69-71 of HLSL).
#include <metal_stdlib>
#include "roundedrect_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as ngon_vs / sinform_vs / rings_vs.
vertex VSOut roundedrect_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);       // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// sdBox — verbatim port of RoundedRect.hlsl lines 35-41.
// Signed distance field of an axis-aligned box centred at origin, half-extents b.
// Positive = outside, negative = inside.
static inline float sdBox(float2 p, float2 b) {
  float2 d = abs(p) - b;
  return length(max(d, float2(0.0f, 0.0f))) + min(max(d.x, d.y), 0.0f);
}

// File-scope PI constant (matches RoundedRect.hlsl literal "3.141578").
constant float RRECT_PI = 3.141578f;

// psMain — verbatim port of RoundedRect.hlsl lines 44-92.
// [fork-cbuffer-binding] Resolution at buffer index RRECT_Resolution (=1).
fragment float4 roundedrect_fs(VSOut input                        [[stage_in]],
                               texture2d<float> ImageA             [[texture(0)]],
                               sampler texSampler                  [[sampler(0)]],
                               constant RoundedRectParams& P       [[buffer(RRECT_Params)]],
                               constant RoundedRectResolution& Res [[buffer(RRECT_Resolution)]]) {
  float aspectRatio = Res.TargetWidth / Res.TargetHeight;

  float2 p = input.texCoord;
  p -= 0.5f;
  p.x *= aspectRatio;

  // Rotate (RoundedRect.hlsl lines 52-61)
  float imageRotationRad = (-P.Rotate - 90.0f) / 180.0f * RRECT_PI;
  float sina = sin(-imageRotationRad - RRECT_PI / 2.0f);
  float cosa = cos(-imageRotationRad - RRECT_PI / 2.0f);

  // Center subtraction: p -= Center * float2(1,-1)  (RoundedRect.hlsl line 58)
  p -= float2(P.CenterX, P.CenterY) * float2(1.0f, -1.0f);
  p = float2(cosa * p.x - sina * p.y,
             cosa * p.y + sina * p.x);

  // Shape geometry (lines 63-68)
  float2 size = float2(P.StretchX, P.StretchY) * P.Scale;
  float minSize = min(size.x, size.y);
  float roundOffset = minSize * P.Round;
  float2 rsize = size - roundOffset;

  float d = sdBox(p, rsize / 2.0f);

  // GradientBias / FeatherBias ramp (lines 69-71). [fork-GradientBias-branch]
  d = (P.GradientBias >= 0.0f)
      ? pow(d, P.GradientBias + 1.0f)
      : 1.0f - pow(clamp(1.0f - d, 0.0f, 10.0f), -P.GradientBias + 1.0f);

  // Feather / smoothstep (lines 73-77)
  float feather = P.Scale * P.Feather / 2.0f;
  float dInside = smoothstep(-feather, feather, d - roundOffset / 2.0f);

  float stroke = max(P.Stroke * minSize, 0.0f);
  float dStroke = smoothstep(-feather, feather, d - roundOffset / 2.0f - stroke);

  // Stroke / outline composite (lines 79-86)
  float showStroke = saturate(abs(stroke) * 100.0f);
  float4 fill       = float4(P.FillR,    P.FillG,    P.FillB,    P.FillA);
  float4 outlineColor = float4(P.OutlineR, P.OutlineG, P.OutlineB, P.OutlineA);
  float4 background = float4(P.BgR,     P.BgG,     P.BgB,     P.BgA);
  outlineColor = mix(fill, outlineColor, showStroke);

  float4 cInside = mix(fill, outlineColor, dInside);

  float4 cStroke = mix(background, outlineColor, 1.0f - dStroke);
  float4 c = mix(cInside, cStroke, dStroke);

  // IsTextureValid composite (lines 88-91). [fork-IsTextureValid]
  float4 orgColor = ImageA.sample(texSampler, input.texCoord);
  return (P.IsTextureValid < 0.5f)
      ? c
      : float4((1.0f - c.a) * orgColor.rgb + c.a * c.rgb,
               orgColor.a + c.a - orgColor.a * c.a);
}
