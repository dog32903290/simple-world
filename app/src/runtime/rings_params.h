// Shared host<->shader params for the TiXL-ported Rings IMAGE FILTER (image/generate/pattern).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/Rings.hlsl (b0 ParamConstants)
// and Rings.cs/.t3 defaults. TiXL authority: Rings.cs (slot declarations) + Rings.t3 (defaults)
// + Rings.hlsl (single-pass kernel: concentric ring pattern with segment/thickness/twist hash
// variation, optional BlendMode composite with upstream image).
//
// b0 ParamConstants layout (verbatim HLSL cbuffer field order, Rings.hlsl lines 4-33):
//   float4 Fill;           offset   0
//   float4 Background;     offset  16
//   float4 Highlight;      offset  32
//   float2 Radius;         offset  48
//   float2 Position;       offset  56
//   float  RingCount;      offset  64
//   float  Feather;        offset  68
//   float  Rotate;         offset  72
//   float  Offset;         offset  76
//   float2 _Segments;      offset  80
//   float2 _Twist;         offset  88
//   float2 _Thickness;     offset  96
//   float2 _Ratio;         offset 104
//   float  _FillRatio;     offset 112
//   float  _HighlightRatio;offset 116
//   float  HighlightSeed;  offset 120
//   float  Distort;        offset 124
//   float  Contrast;       offset 128
//   float  Seed;           offset 132
//   float  BlendMode;      offset 136
//   float  IsTextureValid; offset 140
// Total: 144 bytes (9 × 16-byte registers — exact multiple, no padding needed).
//
// b1 Resolution{TargetWidth, TargetHeight} is framework-injected (host fills from output size);
// bound as a second Metal fragment cbuffer (same pattern as VoronoiCells / ChromaticAbberation).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RingsParams {
  // b0 ParamConstants — field order MUST match Rings.hlsl cbuffer verbatim.
  float FillR, FillG, FillB, FillA;                    // float4 Fill, default (1,1,1,1)
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA; // float4 Background, default (0,0,0,0)
  float HighlightR, HighlightG, HighlightB, HighlightA;    // float4 Highlight, default (1,0,0,1) red
  float RadiusX, RadiusY;                               // float2 Radius, default (0.0, 0.5)
  float PositionX, PositionY;                           // float2 Position, default (0,0)
  float RingCount;    // TiXL Count input (Single), default 0.5; drives ring frequency
  float Feather;      // TiXL Feather (Single), default 0.03333335; ring edge softness
  float Rotate;       // TiXL Rotate (Single), default 0.0; degrees, segment angular offset
  float Offset;       // TiXL Offset (Single), default 0.0; ring phase offset
  float SegmentsX, SegmentsY;   // float2 _Segments, default (20,0)
  float TwistX, TwistY;         // float2 _Twist, default (0,0)
  float ThicknessX, ThicknessY; // float2 _Thickness, default (0.5,0)
  float RatioX, RatioY;         // float2 _Ratio, default (1.05,0)
  float FillRatio;      // TiXL _FillRatio (Single), default 1.0
  float HighlightRatio; // TiXL _HighlightRatio (Single), default 0.0
  float HighlightSeed;  // TiXL HighlightSeed (Int → float), default 0
  float Distort;        // TiXL Distort (Single), default 1.0
  float Contrast;       // TiXL Constrast (typo in .cs), default 1.0
  float Seed;           // TiXL Seed (Int → float), default 0
  float BlendMode;      // TiXL BlendMode (Int → float), default 0 (normal)
  float IsTextureValid; // framework-injected by _ImageFxShaderSetupStatic: 1.0 if upstream wired
};

struct RingsResolution {
  // Mirrors Rings.hlsl b1 Resolution cbuffer (TargetWidth/TargetHeight); host-filled.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 bytes
};

enum RingsBinding {
  RINGS_Params     = 0,  // constant RingsParams& (folds Rings.hlsl b0)
  RINGS_Resolution = 1,  // constant RingsResolution& (Rings.hlsl b1; bound at Metal index 1)
  // texture(0) = inputTexture (Image, optional upstream), sampler(0) = linear+clamp; direct bind.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RingsParams) == 144,
              "RingsParams must be 144 bytes (9x16-byte HLSL cbuffer registers)");
static_assert(sizeof(RingsResolution) == 16, "RingsResolution 16 bytes");
#endif
