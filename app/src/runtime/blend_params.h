// Shared host<->shader params for the TiXL-ported Blend IMAGE FILTER (lane multi-image, image/use).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Blend.hlsl. Blend composites TWO graph-
// wired images: ImageA @ texture(0), ImageB @ texture(1) (a multi-image seam consumer, same gather as
// Displace/DistortAndShade). ColorA/ColorB pre-multiply each sample; BlendMode (ColorMode in the .hlsl)
// picks the RGB compositing math, AlphaMode picks the alpha math, ScaleMode aspect-fits ImageB.
//
// cbuffer ParamConstants (HLSL b0) field order — packed here as scalars (particle_params.h discipline,
// no Vector4 forcing host/shader layout drift):
//     float4 ImageAColor;  float4 ImageBColor;  float ColorMode;  float AlphaMode;
//     float UseNormalForUpperHalf;  float ScaleMode;
//
// .t3 param routing (STEP-0, FloatsToBuffer connection-order, the Cut-55 rule — BACKWARD-TRACED &
// verified clean 1:1, NO arithmetic junctions): the _multiImageFxSetupStatic FloatsToBuffer MultiInput
// is fed by 12 connections (Blend.t3 Connections, the ones whose TargetSlotId = 2929c4c9...) in EXACTLY
// this order:
//   Vector4Components(ColorA=46965dc5).{X=cfb58526, Y=2f8e90dd, Z=162bb4fe, W=e1dede5f}  -> ImageAColor.xyzw
//   Vector4Components(ColorB=42f85198).{X,Y,Z,W}                                          -> ImageBColor.xyzw
//   IntToFloat(BlendMode=8127b1df)                                                        -> ColorMode
//   IntToFloat(AlphaMode=b7edf6bf)                                                         -> AlphaMode
//   BoolToFloat(NormalForUpperHalf=882f89a2)                                               -> UseNormalForUpperHalf
//   IntToFloat(ScaleMode=b2c29ae7)                                                         -> ScaleMode
// The Vector4Components nodes are identity splitters; IntToFloat/BoolToFloat are pure type casts (NOT
// arithmetic). => clean 1:1 op-port -> cbuffer-field routing, matching the .hlsl field order verbatim.
//
// TiXL .t3 defaults (Blend.t3): ColorA (1,1,1,1), ColorB (1,1,1,1), BlendMode 0 (Normal), AlphaMode 0
// (Normal), NormalForUpperHalf false, ScaleMode 0 (Stretch). ImageA/ImageB default null (unwired).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct BlendParams {
  // cbuffer ParamConstants (HLSL b0), same field order:
  float ImageAColorR, ImageAColorG, ImageAColorB, ImageAColorA;  // float4 ImageAColor, default (1,1,1,1)
  float ImageBColorR, ImageBColorG, ImageBColorB, ImageBColorA;  // float4 ImageBColor, default (1,1,1,1)
  float ColorMode;               // TiXL "BlendMode" (RgbBlendModes int), default 0 (Normal); RGB compositing
  float AlphaMode;               // TiXL "AlphaMode" (AlphaBlendModes int), default 0 (Normal); alpha math
  float UseNormalForUpperHalf;   // TiXL "NormalForUpperHalf" (bool), default 0
  float ScaleMode;               // TiXL "ScaleMode" (ScaleModes int), default 0 (Stretch); ImageB aspect-fit
};

enum BlendBinding {
  BLEND_Params = 0,  // constant BlendParams& (b0)
  // texture(0) = ImageA, texture(1) = ImageB, sampler(0) = linear; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BlendParams) == 48, "BlendParams 48 bytes (12 floats, 16-byte multiple)");
#endif
