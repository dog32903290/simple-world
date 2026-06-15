// Shared host<->shader params for the TiXL-ported TransformImage IMAGE FILTER (image/transform).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/TransformImage.hlsl +
// TransformImage.cs/.t3.
//
// cbuffer layout follows the .hlsl (NOT the .cs [Input] order — the NodeSpec ports follow the .cs).
// The kernel reads TWO cbuffers:
//   ParamConstants (b0): float2 Offset; float2 Stretch; float Scale; float Rotation; float RepeatMode;
//   IntParams      (b2): int2 OrgResolution; int2 TargetResolution;
// We fold both into ONE host struct (TransformImageParams) and bind it at one buffer slot; the
// kernel reads its members directly. RepeatMode/TargetResolution are declared in the .hlsl but the
// ACTIVE path ignores them (RepeatMode commented-out branch; TargetResolution only in the commented
// rescale). We carry them as honest no-op fields so the struct shape matches the .hlsl cbuffers.
//
// OrgResolution = the SOURCE texture size, fed from inputTexture->width()/height() in the cook
// (TiXL wires GetTextureSize(Image) -> OrgResolution). This is the LOAD-BEARING b2 source-aspect
// seam called out in the work order.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TransformImageParams {
  // --- mirrors .hlsl cbuffer ParamConstants(b0) ---
  float OffsetX, OffsetY;    // float2 Offset
  float StretchX, StretchY;  // float2 Stretch
  float Scale;               // float Scale
  float Rotation;            // float Rotation
  float RepeatMode;          // float RepeatMode (declared in .hlsl, IGNORED by active path — no-op)
  float _pad0;               // pad to 16-byte boundary before the int block

  // --- mirrors .hlsl cbuffer IntParams(b2) ---
  int OrgResolutionX, OrgResolutionY;        // int2 OrgResolution (SOURCE texture size; source-aspect)
  int TargetResolutionX, TargetResolutionY;  // int2 TargetResolution (declared, only used in commented rescale — no-op)
};

enum TransformImageBinding {
  TI_Params = 0,  // constant TransformImageParams& (single slot folds .hlsl b0 + b2)
  // texture(0) = inputTexture (ImageA), sampler(0) = Wrap+linear (TiXL t3 Wrap/MinMagMipLinear).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TransformImageParams) == 48,
              "TransformImageParams: 8 floats(32) + 4 ints(16) = 48 bytes");
#endif
