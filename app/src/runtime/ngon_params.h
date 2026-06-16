// Shared host<->shader params for the TiXL-ported NGon IMAGE FILTER (image/generate/basic).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/NGon.hlsl (b0 ParamConstants)
// and NGon.cs/.t3 defaults.
//
// TiXL authority:
//   NGon.cs  — slot declarations, types
//   NGon.t3  — default values
//   NGon.hlsl — cbuffer b0 field order (lines 4-18) + b1 Resolution (lines 21-25)
//
// b0 ParamConstants layout (verbatim HLSL cbuffer field order, NGon.hlsl lines 4-18):
//   float4 Fill;           offset   0   (default (1,1,1,1) — white)
//   float4 Background;     offset  16   (default (0,0,0,0) — transparent)
//   float2 Position;       offset  32   (default (0,0))
//   float  Round;          offset  40   (default 0.0)
//   float  Feather;        offset  44   (default 0.05)
//   float  GradientBias;   offset  48   (= FeatherBias in .cs; default 0.0)
//   float  Rotate;         offset  52   (default -90.0; degrees)
//   float  Sides;          offset  56   (default 3.0; triangle)
//   float  Radius;         offset  60   (fills register 3 [48-63])
//   float  Curvature;      offset  64   (default 0.0)
//   float  Blades;         offset  68   (default 0.0)
//   float  BlendMode;      offset  72   (Int→float via IntToFloat in .t3; default 0)
//   float  IsTextureValid; offset  76   (host-injected; fills register 4 [64-79])
// Total: 80 bytes (5 × 16-byte registers — exact multiple, no cross-register straddle).
// No padding needed: 20 floats × 4 = 80 bytes.
//
// b1 Resolution{TargetWidth, TargetHeight} — host-filled from output size; bound at Metal
// fragment cbuffer index 1 (same pattern as Rings / VoronoiCells / SinForm).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct NGonParams {
  // b0 ParamConstants — field order MUST match NGon.hlsl cbuffer verbatim.
  float FillR, FillG, FillB, FillA;              // float4 Fill,       default (1,1,1,1)
  float BgR,   BgG,   BgB,   BgA;               // float4 Background, default (0,0,0,0)
  float PositionX, PositionY;                    // float2 Position,   default (0,0)
  float Round;           // TiXL Round (Single),       default 0.0
  float Feather;         // TiXL Feather (Single),     default 0.05
  float GradientBias;    // TiXL FeatherBias (Single), default 0.0  [= GradientBias in hlsl]
  float Rotate;          // TiXL Rotate (Single),      default -90.0 degrees
  float Sides;           // TiXL Sides (Single),       default 3.0
  float Radius;          // TiXL Radius (Single),      default 0.25
  float Curvature;       // TiXL Curvature (Single),   default 0.0
  float Blades;          // TiXL Blades (Single),      default 0.0
  float BlendMode;       // TiXL BlendMode (Int→float), default 0 (normal)
  float IsTextureValid;  // host-injected: 1.0 if Image wired, else 0.0
  // No padding needed: 20 floats × 4 = 80 bytes (5 × 16-byte HLSL registers, exact multiple).
};

struct NGonResolution {
  // Mirrors NGon.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8→16 bytes
};

enum NGonBinding {
  NGON_Params     = 0,  // constant NGonParams& (folds NGon.hlsl b0)
  NGON_Resolution = 1,  // constant NGonResolution& (NGon.hlsl b1; Metal index 1)
  // texture(0) = inputTexture (Image, optional upstream), sampler(0) = linear+Repeat.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(NGonParams) == 80,
              "NGonParams must be 80 bytes (5 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(NGonResolution) == 16, "NGonResolution 16 bytes");
#endif
