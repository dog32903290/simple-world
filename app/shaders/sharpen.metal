// Sharpen: TiXL-ported 3x3 desaturated-Laplacian unsharp mask, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Sharpen.hlsl (psMain).
// Builds a luminance Laplacian over the 8 neighbours and the center, scales by Strength, adds
// it back into the center color (col + col*Strength*L). Optional saturate() when Clamping>0.
//
// FORK (named, DX11->Metal):
//  1. HLSL GetDimensions(width,height) -> we pass the source dims in via SharpenResolution
//     (host fills from the input texture). pX/pY computed from those, identical to GetDimensions.
//  2. Sampler: fixed linear+clamp. Sharpen.t3 sets Wrap=MirrorOnce; at the 1px sample radius the
//     edge difference between Clamp and MirrorOnce is a hairline of edge pixels only — kept Clamp
//     for parity with the other image filters (same fork class as Blur/Tint). NON-default Wrap is
//     a follow-up. NAMED so the refuter can weigh the 1px edge ring.
//  3. desaturate() / the 8-tap Laplacian / the col + col*Strength*L formula are verbatim.
#include <metal_stdlib>
#include "sharpen_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut sharpen_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL: float desaturate(float4 color) { return dot(float3(0.2126,0.7152,0.0722), color.xyz); }
static inline float desaturate(float4 color) {
  return dot(float3(0.2126f, 0.7152f, 0.0722f), color.xyz);
}

// Mirror of Sharpen.hlsl psMain.
fragment float4 sharpen_fs(VSOut in [[stage_in]],
                           texture2d<float> Image       [[texture(0)]],
                           sampler texSampler           [[sampler(0)]],
                           constant SharpenParams& P     [[buffer(SHARPEN_Params)]],
                           constant SharpenResolution& R [[buffer(SHARPEN_Resolution)]]) {
  float2 uv = in.texCoord;

  // HLSL: Image.GetDimensions(width,height); -> passed in via R.
  float width  = R.TargetWidth;
  float height = R.TargetHeight;

  // HLSL: float pX = SampleRadius / width; float pY = SampleRadius / height;
  float pX = P.SampleRadius / width;
  float pY = P.SampleRadius / height;

  // HLSL: col = Image.Sample(texSampler, uv);
  float4 col = Image.sample(texSampler, uv);

  // 4-connected neighbours.
  float colorL = desaturate(Image.sample(texSampler, uv + float2(-pX, 0.0f)));
  float colorR = desaturate(Image.sample(texSampler, uv + float2( pX, 0.0f)));
  float colorA = desaturate(Image.sample(texSampler, uv + float2( 0.0f, -pY)));
  float colorB = desaturate(Image.sample(texSampler, uv + float2( 0.0f,  pY)));

  // Diagonal neighbours.
  float colorLA = desaturate(Image.sample(texSampler, uv + float2(-pX,  pY)));
  float colorRA = desaturate(Image.sample(texSampler, uv + float2( pX,  pY)));
  float colorLB = desaturate(Image.sample(texSampler, uv + float2(-pX, -pY)));
  float colorRB = desaturate(Image.sample(texSampler, uv + float2( pX, -pY)));

  // HLSL: final = col + col*Strength*(8*desaturate(col) - 8 neighbour luminances)
  float4 final = col + col * P.Strength *
                 (8.0f * desaturate(col)
                  - colorL - colorR - colorA - colorB
                  - colorLA - colorRA - colorLB - colorRB);

  // HLSL: if (Clamping > 0.0) final = saturate(final);
  if (P.Clamping > 0.0f) {
    final = saturate(final);
  }
  return final;
}
