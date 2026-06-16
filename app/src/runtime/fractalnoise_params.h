// Shared host<->shader params for the TiXL-ported FractalNoise IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/noise/FractalNoise.cs (slot declarations, types)
//   + FractalNoise.t3 (defaults) + Assets/shaders/img/generate/FractalNoise.hlsl (cbuffer layout).
//
// cbuffer b0 order traced from FractalNoise.t3 FloatsToBuffer connections (verbatim HLSL
// cbuffer ParamConstants field order, FractalNoise.hlsl lines 5-22):
//   float4 ColorA;         offset  0  (default (1e-6, 1e-6, 1e-6, 1.0) — near-black)
//   float4 ColorB;         offset  4  (default (1.0, 1.0, 1.0, 1.0) — white)
//   float2 Offset;         offset  8  (default (0,0))
//   float2 Stretch;        offset 10  (default (2,2))
//   float  Scale;          offset 12  (default 1.0)
//   float  Phase;          offset 13  (= RandomPhase/10 in shader; .cs default 5.0)
//   float  Iterations;     offset 14  (IntToFloat in .t3; .cs default 2; clamped 1..5 in shader)
//   float  __padding;      offset 15  (explicit pad child in .t3, always 0.0)
//   float2 GainAndBias;    offset 16  (default (0.5, 0.5) — neutral bias/gain)
//   float2 WarpOffsetXY;   offset 18  (= WarpXY in .cs; default (0,0))
//   float  WarpOffsetZ;    offset 20  (= WarpZ in .cs; default 0.0)
//   [3 pad floats to reach 24 = 6 × 16-byte registers]
//   Total: 24 floats × 4 = 96 bytes (6 × 16-byte MSL/HLSL registers, exact multiple).
//
// b1 = Resolution: TargetWidth/TargetHeight (16 bytes, same pattern as Rings/CheckerBoard).
//
// PORTABILITY CHECK (STEP-0, PASSED):
//   ① Zero Texture2D inputs — pure generator (no Image [Input] in FractalNoise.cs).
//   ② No gradient/curve-LUT/asset-texture/mip seam.
//   ③ _ImageFxShaderSetupStatic class; direct cbuffer feed via FloatsToBuffer.
//   ④ Noise is purely procedural: hash33() spatial hash with MOD3 float constants.
//      No temporal random, no feedback, no external noise texture asset.
//
// .t3 routing: backward-trace of connections to FloatsToBuffer slot 4ef6f204:
//   Vector4Components (ColorA) → 4f | Vector4Components (ColorB) → 4f |
//   Vector2Components (Offset) → 2f | Vector2Components (Stretch) → 2f |
//   Scale direct → 1f | RandomPhase direct → 1f |
//   IntToFloat(Iterations) → 1f | __padding child (zero float) → 1f |
//   Vector2Components (GainAndBias) → 2f | Vector2Components (WarpXY) → 2f |
//   WarpZ direct → 1f.  Matches hlsl field order exactly. NO intermediate math nodes.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct FractalNoiseParams {
  // b0 ParamConstants — field order MUST match FractalNoise.hlsl cbuffer verbatim.
  float ColorAR, ColorAG, ColorAB, ColorAA;  // float4 ColorA, default (~0,~0,~0,1)
  float ColorBR, ColorBG, ColorBB, ColorBA;  // float4 ColorB, default (1,1,1,1)
  float OffsetX, OffsetY;                    // float2 Offset,  default (0,0)
  float StretchX, StretchY;                  // float2 Stretch, default (2,2)
  float Scale;       // float Scale,      default 1.0
  float Phase;       // float Phase,      default 5.0 (RandomPhase in .cs; raw value, shader divides by 10)
  float Iterations;  // float Iterations, default 2.0 (IntToFloat; shader clamps 1..5)
  float _padding;    // float __padding,  always 0.0 (explicit pad in .t3)
  float GainX;       // float2 GainAndBias.x, default 0.5 (Gain)
  float BiasY;       // float2 GainAndBias.y, default 0.5 (Bias)
  float WarpX, WarpY;  // float2 WarpOffsetXY, default (0,0)
  float WarpZ;         // float  WarpOffsetZ,  default 0.0
  // [5].yzw — pad to 24 floats (6 × 16-byte registers = 96 bytes)
  float _pad[3];       // pad 21 → 24 floats
};

struct FractalNoiseResolution {
  // Mirrors FractalNoise.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 → 16 bytes
};

enum FractalNoiseBinding {
  FRACTALNOISE_Params     = 0,  // constant FractalNoiseParams& (b0)
  FRACTALNOISE_Resolution = 1,  // constant FractalNoiseResolution& (b1; Metal index 1)
  // No texture input (pure generator). No sampler needed.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FractalNoiseParams) == 96,
              "FractalNoiseParams must be 96 bytes (6 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(FractalNoiseResolution) == 16, "FractalNoiseResolution 16 bytes");
#endif
