// DistortAndShade: TiXL-ported image warp+shade (lane multi-image). Faithful 1:1 port of external/tixl
// Operators/Lib/Assets/shaders/img/fx/DistortAndShade.hlsl. A fullscreen triangle samples ImageA at a
// UV pushed radially from Center by an amount read per-pixel from ImageB, then lerps the result toward
// ShadeColor by Shade*displaceAmount. The SECOND consumer of the multi-image seam (two graph-wired
// Texture2D inputs: ImageA @ texture(0), ImageB @ texture(1)).
//
// Forks (named, DX11->Metal):
//   (1) Sampler address mode: TiXL's _multiImageFxSetup binds the host sampler with WrapMode "Mirror"
//       (DistortAndShade.t3 _multiImageFxSetup.WrapMode = "Mirror"). The Metal equivalent of D3D11
//       MIRROR is MirrorRepeat; bound in the op (cookDistortAndShade). A warp that pushes UV past the
//       edge mirrors (TiXL parity), not clamps.
//   (2) HLSL Sample(s, uv) (implicit mip) -> Metal sample(s, uv) (implicit mip 0; no mipped inputs).
//   (3) Faithful: the .hlsl multiplies displaceAmount (a float4) by Displacement, so the per-channel
//       displace differs by channel — kept verbatim (uv2 uses the float2 .xy of that product). The
//       lerp's third arg is also a float4 (Shade * displaceAmount), per-channel — kept verbatim.
#include <metal_stdlib>
#include "distortandshade_params.h"   // DistortAndShadeParams, DISTORTANDSHADE_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as displace_vs / blur_vs.
vertex VSOut distortandshade_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of DistortAndShade.hlsl psMain():
//   float2 d = uv - Center;
//   float4 displaceAmount = ImageB.Sample(s, uv);
//   float2 uv2 = uv + d * displaceAmount * Displacement;   // (float2 = the .xy of the float4 product)
//   float4 c2 = ImageA.Sample(s, uv2);
//   return lerp(c2, ShadeColor, Shade * displaceAmount);   // per-channel lerp weight
fragment float4 distortandshade_fs(VSOut in [[stage_in]],
                                   texture2d<float> imageA [[texture(0)]],
                                   texture2d<float> imageB [[texture(1)]],
                                   sampler texSampler      [[sampler(0)]],
                                   constant DistortAndShadeParams& P [[buffer(DISTORTANDSHADE_Params)]]) {
  float2 uv = in.texCoord;
  float2 center = float2(P.CenterX, P.CenterY);
  float2 d = uv - center;

  float4 displaceAmount = imageB.sample(texSampler, float2(uv.x, uv.y));

  // uv2 = uv + d * displaceAmount * Displacement. In HLSL `d * displaceAmount` is float2*float4 which
  // broadcasts d over .xy (the result is consumed as float2). Replicate: take .xy of the product.
  float2 uv2 = uv + d * displaceAmount.xy * P.Displacement;

  float4 c2 = imageA.sample(texSampler, uv2);

  float4 shadeColor = float4(P.ShadeColorR, P.ShadeColorG, P.ShadeColorB, P.ShadeColorA);
  // lerp(c2, ShadeColor, Shade * displaceAmount): the weight is a float4 (per-channel).
  return mix(c2, shadeColor, P.Shade * displaceAmount);
}
