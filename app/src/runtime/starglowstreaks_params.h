// Shared host<->shader params for the TiXL-ported StarGlowStreaks IMAGE FILTER (image/fx/stylize).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/StarGlowStreaks.hlsl +
// StarGlowStreaks.cs/.t3.
//
// The .hlsl cbuffer ParamConstants(b0) layout (verbatim field order):
//   float4 Color; float Range; float Brightness; float Threshold; float BlendMode;
//   float4 OriginalColor; float Quality; float GlareModes;
// (b1 Resolution{TargetWidth,TargetHeight} is declared in the .hlsl but UNUSED by psMain — omitted.)
//
// We fold ParamConstants into ONE host struct bound at one slot. Field order + 16-byte alignment
// match the HLSL cbuffer packing rules: float4 are 16-aligned; the trailing scalar run after the
// first float4 packs into one 16-byte register group, OriginalColor (float4) starts a fresh
// 16-byte register, then Quality/GlareModes pack into the next register. We pad to make the C++
// struct match that packing so setFragmentBytes hands the kernel a faithful cbuffer image.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

#ifdef __METAL_VERSION__
struct StarGlowStreaksParams {
  float4 Color;          // b0 float4 Color
  float  Range;          // b0 float  Range
  float  Brightness;     // b0 float  Brightness
  float  Threshold;      // b0 float  Threshold (color-key, 0..1)
  float  BlendMode;      // b0 float  BlendMode (int-as-float, dispatched by (int)BlendMode)
  float4 OriginalColor;  // b0 float4 OriginalColor
  float  Quality;        // b0 float  Quality (mip level for SampleLevel)
  float  GlareModes;     // b0 float  GlareModes (0..4 enum-as-float)
  float  _pad0;          // pad the trailing scalar register to 16 bytes (matches cbuffer packing)
  float  _pad1;
};
#else
struct StarGlowStreaksParams {
  float ColorR, ColorG, ColorB, ColorA;      // float4 Color
  float Range;
  float Brightness;
  float Threshold;
  float BlendMode;
  float OrigR, OrigG, OrigB, OrigA;          // float4 OriginalColor
  float Quality;
  float GlareModes;
  float _pad0;
  float _pad1;
};
#endif

enum StarGlowStreaksBinding {
  SGS_Params = 0,  // constant StarGlowStreaksParams& (folds .hlsl b0)
  // texture(0) = inputTexture (Image), sampler(0) = MirrorOnce + linear (TiXL t3 Wrap=MirrorOnce,
  // _ImageFxShaderSetupStatic default Filter=MinMagMipLinear).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(StarGlowStreaksParams) == 64,
              "StarGlowStreaksParams: 4(Color)+4(scalars)+4(Orig)+4(Quality/Glare/pad) floats = 64 bytes");
#endif
