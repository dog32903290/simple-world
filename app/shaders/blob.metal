// Blob image generator/filter op.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/generate/Blob.hlsl.
//
// TiXL: generates a smooth radial blob (ellipse SDF with smoothstep feather + pow bias),
// optionally blended over an input image (orgColor). When no Image is wired (IsTextureValid<0.5),
// outputs the blob colour directly. When Image is wired, blends via BlendColors.
//
// HLSL->MSL port notes:
//   - fork[rotation-verbatim]: TiXL's rotation is a double-negation:
//       imageRotationRad = (-Rotate - 90) / 180 * 3.141578
//       sina = sin(-imageRotationRad - pi/2)
//       cosa = cos(-imageRotationRad - pi/2)
//     Algebraically this simplifies to sina=sin(Rotate*pi/180), cosa=cos(Rotate*pi/180),
//     but we keep the verbatim formula to stay byte-identical with TiXL.
//   - fork[blend-functions-inline]: TiXL #includes "shared/blend-functions.hlsl" which is
//     a DXGI-side shared include. We inline the BlendColors logic directly in this .metal
//     (no shared include mechanism for precompiled Metal; same approach as other ported ops).
//   - fork[clamp-10]: TiXL uses clamp(1-d, 0, 10) in the negative-GradientBias path. Kept verbatim.
//   - No mod() usage in this shader.
//   - sampler: TiXL _ImageFxShaderSetupStatic uses the default texSampler; we use linear+Wrap (Repeat)
//     matching the standard image-filter sampler (same as sinform.metal, tint.metal etc.).
#include <metal_stdlib>
#include "blob_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Full-screen triangle vertex shader (same as all other image-filter ops).
vertex VSOut blob_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 pos;
  if (vid == 0)      { pos = float2(-1.0, -1.0); o.texCoord = float2(0.0, 1.0); }
  else if (vid == 1) { pos = float2( 3.0, -1.0); o.texCoord = float2(2.0, 1.0); }
  else               { pos = float2(-1.0,  3.0); o.texCoord = float2(0.0, -1.0); }
  o.position = float4(pos, 0.0, 1.0);
  return o;
}

// Inline of TiXL shared/blend-functions.hlsl BlendColors(tA, tB, blendMode).
// tA = orgColor (the background Image), tB = c (the blob colour).
static float4 BlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);

  float a = tA.a + tB.a - tA.a * tB.a;
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f, 1.0f, 1.0f);

  switch (blendMode) {
    case 0:  // normal
      rgb = rgbNormalBlended;
      break;
    case 1:  // screen
      rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a);
      break;
    case 2:  // multiply
      rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a);
      break;
    case 3:  // overlay
      rgb = float3(
        tA.r < 0.5f ? (2.0f * tA.r * tB.r) : (1.0f - 2.0f * (1.0f - tA.r) * (1.0f - tB.r)),
        tA.g < 0.5f ? (2.0f * tA.g * tB.g) : (1.0f - 2.0f * (1.0f - tA.g) * (1.0f - tB.g)),
        tA.b < 0.5f ? (2.0f * tA.b * tB.b) : (1.0f - 2.0f * (1.0f - tA.b) * (1.0f - tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a);
      break;
    case 4:  // difference
      rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a);
      break;
    case 5:  // use a
      rgb = tA.rgb;
      break;
    case 6:  // use b
      rgb = tB.rgb;
      break;
    case 7:  // colorDodge
      rgb = tA.rgb / (1.0001f - saturate(tB.rgb));
      break;
    case 8:  // linearDodge
      rgb = tA.rgb + tB.rgb;
      break;
    case 9:  // multiply-alpha
      a = tA.a * tB.a;
      break;
    default:
      rgb = rgbNormalBlended;
      break;
  }
  return float4(rgb, a);
}

fragment float4 blob_fs(VSOut in [[stage_in]],
                        texture2d<float> ImageA [[texture(0)]],
                        sampler texSampler [[sampler(0)]],
                        constant BlobParams& p [[buffer(BLOB_Params)]],
                        constant BlobResolution& res [[buffer(BLOB_Resolution)]]) {
  float aspectRatio = res.TargetWidth / res.TargetHeight;

  float2 uv = in.texCoord;
  // p -= 0.5 (centre coordinates)
  uv -= 0.5f;
  uv.x *= aspectRatio;

  // Rotate (verbatim TiXL double-negation — see fork note above)
  float imageRotationRad = (-p.Rotate - 90.0f) / 180.0f * 3.141578f;
  float sina = sin(-imageRotationRad - 3.141578f / 2.0f);
  float cosa = cos(-imageRotationRad - 3.141578f / 2.0f);

  uv = float2(
    cosa * uv.x - sina * uv.y,
    cosa * uv.y + sina * uv.x
  );
  // Apply Stretch (divide) and Position offset
  uv /= float2(p.StretchX, p.StretchY);
  uv -= float2(p.PositionX, p.PositionY) * float2(1.0f, -1.0f);

  float d = length(uv);
  float f = p.Feather * p.Scale / 2.0f;

  d = smoothstep(p.Scale / 2.0f - f, p.Scale / 2.0f + f, d);

  // GradientBias: pow-based bias of the smoothstep result.
  float dBiased;
  if (p.GradientBias >= 0.0f) {
    dBiased = pow(d, p.GradientBias + 1.0f);
  } else {
    dBiased = 1.0f - pow(clamp(1.0f - d, 0.0f, 10.0f), -p.GradientBias + 1.0f);
  }

  float4 Fill       = float4(p.FillR,  p.FillG,  p.FillB,  p.FillA);
  float4 Background = float4(p.BgR,    p.BgG,    p.BgB,    p.BgA);

  float4 c = mix(Fill, Background, dBiased);
  float4 orgColor = ImageA.sample(texSampler, in.texCoord);

  // Verbatim from TiXL (dead variable in pure-generator path, kept faithful):
  float a = clamp(orgColor.a + c.a - orgColor.a * c.a, 0.0f, 1.0f);
  (void)a;

  // IsTextureValid < 0.5 → no wired Image → output blob colour directly.
  // IsTextureValid >= 0.5 → blend blob over the input image.
  return (p.IsTextureValid < 0.5f) ? c : BlendColors(orgColor, c, (int)p.BlendMode);
}
