// BlendWithMask: TiXL-ported mask-driven crossfade (lane multi-image, image/use). Faithful 1:1 port of
// external/tixl Operators/Lib/Assets/shaders/img/fx/BlendWithMask.hlsl. A fullscreen triangle samples
// ImageA, aspect-fits + samples ImageB, samples Mask, then returns lerp(tA, tB, mask.r): the FIRST op
// with THREE graph-wired Texture2D inputs (ImageA @ texture(0), ImageB @ texture(1), Mask @ texture(2)).
//
// Forks (named, DX11->Metal):
//   (1) Sampler address mode: TiXL's SamplerState (BlendWithMask.t3, child 14605cc9) sets AddressU/V =
//       "Mirror" (AddressW "Wrap", irrelevant for 2D). The Metal equivalent of D3D11 MIRROR is
//       MirrorRepeat; bound in the op (cookBlendWithMask). The aspect-fit uvB can push past [0,1];
//       MirrorRepeat reflects (TiXL parity).
//   (2) HLSL Sample(s, uv) (implicit mip) -> Metal sample(s, uv) (implicit mip 0; no mipped inputs).
//   (3) GetDimensions(): HLSL reads (width,height) per texture; Metal uses get_width()/get_height().
//       The aspect math (imageAAspect < imageBAspect branch) is ported verbatim.
//   (4) GenerateMips host plumbing (the .t3 RenderTarget GenerateMips=true) omitted (no mips) — same
//       fork class as Blur/Displace/Blend.
#include <metal_stdlib>
#include "blendwithmask_params.h"   // BlendWithMaskParams, BLENDWITHMASK_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped, same as blend_vs.
vertex VSOut blendwithmask_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of BlendWithMask.hlsl psMain():
//   tA = ImageA.Sample(s, uv) * ImageAColor;
//   uvB = aspect-fit; tB = ImageB.Sample(s, uvB) * ImageBColor;
//   mask = Mask.Sample(s, uv);
//   tA.a = clamp(tA.a,0,1); tB.a = clamp(tB.a,0,1);
//   return lerp(tA, tB, mask.r);   // a per-channel float4 lerp by the mask's RED channel
fragment float4 blendwithmask_fs(VSOut in [[stage_in]],
                                 texture2d<float> imageA [[texture(0)]],
                                 texture2d<float> imageB [[texture(1)]],
                                 texture2d<float> mask   [[texture(2)]],
                                 sampler texSampler      [[sampler(0)]],
                                 constant BlendWithMaskParams& P [[buffer(BLENDWITHMASK_Params)]]) {
  float2 uv = in.texCoord;

  float4 imageAColor = float4(P.ImageAColorR, P.ImageAColorG, P.ImageAColorB, P.ImageAColorA);
  float4 imageBColor = float4(P.ImageBColorR, P.ImageBColorG, P.ImageBColorB, P.ImageBColorA);

  float4 tA = imageA.sample(texSampler, uv) * imageAColor;

  float imageAAspect = (float)imageA.get_width() / (float)imageA.get_height();
  float imageBAspect = (float)imageB.get_width() / (float)imageB.get_height();

  float2 uvB = imageAAspect < imageBAspect
      ? float2((uv.x - 0.5f) * imageAAspect / imageBAspect + 0.5f, uv.y)
      : float2(uv.x, (uv.y - 0.5f) * imageBAspect / imageAAspect + 0.5f);

  float4 tB = imageB.sample(texSampler, uvB) * imageBColor;

  float4 maskColor = mask.sample(texSampler, uv);

  tA.a = clamp(tA.a, 0.0f, 1.0f);
  tB.a = clamp(tB.a, 0.0f, 1.0f);

  return mix(tA, tB, maskColor.r);
}
