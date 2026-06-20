// Blend: TiXL-ported image composite (lane multi-image, image/use). Faithful 1:1 port of external/tixl
// Operators/Lib/Assets/shaders/img/fx/Blend.hlsl. A fullscreen triangle samples ImageA and ImageB
// (aspect-fit by ScaleMode), pre-multiplies each by its color, then composites RGB by ColorMode and
// alpha by AlphaMode. The third consumer of the multi-image seam (two graph-wired Texture2D inputs:
// ImageA @ texture(0), ImageB @ texture(1)).
//
// Forks (named, DX11->Metal):
//   (1) Sampler address mode: TiXL's _multiImageFxSetupStatic binds the host sampler with WrapMode
//       "Clamp" (Blend.t3 _multiImageFxSetupStatic.WrapMode = "Clamp"). Bound in the op (cookBlend) as
//       linear + ClampToEdge — exact parity with the .t3 (no fork on the wrap itself; named for the
//       record). The aspect-fit uvB can push past [0,1]; Clamp samples the edge (TiXL parity).
//   (2) HLSL Sample(s, uv) (implicit mip) -> Metal sample(s, uv) (implicit mip 0; no mipped inputs).
//   (3) GetDimensions(): HLSL reads (width,height) per texture; Metal uses get_width()/get_height().
//       The aspect math is ported branch-for-branch.
//   (4) GenerateMips host plumbing (Blend.cs GenerateMips bool) omitted (no mips) — same fork class as
//       Blur/Displace. The port covers RGB ColorMode 0..9 and AlphaMode 0..8 verbatim.
#include <metal_stdlib>
#include "blend_params.h"   // BlendParams, BLEND_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as displace_vs / distortandshade_vs / blur_vs.
vertex VSOut blend_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of Blend.hlsl psMain(): aspect-fit ImageB, pre-multiply both samples by their colors, clamp
// alpha, then the AlphaMode switch (a) and ColorMode switch (rgb). NormalForUpperHalf folds at the end.
fragment float4 blend_fs(VSOut in [[stage_in]],
                         texture2d<float> imageA [[texture(0)]],
                         texture2d<float> imageB [[texture(1)]],
                         sampler texSampler      [[sampler(0)]],
                         constant BlendParams& P [[buffer(BLEND_Params)]]) {
  float2 uv = in.texCoord;

  float imageAAspect = (float)imageA.get_width() / (float)imageA.get_height();
  float imageBAspect = (float)imageB.get_width() / (float)imageB.get_height();

  float aspectDifference = (imageAAspect - imageBAspect) * (P.ScaleMode > 1.5f ? 1.0f : -1.0f);

  float2 uvB = P.ScaleMode < 0.5f ? uv : (aspectDifference < 0.0f
      ? float2((uv.x - 0.5f) * imageAAspect / imageBAspect + 0.5f, uv.y)
      : float2(uv.x, (uv.y - 0.5f) * imageBAspect / imageAAspect + 0.5f));

  float4 imageAColor = float4(P.ImageAColorR, P.ImageAColorG, P.ImageAColorB, P.ImageAColorA);
  float4 imageBColor = float4(P.ImageBColorR, P.ImageBColorG, P.ImageBColorB, P.ImageBColorA);

  float4 tA = imageA.sample(texSampler, uv) * imageAColor;
  float4 tB = imageB.sample(texSampler, uvB) * imageBColor;
  tA.a = clamp(tA.a, 0.0f, 1.0f);
  tB.a = clamp(tB.a, 0.0f, 1.0f);

  float a = tA.a + tB.a - tA.a * tB.a;

  switch ((int)P.AlphaMode) {
    case 1: a = tA.a * tB.a; break;
    case 2: a = 1.0f; break;
    case 3: a = tA.a; break;
    case 4: a = tB.a; break;
    case 5: a = (tA.r + tA.g + tA.b) / 3.0f; break;
    case 6: a = (tB.r + tB.g + tB.b) / 3.0f; break;
    case 7: a = tA.a + tB.a; break;
    case 8: a = max(tA.a, tB.a); break;
  }

  float normalRatio = saturate(tB.a * 2.0f - 1.0f);

  if (P.UseNormalForUpperHalf > 0.5f)
    tB.a = saturate(tB.a * 2.0f);

  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);

  switch ((int)P.ColorMode) {
    // normal
    case 0:
      rgb = rgbNormalBlended;
      break;
    // screen
    case 1:
      rgb = tA.rgb + tB.rgb * tB.a;
      break;
    // multiply
    case 2:
      rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a);
      break;
    // overlay
    case 3:
      rgb = float3(
          tA.r < 0.5f ? (2.0f * tA.r * tB.r) : (1.0f - 2.0f * (1.0f - tA.r) * (1.0f - tB.r)),
          tA.g < 0.5f ? (2.0f * tA.g * tB.g) : (1.0f - 2.0f * (1.0f - tA.g) * (1.0f - tB.g)),
          tA.b < 0.5f ? (2.0f * tA.b * tB.b) : (1.0f - 2.0f * (1.0f - tA.b) * (1.0f - tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a);
      break;
    // difference
    case 4:
      rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a);
      break;
    // use a
    case 5:
      rgb = tA.rgb;
      break;
    // use b
    case 6:
      rgb = tB.rgb;
      break;
    // max
    case 7:
      rgb = max(tA.rgb, tB.rgb);
      break;
    // sub
    case 8:
      rgb = tA.rgb - tB.rgb;
      break;
    // mix using ImageB alpha
    case 9:
      rgb = mix(tA.rgb, tB.rgb, imageBColor.a);
      a = mix(tA.a, tB.a, imageBColor.a);
      break;
  }

  if (P.UseNormalForUpperHalf > 0.5f)
    rgb = mix(rgb, rgbNormalBlended, normalRatio);

  return float4(rgb, a);
}
