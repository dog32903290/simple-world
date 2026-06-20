// Shared host<->shader params for the TiXL-ported MunchingSquares2 IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/MunchingSquares2.cs (slot declarations) +
// MunchingSquares2.t3 (defaults) + Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl
// (single-pass kernel: XOR/bitwise munching squares with optional Image composite).
//
// b0 ParamConstants layout (verbatim HLSL cbuffer field order, MunchingSquares.hlsl lines 6-19):
//   float4 Black;          offset   0  — ShadowColor  (default (0,0,0,1))
//   float4 White;          offset  16  — HighlightColor (default (1,1,1,1))
//   float4 GrayScaleWeights; offset 32 — (default (0.2126,0.7152,0.0722,0.0))
//   float2 GainAndBias;    offset  48  — (default (0.5, 0.5))
//   float2 Stretch;        offset  56  — (default (1,1))
//   float2 Offset;         offset  64  — (default (0,0))
//   float  Scale;          offset  72  — (default 4.0)
//   float  IterationFx;    offset  76  — (default 0.0)
//   float  IsTextureValid; offset  80  — injected host-side (1.0 if Image wired, else 0.0)
//   float  _pad[3];        offset  84  — pad 84 -> 96 bytes (6 × 16-byte registers)
// Total b0: 96 bytes.
//
// b1 Resolution: TargetWidth/TargetHeight (8 bytes, padded to 16).
//
// b2 Params (int cbuffer — register(b2) in HLSL, fragment buffer index 2 in Metal):
//   int Method;    offset  0  — enum Methods {Classic=0, Patterns=1, Or=2, Multiply=3, Chaos=4}
//   int Iteration; offset  4  — TiXL Iterations input (Int), default 10
//   int BlendMode; offset  8  — SharedEnums.RgbBlendModes, default 0 (normal)
//   int _pad;      offset 12  — pad to 16 bytes
// Total b2: 16 bytes.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct MunchingSquares2Params {
  // b0 ParamConstants — field order MUST match MunchingSquares.hlsl cbuffer verbatim.
  float BlackR, BlackG, BlackB, BlackA;       // float4 Black  (ShadowColor), default (0,0,0,1)
  float WhiteR, WhiteG, WhiteB, WhiteA;       // float4 White  (HighlightColor), default (1,1,1,1)
  float GrayR, GrayG, GrayB, GrayA;           // float4 GrayScaleWeights, default (0.2126,0.7152,0.0722,0.0)
  float GainAndBiasX, GainAndBiasY;           // float2 GainAndBias, default (0.5, 0.5)
  float StretchX, StretchY;                   // float2 Stretch, default (1,1)
  float OffsetX, OffsetY;                     // float2 Offset, default (0,0)
  float Scale;                                 // float Scale, default 4.0
  float IterationFx;                           // float IterationFx, default 0.0
  float IsTextureValid;                        // injected host-side: 1.0 if Image wired, else 0.0
  float _pad[3];                               // pad 84 -> 96 bytes (6 × 16)
};

struct MunchingSquares2Resolution {
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 bytes
};

struct MunchingSquares2IntParams {
  // b2 Params (register b2 in HLSL): integer cbuffer.
  // Uses ifdef to match Metal (int) vs host (int32_t) type idiom (mirrors simnoiseoffset_params.h).
#ifdef __METAL_VERSION__
  int Method;     // Methods enum: Classic=0, Patterns=1, Or=2, Multiply=3, Chaos=4; default 0
  int Iteration;  // TiXL Iterations Int input, default 10
  int BlendMode;  // SharedEnums.RgbBlendModes, default 0 (normal blend)
  int _pad;       // pad to 16 bytes
#else
  int32_t Method;
  int32_t Iteration;
  int32_t BlendMode;
  int32_t _pad;
#endif
};

enum MunchingSquares2Binding {
  MUNCHINGSQUARES2_Params      = 0,  // constant MunchingSquares2Params& (b0)
  MUNCHINGSQUARES2_Resolution  = 1,  // constant MunchingSquares2Resolution& (b1)
  MUNCHINGSQUARES2_IntParams   = 2,  // constant MunchingSquares2IntParams& (b2)
  // texture(0) = Image (optional upstream), sampler(0) = linear+MirrorClampToEdge
};

#ifndef __METAL_VERSION__
static_assert(sizeof(MunchingSquares2Params) == 96,
              "MunchingSquares2Params must be 96 bytes (6 × 16-byte registers)");
static_assert(sizeof(MunchingSquares2Resolution) == 16,
              "MunchingSquares2Resolution must be 16 bytes");
static_assert(sizeof(MunchingSquares2IntParams) == 16,
              "MunchingSquares2IntParams must be 16 bytes");
#endif
