// Shared host<->shader params for the TiXL-ported TimeDisplace fx (image/fx/distort/TimeDisplace).
// TimeDisplace samples a persistent N-slice history RING (the KeepInTextureArray ring, owned internally
// here) per-pixel: the DisplaceMap's brightness picks how many frames BACK each pixel reaches, so moving
// image regions smear across TIME (not space). The core is TimeDisplace.hlsl psMain (line refs below).
//
// TiXL authority: external/tixl Operators/Lib/Assets/shaders/img/fx/TimeDisplace.hlsl:37-48
//   float4 rgba = DisplaceMap.SampleLevel(uv);                                          (.hlsl:43)
//   int sliceOffset = (int)floor(((rgba.r+rgba.g+rgba.b)/3.0) * DisplaceAmount + 0.5);  (.hlsl:44)
//   int slice = (SliceIndex + sliceOffset) % ArrayLength;                               (.hlsl:46)
//   return Image.SampleLevel(float3(uv, slice));                                        (.hlsl:47)
// Image is a Texture2DArray (the ring); DisplaceMap a normal Texture2D. HLSL→MSL: SampleLevel→
// sample(...,level(0)); the array sample takes float2 uv + uint slice. No matrix, so no mul()→v* rule.
//
// The .hlsl declares Twist/Shade/DisplaceMode/UseRGSSMultiSampling too, but psMain (the only entry) does
// NOT read them — they are wired in the .t3 compound's OTHER (dropped) machinery. We port ONLY psMain's
// load-bearing math: DisplaceAmount, SliceIndex, ArrayLength. Those three ARE the displacement.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TimeDisplaceParams {
  float DisplaceAmount;  // .hlsl:44 — brightness * this = the per-pixel slice offset (frames back)
  int SliceIndex;        // .hlsl:46 — the base read slice (the "current" head of the ring)
  int ArrayLength;       // .hlsl:46 — N (the ring's slice count) for the modulo
  int _pad;              // 16-byte multiple
};

enum TimeDisplaceBinding {
  TIMEDISPLACE_Params = 0,  // constant TimeDisplaceParams& (b0)
  // texture(0) = the N-slice history ring (texture2d_array), texture(1) = DisplaceMap; sampler(0) linear.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TimeDisplaceParams) == 16, "TimeDisplaceParams 16 bytes (16-byte multiple)");
#endif
