// Shared host<->shader params for the TiXL-ported TileableNoise IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/noise/TileableNoise.cs (slot declarations)
//   + TileableNoise.t3 (defaults) + Assets/shaders/img/generate/PerlinNoise2d.hlsl (cbuffer layout).
//
// cbuffer b0 (FloatParams, register b0 in HLSL, [[buffer(0)]] in MSL):
//   Traced from TileableNoise.t3 FloatsToBuffer connections to slot 4ef6f204 (order = connection doc order):
//     Vector4Components(ColorA)  → 4f: ColorAR, ColorAG, ColorAB, ColorAA
//     Vector4Components(ColorB)  → 4f: ColorBR, ColorBG, ColorBB, ColorBA
//     Vector2Components(Offset)  → 2f: OffsetX, OffsetY
//     Scale direct               → 1f: Scale
//     RandomPhase direct         → 1f: Phase   (raw; shader uses p.z = Phase directly)
//     Vector2Components(GainAndBias) → 2f: GainAndBiasX, GainAndBiasY
//     Gain direct                → 1f: Gain       (per-octave amplitude decay, default 0.5)
//     Lacunarity direct          → 1f: Lacunarity  (frequency multiplier, default 2.0)
//     Contrast direct            → 1f: Contrast    (output contrast, default 1.7)
//   = 17 floats = 68 bytes → padded to 80 bytes (5 × 16-byte MSL/HLSL registers).
//
// cbuffer b1 (Resolution, [[buffer(1)]]): TargetWidth, TargetHeight.
//
// cbuffer b2 (IntParams, register b2 in HLSL, [[buffer(2)]] in MSL):
//   Traced from TileableNoise.t3 IntParams connections to slot 86fe6e64:
//     ClampInt(Octaves, min=1, max=10) → int Iterations
//     Detail direct                    → int Detail
//   = 2 ints = 8 bytes → padded to 16 bytes.
//
// TiXL .cs defaults (from TileableNoise.t3):
//   ColorA = (~0,~0,~0,1)   ColorB = (1,1,1,1)
//   Gain = 0.5   Lacunarity = 2.0   RandomPhase = 5.0
//   Offset = (0,0)   Contrast = 1.7   GainAndBias = (0.5,0.5)
//   Scale = 1.0   Resolution = 1024×1024   GenerateMips = false
//   Detail = 1   Octaves = 2   OutputFormat = R16G16B16A16_Float
//
// PORTABILITY CHECK (STEP-0):
//   ① Zero Texture2D inputs — TileableNoise.cs has no Image [Input] slot. Pure generator.
//   ② No gradient/curve-LUT/asset-texture/mip-gen seam.
//   ③ _ImageFxShaderSetupStatic (bd0b9c5b) with direct cbuffer feed. NOT a _multiImageFxSetup compound.
//   ④ No intermediate math nodes in .t3 routing (Vector4/2Components + ClampInt only).
//   ⑤ Tileable Perlin noise: procedural hash33+gradient, no temporal random, no feedback.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TileableNoiseFloatParams {
  // b0 FloatParams — field order MUST match PerlinNoise2d.hlsl cbuffer FloatParams verbatim.
  float ColorAR, ColorAG, ColorAB, ColorAA;  // float4 ColorA, default (~0,~0,~0,1)
  float ColorBR, ColorBG, ColorBB, ColorBA;  // float4 ColorB, default (1,1,1,1)
  float OffsetX, OffsetY;                    // float2 Offset, default (0,0)
  float Scale;        // float Scale,        default 1.0
  float Phase;        // float Phase,        default 5.0 (= RandomPhase raw; shader uses as p.z)
  float GainAndBiasX; // float2 GainAndBias.x, default 0.5
  float GainAndBiasY; // float2 GainAndBias.y, default 0.5
  float Gain;         // float Gain,         default 0.5 (per-octave amplitude decay)
  float Lacunarity;   // float Lacunarity,   default 2.0 (per-octave frequency multiplier)
  float Contrast;     // float Contrast,     default 1.7 (output contrast scalar)
  float _pad[3];      // floats 17-19 → pad 68 → 80 bytes (5 × 16-byte registers)
};

struct TileableNoiseResolution {
  // b1 Resolution: host-filled from output texture size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 → 16 bytes
};

struct TileableNoiseIntParams {
  // b2 IntParams (HLSL register b2, MSL [[buffer(2)]]):
  int Iterations;  // = Octaves, clamped 1..10 by ClampInt in .t3, default 2
  int Detail;      // = Detail, tile repetition scale, default 1
  int _pad[2];     // pad 8 → 16 bytes
};

enum TileableNoiseBinding {
  TILEABLENOISE_FloatParams  = 0,  // constant TileableNoiseFloatParams& ([[buffer(0)]])
  TILEABLENOISE_Resolution   = 1,  // constant TileableNoiseResolution&  ([[buffer(1)]])
  TILEABLENOISE_IntParams    = 2,  // constant TileableNoiseIntParams&   ([[buffer(2)]])
  // No texture input (pure generator). No sampler needed. [fork-no-sampler]
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TileableNoiseFloatParams) == 80,
              "TileableNoiseFloatParams must be 80 bytes (5 × 16-byte cbuffer registers)");
static_assert(sizeof(TileableNoiseResolution) == 16, "TileableNoiseResolution 16 bytes");
static_assert(sizeof(TileableNoiseIntParams) == 16, "TileableNoiseIntParams 16 bytes");
#endif
