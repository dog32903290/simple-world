// Shared host<->shader params for the TiXL-ported Displace IMAGE FILTER (lane D2, image/fx/distort).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Displace.hlsl. Displace warps `Image`
// by reading a `DisplaceMap` texture: the second image filter and the FIRST op with TWO Texture2D
// inputs (Image + DisplaceMap). The HLSL spreads its params over three cbuffers (ParamConstants b0,
// Resolution b1, IntParameters b2); we pack them into ONE all-scalar struct (particle_params.h
// discipline — no Vector2, no int next to floats forcing host/shader layout drift).
//
// TiXL .t3 defaults (Displace.t3): Displacement 0, DisplacementOffset 0, Twist 0, Shade 0,
// SampleRadius 1, DisplaceMapOffset (0,0), DisplaceMode 0 (IntensityGradient), RGSS_4xAA false.
// The op fills TargetWidth/Height from the output texture; DisplaceMode/UseRGSS are floats here and
// reconstructed to the HLSL int compares (DisplaceMode < 0.5 etc.) verbatim.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct DisplaceParams {
  // ParamConstants (HLSL b0), same order:
  float DisplaceAmount;   // TiXL Displacement (Single), default 0; warp strength (×len×10 in shader)
  float DisplaceOffset;   // TiXL DisplacementOffset (Single), default 0; constant warp added to len term
  float Twist;            // TiXL Twist (Single, degrees), default 0; rotates the displacement direction
  float Shade;            // TiXL Shade (Single), default 0; darkens by displacement length (×100)
  float DisplaceMapOffsetX, DisplaceMapOffsetY;  // TiXL DisplaceMapOffset (Vector2), default (0,0)
  float SampleRadius;     // TiXL SampleRadius (Single), default 1; gradient finite-difference reach (texels)
  // Resolution (HLSL b1): filled by the op from the output texture (image aspect correction).
  float TargetWidth, TargetHeight;
  // IntParameters (HLSL b2): kept as floats (host scalar discipline), compared like the HLSL ints.
  float DisplaceMode;          // 0 IntensityGradient / 1 Intensity / 2 NormalMap / 3 SignedNormalMap
  float UseRGSSMultiSampling;  // TiXL RGSS_4xAA (bool), default 0; 4x rotated-grid supersample
  float _pad0;                 // pad 44 -> 48 (11 floats + 1 = 12, 16-byte multiple)
};

enum DisplaceBinding {
  DISPLACE_Params = 0,  // constant DisplaceParams& (b0)
  // texture(0) = Image, texture(1) = DisplaceMap, sampler(0) = linear; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DisplaceParams) == 48, "DisplaceParams 48 bytes (16-byte multiple)");
#endif
