// Shared host<->shader params for the TiXL-ported Sharpen IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Sharpen.hlsl and Sharpen.cs/.t3.
// TiXL authority: Sharpen.cs (Image/SampleRadius/Strength/Clamping inputs) + Sharpen.t3
// (defaults SampleRadius=1.0, Strength=1.0, Clamping=false, Wrap=MirrorOnce, OutputFormat=
// R16G16B16A16_Float) + Sharpen.hlsl (the single-pass kernel: 3x3 desaturated Laplacian unsharp
// mask — final = col + col*Strength*(8*L(center) - sum of 8 neighbour luminances)).
//
// Resolution cbuffer (b1) holds TargetWidth/TargetHeight, matching the .hlsl `Resolution`
// register(b1). The .hlsl derives the per-pixel sample step from Image.GetDimensions
// (pX = SampleRadius/width, pY = SampleRadius/height) — we pass the SOURCE image dims here so the
// Metal kernel reproduces GetDimensions faithfully.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SharpenParams {
  // TiXL Sharpen.hlsl cbuffer (b0): SampleRadius, Strength, Clamping
  float SampleRadius;  // TiXL SampleRadius (Single), default 1.0; neighbour sample step (px)
  float Strength;      // TiXL Strength (Single), default 1.0; unsharp-mask amount
  float Clamping;      // TiXL Clamping (bool as float), default 0.0; saturate() the result
  float _pad0;         // pad 12 -> 16 (16-byte multiple)
};

struct SharpenResolution {
  // TiXL Sharpen.hlsl Resolution cbuffer (b1): TargetWidth, TargetHeight.
  // We pass the SOURCE image dims here (= Image.GetDimensions in the .hlsl pX/pY step).
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum SharpenBinding {
  SHARPEN_Params     = 0,  // constant SharpenParams& (b0)
  SHARPEN_Resolution = 1,  // constant SharpenResolution& (b1)
  // texture(0) = Image, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SharpenParams) == 16, "SharpenParams 16 bytes");
static_assert(sizeof(SharpenResolution) == 16, "SharpenResolution 16 bytes");
#endif
