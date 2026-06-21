// Shared host<->shader params for the TiXL-ported Bloom IMAGE FILTER (the multi-pass-executor seam).
// Authority: external/tixl Operators/Lib/image/fx/_/_ExecuteBloomPasses.cs (the real pass executor;
// the .cs/.t3 wrapper just wires it) + Operators/Lib/Assets/shaders/img/blur/Bloom-*.hlsl (the five
// per-pass pixel shaders). Bloom is a 4N+2-pass pyramid: 1 Brightpass + N×(Downsample, BlurV, BlurH)
// + 1 Copy + N×Upsample-add, output = the composite target.
//
// Three cbuffers, one per pass family, packed BYTE-FOR-BYTE to the HLSL register(b0) layouts:
//   ThresholdParams  (Brightpass)        16B — Bloom-BrightpassPS.hlsl
//   BloomBlurParams  (SeparableBlur)     32B — Bloom-SeparableBlurPS.hlsl (= _ExecuteBloomPasses
//                                              BlurParameters struct, .cs:320-348)
//   CompositeParams  (Upsample-add)      64B — Bloom-UpsamplePS.hlsl (= CompositeParams, .cs:353-372)
//
// All-scalar where HLSL uses vectors (particle_params.h discipline): Metal `packed_float3`/`float2`
// match the HLSL float3/float2 packing; sizes are explicit 16-byte multiples and static_assert-pinned.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// --- Brightpass (Bloom-BrightpassPS.hlsl b0) ---
// HLSL:  float3 ColorWeights; float Threshold;   -> 16 bytes.
struct BloomThresholdParams {
#ifdef __METAL_VERSION__
  packed_float3 ColorWeights;  // Rec.601 luminance weights (default 0.299,0.587,0.114)
#else
  float ColorWeights[3];
#endif
  float Threshold;             // brightness cutoff (TiXL outer default 0.25)
};

// --- Separable blur (Bloom-SeparableBlurPS.hlsl b0) ---
// HLSL:  float DirX, DirY, Width, Height; int UseMask, MaskInvert, ClampTexture, _padding0; -> 32B.
// Mirrors _ExecuteBloomPasses.BlurParameters (.cs:320-348). The shader uses DirX/DirY/Width/Height/
// ClampTexture; UseMask/MaskInvert are present (Bloom internal blur leaves them 0) for byte-parity.
struct BloomBlurParams {
  float DirX;          // blur direction px offset X (BlurH: blurOffset, BlurV: 0)
  float DirY;          // blur direction px offset Y (BlurV: blurOffset, BlurH: 0)
  float Width;         // this level's texture width
  float Height;        // this level's texture height
  int UseMask;         // 0 (Bloom internal blur never masks)
  int MaskInvert;      // 0
  int ClampTexture;    // saturate result if >0 (TiXL Clamp port, outer default false -> 0)
  int _padding0;       // pad 28 -> 32
};

// --- Upsample-add composite (Bloom-UpsamplePS.hlsl b0) ---
// HLSL:  float2 InvTargetSize; float2 InvSourceSize; float4 PassColor; float PassIntensity;  (+ pad)
// Mirrors _ExecuteBloomPasses.CompositeParams (.cs:353-372): InvTargetSize@0, InvSourceSize@8,
// PassColor@16, PassIntensity@32, Vector3 __padding@48 -> declared total 64B (16*4). PassColor is
// multiplied in (GlowGradient -> per-level color). We size the host struct to TiXL's declared 64B
// exactly (byte-identical upload) and the Metal shader cbuffer declares the matching 64B layout.
struct BloomCompositeParams {
#ifdef __METAL_VERSION__
  float2 InvTargetSize;   // 1/composite size
  float2 InvSourceSize;   // 1/this-level size
  float4 PassColor;       // per-level gradient color (default white)
#else
  float InvTargetSize[2];
  float InvSourceSize[2];
  float PassColor[4];
#endif
  float PassIntensity;    // overall Intensity * normalized per-level weight (.cs FieldOffset 8*4=32)
  float _pad[7];          // 33..63: PassIntensity(4)@32 + 28 pad -> 64B total (TiXL declared 16*4)
};

enum BloomBinding {
  BLOOM_ThresholdParams = 0,  // constant BloomThresholdParams& (b0) for bloom_bright_fs
  BLOOM_BlurParams = 0,       // constant BloomBlurParams&      (b0) for bloom_blur_fs
  BLOOM_CompositeParams = 0,  // constant BloomCompositeParams& (b0) for bloom_upsample_fs
  // texture(0) = input/low-res, sampler(0) = linear or point; bound directly (no enum).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BloomThresholdParams) == 16, "BloomThresholdParams 16 bytes");
static_assert(sizeof(BloomBlurParams) == 32, "BloomBlurParams 32 bytes");
// 8 floats (InvTargetSize2 + InvSourceSize2 + PassColor4) = 32, + PassIntensity(4) + pad(28) = 64.
// Matches _ExecuteBloomPasses.CompositeParams declared Size=16*4=64 exactly (byte-identical upload).
static_assert(sizeof(BloomCompositeParams) == 64, "BloomCompositeParams 64 bytes (TiXL 16*4)");
#endif
