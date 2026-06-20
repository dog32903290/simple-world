// Shared host<->shader params for TiXL-ported Raster IMAGE FILTER/GENERATOR (Phase C leaf).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Raster.hlsl and Raster.cs/.t3.
//
// TiXL authority: Raster.cs (Image/Offset/Rotate/Stretch/Scale/Color/Background/MixOriginal/
//   Resolution/DotSize/LineWidth/LineRatio/Feather/RedToDotSize/GreenToLineWidth/
//   BlueToLineRatio inputs) + Raster.t3 (defaults: Scale=4, DotSize=0.05333333,
//   LineWidth=0.053333342, LineRatio=0.75, Feather=0.02, MixOriginal=1.0, Stretch=(32,32),
//   Offset=(0,0), Rotate=0, Color=(1,1,1,1), Background=(0,0,0,0),
//   RedToDotSize=0, GreenToLineWidth=0, BlueToLineRatio=0,
//   Wrap=Clamp, GenerateMips=true, Resolution=(0,0)=WindowFollow) +
//   Assets/shaders/img/fx/Raster.hlsl (psMain: halftone dot+line raster grid,
//   optional per-channel modulation from input image, alpha composite over input).
//
// b0 ParamConstants order (Raster.hlsl lines 5-20):
//   float4 Fill;              // 16
//   float4 Background;        // 16
//   float2 Size;              // 8
//   float2 Offset;            // 8
//   float ScaleFactor;        // 4
//   float Rotate;             // 4
//   float DotSize;            // 4
//   float LineWidth;          // 4
//   float LineRatio;          // 4
//   float RAffects_DotSize;   // 4
//   float GAffects_LineWidth; // 4
//   float BAffects_LineRatio; // 4
//   float MixOriginal;        // 4
//   float Feather;            // 4
//   Total: 88 bytes -> pad to 96 (16-byte multiple)
//
// b1 Resolution (Raster.hlsl lines 31-34): float TargetWidth, TargetHeight -> 8 bytes -> 16.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RasterParams {
  // TiXL Raster.hlsl cbuffer ParamConstants (b0) — field names and order verbatim.
  float FillR, FillG, FillB, FillA;                // Fill (Vec4), t3 default (1,1,1,1)
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA; // Background (Vec4), t3 default (0,0,0,0)
  float SizeX, SizeY;                               // Size/Stretch (Vec2), t3 default (32,32)
  float OffsetX, OffsetY;                           // Offset (Vec2), t3 default (0,0)
  float ScaleFactor;                                // Scale (float), t3 default 4.0
  float Rotate;                                     // Rotate (float), t3 default 0.0
  float DotSize;                                    // DotSize (float), t3 default 0.05333333
  float LineWidth;                                  // LineWidth (float), t3 default 0.053333342
  float LineRatio;                                  // LineRatio (float), t3 default 0.75
  float RAffects_DotSize;                           // RedToDotSize (float), t3 default 0.0
  float GAffects_LineWidth;                         // GreenToLineWidth (float), t3 default 0.0
  float BAffects_LineRatio;                         // BlueToLineRatio (float), t3 default 0.0
  float MixOriginal;                                // MixOriginal (float), t3 default 1.0
  float Feather;                                    // Feather (float), t3 default 0.02
  float _pad[2];                                    // pad 88 -> 96 (16-byte multiple)
};

struct RasterResolution {
  // Raster.hlsl b1 Resolution cbuffer (TargetWidth/TargetHeight); host-filled from output.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum RasterBinding {
  RASTER_Params     = 0,  // constant RasterParams& (b0)
  RASTER_Resolution = 1,  // constant RasterResolution& (b1)
  // texture(0) = inputTexture (or 1x1 black dummy), sampler(0) = linear+Clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RasterParams) == 96, "RasterParams 96 bytes (16-byte multiple)");
static_assert(sizeof(RasterResolution) == 16, "RasterResolution 16 bytes");
#endif
