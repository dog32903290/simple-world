// Shared host<->shader params for the TiXL-ported Crop IMAGE FILTER (the first -cs.hlsl COMPUTE
// leaf). Mirrors external/tixl Operators/Lib/Assets/shaders/img/transform/CropImage-cs.hlsl and
// Operators/Lib/image/transform/Crop.cs.
//
// The HLSL cbuffer (b0) is, verbatim:
//     float CropLeft; float CropRight; float CropTop; float CropBottom;  // 16 bytes
//     float4 BackgroundColor;                                            // float4 @ offset 16
// HLSL cbuffers and MSL `constant` buffers both align a float4 to 16 bytes; the four scalar floats
// already fill offsets 0..15, so float4 lands naturally at offset 16 with NO padding — keep this
// field order, do NOT reorder (silent-corruption trap if BackgroundColor straddles a 16B boundary).
// No packed_float3 anywhere (metal-cpp-discipline: float4 is naturally 16-aligned).
//
// FORK / fidelity note: TiXL's .hlsl #includes shared/{hash,noise,point,quat}-functions.hlsl but the
// Crop kernel uses NONE of them — they are dead includes. Our crop.metal drops them (no behavior
// change). The kernel itself is ported 1:1.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CropParams {
  // CropImage-cs.hlsl cbuffer Params (b0): four crop margins (in pixels), then the padding color.
  float CropLeft;
  float CropRight;
  float CropTop;
  float CropBottom;
  // BackgroundColor (float4) — the color written to out-of-source (cropped/padded) pixels. Lands at
  // offset 16; do not move above the four scalars.
  float BackgroundColor[4];
};

enum CropBinding {
  CROP_Params = 0,  // constant CropParams& (b0)
  CROP_Source = 0,  // texture(0): input  (HLSL t0). MSL texture namespace is shared t0/u0 -> in @0.
  CROP_Result = 1,  // texture(1): output (HLSL u0, RWTexture2D).
};

// Threadgroup size MUST match crop.metal's logical [numthreads(8,8,1)] (MSL has no numthreads
// attribute; the host dispatch is the only place the 8x8 tile size lives).
#ifndef __METAL_VERSION__
static constexpr uint32_t CROP_TGX = 8;
static constexpr uint32_t CROP_TGY = 8;
static_assert(sizeof(CropParams) == 32, "CropParams 32 bytes (4 floats + float4 @ offset 16)");
#endif
