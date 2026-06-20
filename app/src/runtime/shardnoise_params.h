// Shared host<->shader params for the TiXL-ported ShardNoise IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/noise/ShardNoise.cs (slot declarations, types) +
//   ShardNoise.t3 (defaults: ColorA=(1e-6,1e-6,1e-6,1), ColorB=(1,1,1,1), Direction=(0,0),
//   Stretch=(2,2), Scale=10, Sharpness=1.0, Phase=0.0, Rate=2.0, Method=0,
//   GainAndBias=(0.5,0.5), Offset=(0,0), Resolution=256×256, GenerateMips=false) +
//   Assets/shaders/img/generate/ShardNoise.hlsl (shard_noise 3D Voronoi-style noise,
//   3×3×3 cell iteration with exp2-weighted tanh approximation).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader.
//
// STEP-0 portability check (PASSED):
//   ① Zero Texture2D inputs — ShardNoise.cs has NO Image [Input] slot. Pure generator.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ _ImageFxShaderSetupStatic with direct cbuffer feed. NOT a _multiImageFxSetup compound.
//   ④ STEP-0 backward-trace of ShardNoise.t3 connections to FloatsToBuffer slot 4ef6f204,
//      in document order:
//        Vector4Components(ColorA) → 4f | Vector4Components(ColorB) → 4f |
//        Vector2Components(Direction) → 2f | Vector2Components(Stretch) → 2f |
//        Scale direct → 1f | Sharpen direct → 1f | Phase direct → 1f | Rate direct → 1f |
//        IntToFloat(Method) → 1f | Vector2Components(GainAndBias) → 2f |
//        Vector2Components(Offset) → 2f | Value(0) explicit pad → 1f.
//      Total: 22 floats fed from .t3. HLSL cbuffer declares 21 named floats + compiler pad.
//      The explicit Value(0) child maps to the first compiler-pad slot after float2 Offset.
//      Pad to 24 floats = 96 bytes (6 × 16-byte MSL/HLSL registers).
//   ⑤ Noise is purely procedural: 3D spatial hash, 3×3×3 cell iteration. No temporal random,
//      no cross-frame feedback, no external noise texture asset. Fully deterministic.
//
// cbuffer b0 order (ShardNoise.hlsl ParamConstants, verbatim):
//   float4 ColorA;       offset 0-3   (default (1e-6, 1e-6, 1e-6, 1.0))
//   float4 ColorB;       offset 4-7   (default (1.0, 1.0, 1.0, 1.0))
//   float2 Direction;    offset 8-9   (default (0,0))
//   float2 Stretch;      offset 10-11 (default (2,2))
//   float  Scale;        offset 12    (default 10.0)
//   float  Sharpness;    offset 13    (default 1.0; shader multiplies by 128)
//   float  Phase;        offset 14    (default 0.0)
//   float  Rate;         offset 15    (default 2.0; used as Phase*0.05*Rate for z)
//   float  Method;       offset 16    (default 0; IntToFloat; 0=Cubism/1=Cubism×Oct/2=Oct)
//   float2 GainAndBias;  offset 17-18 (default (0.5,0.5))
//   float2 Offset;       offset 19-20 (default (0,0))
//   float  _pad;         offset 21    (explicit Value(0) child in .t3; HLSL compiler pad)
//   [pad to 24 floats]
//
// b1 = Resolution: TargetWidth/TargetHeight (16 bytes, same pattern as FractalNoise).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ShardNoiseParams {
  // b0 ParamConstants — field order MUST match ShardNoise.hlsl cbuffer verbatim.
  float ColorAR, ColorAG, ColorAB, ColorAA;  // float4 ColorA, default (1e-6, 1e-6, 1e-6, 1)
  float ColorBR, ColorBG, ColorBB, ColorBA;  // float4 ColorB, default (1, 1, 1, 1)
  float DirectionX, DirectionY;              // float2 Direction, default (0,0)
  float StretchX, StretchY;                  // float2 Stretch, default (2,2)
  float Scale;      // float Scale,     default 10.0
  float Sharpness;  // float Sharpness, default 1.0 (shader: _sharpness = Sharpness * 128)
  float Phase;      // float Phase,     default 0.0
  float Rate;       // float Rate,      default 2.0
  float Method;     // float Method,    default 0.0 (IntToFloat in .t3)
  float GainX;      // float2 GainAndBias.x, default 0.5 (Gain)
  float BiasY;      // float2 GainAndBias.y, default 0.5 (Bias)
  float OffsetX, OffsetY;  // float2 Offset, default (0,0)
  float _pad;              // explicit Value(0) child in .t3 / HLSL compiler pad slot 21
  // [5].yzw — pad to 24 floats (6 × 16-byte registers = 96 bytes)
  float _pad2[2];
};

struct ShardNoiseResolution {
  // Mirrors ShardNoise.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 → 16 bytes
};

enum ShardNoiseBinding {
  SHARDNOISE_Params     = 0,  // constant ShardNoiseParams& (b0)
  SHARDNOISE_Resolution = 1,  // constant ShardNoiseResolution& (b1; Metal index 1)
  // No texture input (pure generator). No sampler needed.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ShardNoiseParams) == 96,
              "ShardNoiseParams must be 96 bytes (6 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(ShardNoiseResolution) == 16, "ShardNoiseResolution 16 bytes");
#endif
