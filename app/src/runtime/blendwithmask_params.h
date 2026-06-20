// Shared host<->shader params for the TiXL-ported BlendWithMask IMAGE FILTER (lane multi-image,
// image/use). Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/BlendWithMask.hlsl.
// BlendWithMask composites THREE graph-wired images: ImageA @ texture(0), ImageB @ texture(1), Mask @
// texture(2). It is the FIRST op with THREE Texture2D inputs (Displace/DistortAndShade/Blend had two).
// The output is a per-pixel lerp(tA, tB, mask.r): a mask-driven crossfade between ImageA and ImageB.
//
// cbuffer ParamConstants (HLSL b0) field order — packed here as scalars (particle_params.h discipline):
//     float4 ImageAColor;  float4 ImageBColor;  float ColorMode;  float AlphaMode;
// NOTE: the .hlsl declares ColorMode/AlphaMode in the cbuffer but NEVER reads them (the only output is
// lerp(tA, tB, mask.r) — no mode switch). They are vestigial fields kept for cbuffer layout parity; the
// .t3 leaves them UNFED (no connection targets them) so they are 0 at runtime. We carry them as zeroed
// fields (op never writes them) for byte-layout fidelity with the .hlsl; the shader ignores them.
//
// .t3 param routing (STEP-0, the Cut-55 rule — BACKWARD-TRACED & verified clean 1:1, NO math junctions;
// BlendWithMask is NOT a _multiImageFxSetup compound — it is hand-wired). The FloatsToBuffer (28a1db99)
// cbuffer slot (49556d12) is fed by EXACTLY 8 connections in this order:
//   Vector4Components(ColorA=d86c6e46).{X,Y,Z,W} -> ImageAColor.xyzw,
//   Vector4Components(ColorB=ff64625a).{X,Y,Z,W} -> ImageBColor.xyzw.
// (ColorMode/AlphaMode get NO feed -> 0.) The SRV array on PixelShaderStage (1bc9e608, slot 50052906)
// is fed in order: SrvFromTexture2d(9df55df1)<-ImageA=t0, SrvFromTexture2d(e8f720be)<-ImageB=t1,
// SrvFromTexture2d(d2c3a258)<-Mask=t2 — matching the .hlsl register(t0/t1/t2).
//
// TiXL .t3 defaults (BlendWithMask.t3): ColorA (1,1,1,1), ColorB (1,1,1,1). ImageA/ImageB/Mask default
// null (unwired).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct BlendWithMaskParams {
  // cbuffer ParamConstants (HLSL b0), same field order:
  float ImageAColorR, ImageAColorG, ImageAColorB, ImageAColorA;  // float4 ImageAColor, default (1,1,1,1)
  float ImageBColorR, ImageBColorG, ImageBColorB, ImageBColorA;  // float4 ImageBColor, default (1,1,1,1)
  float ColorMode;  // .hlsl cbuffer field; UNFED in .t3 -> 0; .hlsl never reads it (layout parity only)
  float AlphaMode;  // .hlsl cbuffer field; UNFED in .t3 -> 0; .hlsl never reads it (layout parity only)
  float _pad0, _pad1;  // pad 40 -> 48 (10 floats + 2 = 12, 16-byte multiple)
};

enum BlendWithMaskBinding {
  BLENDWITHMASK_Params = 0,  // constant BlendWithMaskParams& (b0)
  // texture(0) = ImageA, texture(1) = ImageB, texture(2) = Mask, sampler(0) = linear; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BlendWithMaskParams) == 48, "BlendWithMaskParams 48 bytes (16-byte multiple)");
#endif
