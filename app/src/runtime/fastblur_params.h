// Shared host<->shader params for the TiXL-ported FastBlur IMAGE FILTER (a MULTI-PASS COMPUTE leaf —
// the first real consumer of the multi-pass scratch seam). Mirrors the two pixel shaders that the
// TiXL FastBlur.t3 compound runs through _ExecuteFastBlurPasses.cs:
//   external/tixl Operators/Lib/Assets/shaders/img/blur/FastBlur-DownsamplePS.hlsl  (down pass)
//   external/tixl Operators/Lib/Assets/shaders/img/blur/FastBlur-UpsampleAcculuatePS.hlsl (up pass)
//
// ALGORITHM (Dual Kawase++ / Marius Bjørge): downsample-blur N levels (each a 4-tap box, DC gain 1),
// then upsample-blur back up (each a 9-tap tent, weights NORMALIZED to sum 1). The upsample WRITES
// each level (TiXL DisabledBlendState — _ExecuteFastBlurPasses.cs:80, NOT additive), so it is a fixed
// leaf pipeline, never a Layer2d composite. See point_ops_fastblur.cpp for the named fork.
//
// HLSL cbuffers (both fit in 16 / 32 bytes; HLSL & MSL align float4/float2 the same way):
//   DownParams (b0, 16B): float2 InvSrcSize; float OffsetPx; float _pad0;
//   UpParams   (b0, 32B): float2 InvLowSize; float OffsetPx; float WCenter; float WCard; float WDiag;
//                         float2 _pad0;
// Field order is verbatim — do NOT reorder (silent-corruption trap if a float2 straddles 16B).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// Downsample pass params (FastBlur-DownsamplePS.hlsl cbuffer DownParams).
struct FastBlurDownParams {
  float InvSrcSize[2];  // 1/srcWidth, 1/srcHeight (UV step of ONE source texel)
  float OffsetPx;       // tap offset in SOURCE pixels (TiXL hardcodes 1.0)
  float _pad0;
};

// Upsample pass params (FastBlur-UpsampleAcculuatePS.hlsl cbuffer UpParams).
struct FastBlurUpParams {
  float InvLowSize[2];  // 1/lowWidth, 1/lowHeight (UV step of ONE low-res texel)
  float OffsetPx;       // tap offset in LOW pixels (TiXL hardcodes 1.0)
  float WCenter;        // normalized center weight
  float WCard;          // normalized weight for EACH of the 4 cardinal taps
  float WDiag;          // normalized weight for EACH of the 4 diagonal taps
  float _pad0[2];
};

enum FastBlurBinding {
  // Compute namespace: t0 (Src/Low) -> texture(0); u0 (Result) -> texture(1); s0 -> sampler(0).
  FASTBLUR_Src    = 0,  // texture(0): input to read (down: prev level; up: the LOW level)
  FASTBLUR_Result = 1,  // texture(1): RWTexture2D output written this pass
  FASTBLUR_Sampler = 0,  // sampler(0): linear, clamp-to-edge
  FASTBLUR_Params = 0,  // buffer(0): the down OR up cbuffer for this pass
};

// Threadgroup tile (logical [numthreads(8,8,1)]; MSL has no numthreads -> the host dispatch is the
// only place the 8x8 tile lives). Output dims are ceil-div'd by these and a kernel guard skips the
// remainder overrun (same idiom as crop.metal).
#ifndef __METAL_VERSION__
static constexpr uint32_t FASTBLUR_TGX = 8;
static constexpr uint32_t FASTBLUR_TGY = 8;
static_assert(sizeof(FastBlurDownParams) == 16, "FastBlurDownParams 16 bytes");
static_assert(sizeof(FastBlurUpParams) == 32, "FastBlurUpParams 32 bytes");
#endif
