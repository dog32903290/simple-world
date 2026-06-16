// Shared host<->shader params for the TiXL-ported RoundedRect IMAGE FILTER (image/generate/basic).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/RoundedRect.hlsl (b0 ParamConstants)
// and RoundedRect.cs/.t3 defaults.
//
// TiXL authority:
//   RoundedRect.cs  — slot declarations, types
//   RoundedRect.t3  — default values
//   RoundedRect.hlsl — cbuffer b0 field order + b1 Resolution
//
// b0 ParamConstants cbuffer trace (RoundedRect.t3 FloatsToBuffer connections → b0 fill order):
//   .t3 connections to slot 4ef6f204 in source order:
//     3b04103f (Color/Vector4Components)      → float4 Fill
//     d2b77680 (StrokeColor/Vector4Components)→ float4 OutlineColor
//     3eade3a0 (Background/Vector4Components) → float4 Background
//     624bc5a4 (Stretch/Vector2Components)    → float2 Stretch
//     17626164 (Position/Vector2Components)   → float2 Center
//     d2401798 (Scale)                        → float  Scale
//     ef3d0313 (Round)                        → float  Round
//     80bd2460 (Stroke)                       → float  Stroke
//     be43e160 (Feather)                      → float  Feather
//     574e7268 (FeatherBias)                  → float  GradientBias
//     202340b4 (Rotate)                       → float  Rotate
//   + IsTextureValid (framework-injected via b55312c4 Image input slot)
//
// HLSL cbuffer b0 layout (verbatim field order):
//   float4 Fill;           offset   0   (Color, TiXL default (1,1,1,1) — white)
//   float4 OutlineColor;   offset  16   (StrokeColor, default (1,1,1,1) — white)
//   float4 Background;     offset  32   (Background, default (0,0,0,0) — transparent)
//   float2 Stretch;        offset  48   (default (1,1))
//   float2 Center;         offset  56   (Position, default (0,0))
//   float  Scale;          offset  64   (default 0.5)
//   float  Round;          offset  68   (default 0.5)
//   float  Stroke;         offset  72   (default 0.0)
//   float  Feather;        offset  76   (default 0.0)
//   float  GradientBias;   offset  80   (= FeatherBias in .cs; default -0.001)
//   float  Rotate;         offset  84   (default 0.0; degrees)
//   float  IsTextureValid; offset  88   (host-injected; 1.0 if Image wired)
//   _pad;                  offset  92   (4 bytes align to 96 = 6 × 16-byte register)
// Total: 92 bytes padded to 96.
//
// BACKWARD-TRACE: No _multiImageFxSetup, no intermediate Multiply/IntToFloat math nodes.
//   RoundedRect uses _ImageFxShaderSetupStatic (bd0b9c5b) — direct float feed, clean leaf.
//   No seam dependency (gradient / asset-texture / multi-image / mip-gen). ✅
//
// b1 Resolution{TargetWidth, TargetHeight} — host-filled from output size; bound at Metal
// fragment cbuffer index 1.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RoundedRectParams {
  // b0 ParamConstants — field order MUST match RoundedRect.hlsl cbuffer verbatim.
  float FillR, FillG, FillB, FillA;          // float4 Fill       (Color),       default (1,1,1,1)
  float OutlineR, OutlineG, OutlineB, OutlineA;// float4 OutlineColor (StrokeColor), default (1,1,1,1)
  float BgR, BgG, BgB, BgA;                  // float4 Background, default (0,0,0,0)
  float StretchX, StretchY;                  // float2 Stretch,   default (1,1)
  float CenterX, CenterY;                    // float2 Center     (Position), default (0,0)
  float Scale;          // TiXL Scale (Single),       default 0.5
  float Round;          // TiXL Round (Single),       default 0.5
  float Stroke;         // TiXL Stroke (Single),      default 0.0
  float Feather;        // TiXL Feather (Single),     default 0.0
  float GradientBias;   // TiXL FeatherBias (Single), default -0.001 [= GradientBias in hlsl]
  float Rotate;         // TiXL Rotate (Single),      default 0.0 degrees
  float IsTextureValid; // host-injected: 1.0 if Image wired, else 0.0
  float _pad;           // align struct to 96 bytes (6 × 16-byte HLSL registers)
};

struct RoundedRectResolution {
  // Mirrors RoundedRect.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8→16 bytes
};

enum RoundedRectBinding {
  RRECT_Params     = 0,  // constant RoundedRectParams& (folds RoundedRect.hlsl b0)
  RRECT_Resolution = 1,  // constant RoundedRectResolution& (b1; Metal index 1)
  // texture(0) = inputTexture (Image, optional upstream), sampler(0) = linear+Repeat.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RoundedRectParams) == 96,
              "RoundedRectParams must be 96 bytes (6 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(RoundedRectResolution) == 16, "RoundedRectResolution 16 bytes");
#endif
