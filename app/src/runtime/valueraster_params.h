// Shared host<->shader params for TiXL-ported ValueRaster IMAGE GENERATOR (Phase C leaf).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ValueRaster.hlsl and
// ValueRaster.cs/.t3.
// TiXL authority: ValueRaster.cs (Image/Color/Background/MixOriginal/Resolution/RangeX/RangeY/
//   MajorLineWidth/MinorLineWidth/Density inputs) + ValueRaster.t3 (defaults) +
//   ValueRaster.hlsl (psMain: adaptive raster grid with log10 decade stepping, AA smoothstep,
//   color temperature gradient by p.y, alpha-composite over optional input orgColor).
//
// b0 ParamConstants order (ValueRaster.hlsl lines 5-18):
//   float4 LineColor;        // 16
//   float4 BackgroundColor;  // 16
//   float2 RangeX;           // 8
//   float2 RangeY;           // 8
//   float MajorLineWidth;    // 4
//   float MinorLineWidth;    // 4
//   float2 Density;          // 8
//   float MixOriginal;       // 4
//   Total: 68 bytes -> pad to 80 (16-byte multiple)
//
// b1 Resolution (ValueRaster.hlsl lines 22-25): float TargetWidth, TargetHeight -> 8 bytes -> 16.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ValueRasterParams {
  // TiXL ValueRaster.hlsl cbuffer ParamConstants (b0) — field names and order verbatim.
  float LineColorR, LineColorG, LineColorB, LineColorA;         // LineColor (Vec4), t3 default (1,1,1,0.695)
  float BackgroundColorR, BackgroundColorG, BackgroundColorB, BackgroundColorA; // BackgroundColor (Vec4), t3 default (0,0,0,0)
  float RangeXMin, RangeXMax;                                   // RangeX (Vec2), t3 default (0,1)
  float RangeYMin, RangeYMax;                                   // RangeY (Vec2), t3 default (0,1)
  float MajorLineWidth;                                         // float, t3 default 1.0
  float MinorLineWidth;                                         // float, t3 default 0.25
  float DensityX, DensityY;                                     // Density (Vec2), t3 default (1000,1000)
  float MixOriginal;                                            // float, t3 default 1.0
  float _pad[3];                                                // pad 68 -> 80 (16-byte multiple)
};

struct ValueRasterResolution {
  // ValueRaster.hlsl b1 Resolution cbuffer (TargetWidth/TargetHeight); host-filled from output.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum ValueRasterBinding {
  VALUERASTER_Params     = 0,  // constant ValueRasterParams& (b0)
  VALUERASTER_Resolution = 1,  // constant ValueRasterResolution& (b1)
  // texture(0) = inputTexture (or 1x1 black dummy), sampler(0) = linear+Clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ValueRasterParams) == 80, "ValueRasterParams 80 bytes (16-byte multiple)");
static_assert(sizeof(ValueRasterResolution) == 16, "ValueRasterResolution 16 bytes");
#endif
