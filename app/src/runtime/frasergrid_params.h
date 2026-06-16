// Shared host<->shader params for the TiXL-ported FraserGrid IMAGE GENERATOR (Phase C leaf).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/FraserGrid.hlsl and FraserGrid.cs/.t3.
// TiXL authority: FraserGrid.cs (Image/Fill/FillB/Background/Feather/Size/Offset/Scale/Rotate/
// RotateShapes/ShapeSize/BarWidth/BorderWidth/RowSwift/R|G|BAffects_*/Resolution inputs) +
// FraserGrid.t3 (defaults) + FraserGrid.hlsl (psMain: per-cell diamond-grid Fraser pattern).
//
// b0 ParamConstants order (FraserGrid.hlsl lines 5-23):
//   float4 FillA;         // 16 bytes
//   float4 FillB;         // 16 bytes
//   float4 Background;    // 16 bytes
//   float2 Size;          //  8 bytes
//   float2 Offset;        //  8 bytes
//   float ScaleFactor;    //  4 bytes
//   float Rotate;         //  4 bytes
//   float Feather;        //  4 bytes
//   float RotateShapes;   //  4 bytes
//   float ShapeSize;      //  4 bytes
//   float BarWidth;       //  4 bytes
//   float BorderWidth;    //  4 bytes
//   float RowSwift;       //  4 bytes
//   float RAffects_BarWidth;   //  4 bytes
//   float GAffects_ShapeSize;  //  4 bytes
//   float BAffects_LineRatio;  //  4 bytes
// Layout: 16+16+16+8+8+4+4+4+4+4+4+4+4+4+4+4 = 112 bytes (16-byte multiple, no pad needed).
//
// b1 Resolution (FraserGrid.hlsl lines 26-30): float TargetWidth, TargetHeight -> 8 bytes -> pad 16.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct FraserGridParams {
  // TiXL FraserGrid.hlsl cbuffer ParamConstants (b0) — field names and order verbatim.
  float FillAR, FillAG, FillAB, FillAA;       // FillA (Vec4), TiXL t3 default (0,0,0,1)
  float FillBR, FillBG, FillBB, FillBA;       // FillB (Vec4), TiXL t3 default (1,1,1,1)
  float BgR, BgG, BgB, BgA;                  // Background (Vec4), TiXL t3 default ~(0.675,0.675,0.676,1)
  float SizeX, SizeY;                          // Size (Vec2), TiXL t3 default (32,16)
  float OffsetX, OffsetY;                      // Offset (Vec2), TiXL t3 default (0,0)
  float ScaleFactor;                           // Scale (float), TiXL t3 default 4.0
  float Rotate;                                // Rotate (float), TiXL t3 default 0.0
  float Feather;                               // Feather (float), TiXL t3 default 0.015
  float RotateShapes;                          // RotateShapes (float), TiXL t3 default 45.0
  float ShapeSize;                             // ShapeSize (float), TiXL t3 default 0.22
  float BarWidth;                              // BarWidth (float), TiXL t3 default 0.035
  float BorderWidth;                           // BorderWidth (float), TiXL t3 default 0.06
  float RowSwift;                              // RowSwift (float), TiXL t3 default 0.0
  float RAffects_BarWidth;                     // RAffects_BarWidth (float), TiXL t3 default 0.0
  float GAffects_ShapeSize;                    // GAffects_ShapeSize (float), TiXL t3 default 0.0
  float BAffects_LineRatio;                    // BAffects_LineRatio (float), TiXL t3 default 0.0
  // Layout: 3*16 + 2*8 + 11*4 = 48+16+44 = 108 bytes -> pad to 112 (16-byte multiple).
  float _pad[1];
};

struct FraserGridResolution {
  // FraserGrid.hlsl b1 Resolution cbuffer (TargetWidth/TargetHeight); host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum FraserGridBinding {
  FRASERGRID_Params     = 0,  // constant FraserGridParams& (b0)
  FRASERGRID_Resolution = 1,  // constant FraserGridResolution& (b1, bound at Metal fragment index 1)
  // texture(0) = inputTexture (or 1x1 black dummy), sampler(0) = linear+clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FraserGridParams) == 112, "FraserGridParams 112 bytes (16-byte multiple)");
static_assert(sizeof(FraserGridResolution) == 16, "FraserGridResolution 16 bytes");
#endif
