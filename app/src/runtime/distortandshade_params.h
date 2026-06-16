// Shared host<->shader params for the TiXL-ported DistortAndShade IMAGE FILTER (lane multi-image,
// image/fx/distort). Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/DistortAndShade.hlsl.
// DistortAndShade warps `ImageA` by a per-pixel amount read from `ImageB` (a graph-wired SECOND image),
// then lerps toward ShadeColor by Shade*displaceAmount. It is the SECOND consumer of the multi-image
// seam (Displace was the first): TWO graph-wired Texture2D inputs — ImageA @ texture(0), ImageB @
// texture(1) (backward-traced in STEP-0: the .t3 binds SrvFromTexture2d(ImageA) first, ImageB second,
// into _multiImageFxSetup's SetPixelAndVertexShaderStage SRV array -> t0=ImageA, t1=ImageB, matching
// the .hlsl register(t0)/register(t1)).
//
// cbuffer ParamConstants (HLSL b0) field order — packed here as scalars (particle_params.h discipline,
// no Vector2/Vector4 forcing host/shader layout drift):
//     float4 ShadeColor;  float Displacement;  float Shade;  float2 Center;
// .t3 param routing (STEP-0, FloatsToBuffer connection-order, the Cut-55 rule): the 8 cbuffer floats
// are fed by 8 .t3 connections in EXACTLY this order — Vector4Components(ShadeColor).{X,Y,Z,W},
// root.Displacement, root.Shading, Vector2Components(Center).{X,Y}. The Vector*Components nodes are
// identity splitters (no intervening math); ColorGrade children on ImageA/ImageB DANGLE (outputs
// unconnected) so they do NOT touch the image path. => clean 1:1 op-port -> cbuffer-field routing.
//
// TiXL .t3 defaults (DistortAndShade.t3): ShadeColor (1,1,1,1), Displacement 0.5, Shading 0,
// Center (0.5,0.5). ImageA/ImageB default null (unwired).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct DistortAndShadeParams {
  // cbuffer ParamConstants (HLSL b0), same field order:
  float ShadeColorR, ShadeColorG, ShadeColorB, ShadeColorA;  // float4 ShadeColor, default (1,1,1,1)
  float Displacement;  // default 0.5; scales the ImageB-driven UV push
  float Shade;         // TiXL "Shading" port -> HLSL "Shade"; default 0; lerp weight toward ShadeColor
  float CenterX, CenterY;  // float2 Center, default (0.5,0.5); displacement radiates from here
};

enum DistortAndShadeBinding {
  DISTORTANDSHADE_Params = 0,  // constant DistortAndShadeParams& (b0)
  // texture(0) = ImageA, texture(1) = ImageB, sampler(0) = linear; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DistortAndShadeParams) == 32,
              "DistortAndShadeParams 32 bytes (8 floats, 16-byte multiple)");
#endif
