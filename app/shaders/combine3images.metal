// Combine3Images: TiXL-ported channel-pack image filter (lane multi-image, image/use). Faithful 1:1
// port of external/tixl Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl (Thomas Helzle 2022).
// Three graph-wired images (ImageA @ t0, ImageB @ t1, ImageC @ t2) are each tinted by a color, then the
// new image's R/G/B each take ONE selected channel (R/G/B/Average/Brightness of any of the 3 tinted
// images, 15-way), and A takes a 5-way mode. The THIRD consumer of the multi-image seam.
//
// Forks (named, DX11->Metal):
//   (1) Sampler address mode: TiXL's _trippleImageFxSetup binds the host sampler with WrapMode default
//       "Wrap" (Combine3Images.t3 does not override it). The Metal equivalent of D3D11 WRAP is Repeat;
//       bound in the op (cookCombine3Images). NOT load-bearing here: all three images are sampled at the
//       SAME psInput.texCoord in [0,1] (no warp, no OOB), so the address mode never engages — kept
//       faithful anyway.
//   (2) HLSL Sample(s, uv) (implicit mip) -> Metal sample(s, uv) (implicit mip 0; no mipped inputs).
//   (3) HLSL `color[i] = v` writes color.r/g/b in the i=0/1/2 loop; alpha written separately. Replicated
//       verbatim with a local float3 rgb + the separate alpha switch.
#include <metal_stdlib>
#include "combine3images_params.h"   // Combine3ImagesParams, COMBINE3IMAGES_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as distortandshade_vs / displace_vs / blur_vs.
vertex VSOut combine3images_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of img-combine-3.hlsl psMain():
//   tA = ImageA.Sample * ImageAColor;  tB = ...; tC = ...;
//   alpha switch on AlphaMode (0..4);
//   for i in 0..2: 15-way switch on selects[i] -> color[i] = chosen channel/avg/brightness;
//   return float4(color.rgb, a).
// The 15-way switch is faithful: cases 0-4 read tA (R/G/B/Average/Brightness), 5-9 tB, 10-14 tC.
// Brightness = saturate(0.239*r + 0.686*g + 0.075*b) — TiXL's exact luma weights (note: NOT BT.709).
fragment float4 combine3images_fs(VSOut in [[stage_in]],
                                  texture2d<float> imageA [[texture(0)]],
                                  texture2d<float> imageB [[texture(1)]],
                                  texture2d<float> imageC [[texture(2)]],
                                  sampler texSampler      [[sampler(0)]],
                                  constant Combine3ImagesParams& P [[buffer(COMBINE3IMAGES_Params)]]) {
  float2 uv = in.texCoord;
  float4 colorA = float4(P.ImageAColorR, P.ImageAColorG, P.ImageAColorB, P.ImageAColorA);
  float4 colorB = float4(P.ImageBColorR, P.ImageBColorG, P.ImageBColorB, P.ImageBColorA);
  float4 colorC = float4(P.ImageCColorR, P.ImageCColorG, P.ImageCColorB, P.ImageCColorA);

  float4 tA = imageA.sample(texSampler, uv) * colorA;
  float4 tB = imageB.sample(texSampler, uv) * colorB;
  float4 tC = imageC.sample(texSampler, uv) * colorC;

  // Alpha (5-way), HLSL switch on (int)AlphaMode.
  float a = 0.0f;
  switch ((int)P.AlphaMode) {
    case 0: a = tA.a; break;
    case 1: a = tB.a; break;
    case 2: a = tC.a; break;
    case 3: a = 0.0f; break;
    case 4: a = 1.0f; break;
  }

  // RGB channel pack (15-way per output channel), HLSL int3 selects + the i=0..2 loop.
  int selects[3] = {(int)P.Select_R, (int)P.Select_G, (int)P.Select_B};
  float3 rgb = float3(0.0f);
  for (int i = 0; i < 3; ++i) {
    float v = 0.0f;
    switch (selects[i]) {
      case 0:  v = tA.r; break;
      case 1:  v = tA.g; break;
      case 2:  v = tA.b; break;
      case 3:  v = (tA.r + tA.g + tA.b) / 3.0f; break;
      case 4:  v = min(1.0f, max(0.0f, 0.239f * tA.r + 0.686f * tA.g + 0.075f * tA.b)); break;
      case 5:  v = tB.r; break;
      case 6:  v = tB.g; break;
      case 7:  v = tB.b; break;
      case 8:  v = (tB.r + tB.g + tB.b) / 3.0f; break;
      case 9:  v = min(1.0f, max(0.0f, 0.239f * tB.r + 0.686f * tB.g + 0.075f * tB.b)); break;
      case 10: v = tC.r; break;
      case 11: v = tC.g; break;
      case 12: v = tC.b; break;
      case 13: v = (tC.r + tC.g + tC.b) / 3.0f; break;
      case 14: v = min(1.0f, max(0.0f, 0.239f * tC.r + 0.686f * tC.g + 0.075f * tC.b)); break;
    }
    rgb[i] = v;
  }

  return float4(rgb, a);
}
