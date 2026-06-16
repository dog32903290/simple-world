// Shared host<->shader params for the TiXL-ported RgbTV IMAGE FILTER (a CRT / RGB-stripe glitch op).
// This is the (E)-seam phase-2 leaf: the first op that binds a SECOND asset texture (t1, the
// perlin-noise asset) AND consumes the orphan mip seam (it samples its INPUT at LOD 0..7).
//
// NAMED IMPROVEMENT FORK (full detail in point_ops_rgbtv.cpp FORK #3): TiXL ships RgbTV with the
// perlin noise node DISCONNECTED (t1 = Blur of an empty LoadImage ≈ black -> degenerate uniform
// noiseColor=0.5*GlitchAmount). We bind the real perlin asset at t1 so the CRT glitch works. The
// noise path is therefore improvement-over-TiXL-WIP, NOT byte-parity. The 24-float cbuffer math
// below IS byte-parity with RgbTV.hlsl; only the t1 texture source forks.
//
// TiXL authority:
//   Operators/Lib/image/fx/glitch/RgbTV.cs   — the op ports (24 floats + Image + Resolution).
//   Operators/Lib/image/fx/glitch/RgbTV.t3   — the _multiImageFxSetup compound: FloatsToBuffer fills
//       the cbuffer in CONNECTION order. Traced 1:1 (STEP-0): the 24 MultiInput connections land in
//       EXACTLY the cbuffer field order below (the two intermediate-math entries — Clamp(Add(...)) =>
//       GlitchAmount and BoolToFloat(...) => GlitchTime — sit at their natural cbuffer slots 14/15).
//   Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl — the pixel shader (ported 1:1 into rgbtv.metal).
//
// HLSL cbuffer ParamConstants (b0), verbatim field order — 24 floats. HLSL packs scalars into 16-byte
// rows; the comment markers below match the .hlsl blank-line grouping (4 per row after the first).
// 24 floats = 96 bytes, a multiple of 16 — no tail padding needed. Do NOT reorder (the FloatsToBuffer
// routing + the .hlsl read both depend on this exact order; reorder = silent param corruption).
// No packed_float3 / float4 mid-struct — all scalar floats, so no 16B-straddle alignment trap.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RgbTvParams {
  float Visibility;        // 0
  float PatternAmount;     // 1
  float ImageBrightness;   // 2  (.cs ImageBrightess [sic])
  float BlackLevel;        // 3
  float Contrast;          // 4  (.cs ImageContrast)
  float BlurImage;         // 5
  float GlowIntensity;     // 6
  float GlowBlur;          // 7
  float PatternSize;       // 8
  float ShiftColums;       // 9  (.cs ShiftColumns; .hlsl spells it ShiftColums)
  float Gaps;              // 10
  float PatternBlurX;      // 11 (.cs PatternBlur.X via Vector2Components)
  float PatternBlurY;      // 12 (.cs PatternBlur.Y via Vector2Components)
  float GlitchAmount;      // 13 (.t3: Clamp(Add(GlitchAmount, AnimValue(Flicker)),0,1e4); default 1.0)
  float GlitchTime;        // 14 (.t3: BoolToFloat -> _Time when GlitchTimeOverride default 0)
  float GlitchDistort;     // 15
  float ShadeDistortion;   // 16
  float NoiseForDistortion;// 17
  float Noise;             // 18
  float NoiseSpeed;        // 19
  float NoiseExponent;     // 20
  float NoiseColorize;     // 21
  float Buldge;            // 22 (.cs/.hlsl spelling "Buldge")
  float Vignette;          // 23
};

// Resolution cbuffer (b1 in .hlsl): TargetWidth/TargetHeight. The kernel reads these for aspectRatio.
struct RgbTvResolution {
  float TargetWidth;
  float TargetHeight;
  float _pad0[2];  // pad to 16B (we bind it as its own buffer; keep 16-aligned for safety)
};

enum RgbTvBinding {
  // Compute namespace (single MSL texture/sampler/buffer namespace — t0/t1/u0 -> texture indices):
  RGBTV_Input   = 0,  // texture(0): input image, MIPPED (HLSL t0 inputTexture, sampled at LOD 0..7)
  RGBTV_Noise   = 1,  // texture(1): perlin-noise asset (HLSL t1 noiseTexture)
  RGBTV_Result  = 2,  // texture(2): RWTexture2D output (HLSL u0 — kept after the two read textures)
  RGBTV_ClampSampler = 0,  // sampler(0): clamp-to-edge, linear + mip-linear (HLSL s0 clampingSampler)
  RGBTV_WrapSampler  = 1,  // sampler(1): wrap/repeat, linear (HLSL s1 wrappingSampler)
  RGBTV_Params  = 0,  // buffer(0): RgbTvParams (b0)
  RGBTV_Res     = 1,  // buffer(1): RgbTvResolution (b1)
};

// MIP level count the .hlsl hardcodes (mipLevelCount = 7 -> loop i = 0..7 inclusive = 8 samples).
// Outside the host-only guard: the kernel (rgbtv.metal) reads it too.
#define RGBTV_MIP_LEVELS 7

// Threadgroup tile (logical [numthreads(8,8,1)]; MSL has no numthreads -> host dispatch is the only
// place the 8x8 tile lives). Output dims ceil-div'd by these; a kernel guard skips remainder overrun.
#ifndef __METAL_VERSION__
static constexpr uint32_t RGBTV_TGX = 8;
static constexpr uint32_t RGBTV_TGY = 8;
static_assert(sizeof(RgbTvParams) == 96, "RgbTvParams 24 floats = 96 bytes (verbatim cbuffer order)");
static_assert(sizeof(RgbTvResolution) == 16, "RgbTvResolution 16 bytes");
#endif
