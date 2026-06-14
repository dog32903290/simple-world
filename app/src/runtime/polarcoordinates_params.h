// Shared host<->shader params for the TiXL-ported PolarCoordinates IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/PolarCoordinates.hlsl and
// PolarCoordinates.cs/.t3. TiXL authority: PolarCoordinates.cs (Image/Center/Radius/RadialBias/
// RadialOffset/Twist/Stretch/Resolution/Mode inputs, Modes enum {Cartesian2Polar,Polar2Cartesian})
// + PolarCoordinates.t3 (defaults Center=(0,0), Radius=1, RadialBias=1, RadialOffset=0, Twist=0,
// Stretch=(1,1), Mode=0) + PolarCoordinates.hlsl (the single-pass bidirectional remap kernel).
//
// The .hlsl ParamConstants cbuffer (b0) is laid out EXACTLY in this order (verbatim):
//   float2 Center; float Radius; float Mode; float RadialBias; float RadialOffset; float Twist;
//   float __padding; float2 Stretch;
// We mirror that field order so a single setFragmentBytes matches the HLSL register(b0) layout.
//
// Resolution cbuffer (b1) holds TargetWidth/TargetHeight — needed by the .hlsl for the aspect
// ratio (`aspectRatio = TargetWidth/TargetHeight`). The host fills it from c.output->width/height.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PolarCoordParams {
  // TiXL PolarCoordinates.hlsl cbuffer (b0), field order verbatim.
  float Center[2];     // TiXL Center (Vector2), default (0,0); polar/cartesian recentering
  float Radius;        // TiXL Radius (Single), default 1.0; radial scale
  float Mode;          // TiXL Mode (int enum as float): <0.5 Cartesian2Polar, else Polar2Cartesian
  float RadialBias;    // TiXL RadialBias (Single), default 1.0; pow() exponent on radius
  float RadialOffset;  // TiXL RadialOffset (Single), default 0.0; radial shift
  float Twist;         // TiXL Twist (Single), default 0.0; angular shear w.r.t. radius
  float __padding;     // TiXL __padding (verbatim — keeps Stretch on a float2 boundary)
  float Stretch[2];    // TiXL Stretch (Vector2), default (1,1); per-axis polar scale
};

struct PolarCoordResolution {
  // TiXL PolarCoordinates.hlsl Resolution cbuffer (b1): TargetWidth, TargetHeight.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum PolarCoordBinding {
  POLARCOORD_Params     = 0,  // constant PolarCoordParams& (b0)
  POLARCOORD_Resolution = 1,  // constant PolarCoordResolution& (b1)
  // texture(0) = Image, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
// 10 floats laid out as a flat scalar sequence (Center[2], Radius, Mode, RadialBias, RadialOffset,
// Twist, __padding, Stretch[2]) = byte offsets 0,4,8,12,16,20,24,28,32,36 = 40 bytes. Because we
// use flat `float[2]` (not float2/packed vectors) on BOTH host and Metal sides, the byte offsets are
// purely sequential and match the HLSL cbuffer's offsets exactly: HLSL packs Center+Radius+Mode in
// row0 (0..15), RadialBias+RadialOffset+Twist+__padding in row1 (16..31), Stretch in row2 (32..39).
// No straddle occurs, so the flat layout is byte-identical to the HLSL register(b0). The Metal
// shader reads P.Center[0], P.Stretch[1], etc. (NOT float2) to keep the same flat layout.
static_assert(sizeof(PolarCoordParams) == 40, "PolarCoordParams 40 bytes (10 floats)");
static_assert(sizeof(PolarCoordResolution) == 16, "PolarCoordResolution 16 bytes");
#endif
