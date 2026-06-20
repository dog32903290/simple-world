// Shared host<->shader params for the TiXL-ported WorleyNoise IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/noise/WorleyNoise.cs (slot declarations) +
//   WorleyNoise.t3 (defaults: Scale=5, Stretch=(1,1), Offset=(0,0), Phase=5.0,
//     Randomness=12.6, Clamping=(0,1), GainAndBias=(0.5,0.5), Method=0,
//     ColorA=(1,1,1,1), ColorB=(0,0,0,1), TextureBlend=1.0,
//     Resolution=512×512, GenerateMips=false) +
//   Assets/shaders/img/generate/WorleyNoise.hlsl (cbuffer layout, Worley F1/F2 variants).
//
// cbuffer b0 order backward-traced from WorleyNoise.t3 connections to FloatsToBuffer slot 4ef6f204:
//   Vector4Components(ColorA)   → 4f  offsets  0-3
//   Vector4Components(ColorB)   → 4f  offsets  4-7
//   Vector2Components(Offset)   → 2f  offsets  8-9
//   Vector2Components(Stretch)  → 2f  offsets 10-11
//   Scale direct                → 1f  offset  12
//   Phase direct                → 1f  offset  13
//   Vector2Components(Clamping) → 2f  offsets 14-15
//   Vector2Components(GainAndBias) → 2f offsets 16-17
//   IntToFloat(Method)          → 1f  offset  18
//   Randomness direct           → 1f  offset  19
//   TextureBlend direct         → 1f  offset  20  (FxTextureBlend in HLSL)
//   IsTextureValid              → 1f  offset  21  (auto-set by _FxShaderSetup; we pass 0.0 = generator)
//   [pad 2 floats]                   offsets 22-23 (reach 24 = 6×16-byte registers)
//
// b1 = Resolution: TargetWidth/TargetHeight (same pattern as FractalNoise/Rings/CheckerBoard).
//
// PORTABILITY CHECK (STEP-0, PASSED):
//   ① Optional Texture2D input (Image, default null) — generator + optional overlay.
//      No multi-image seam required. We bind 1×1 dummy, set IsTextureValid=0.0.
//   ② _ImageFxShaderSetupStatic class (not _multiImageFxSetup compound).
//   ③ .t3 backward-trace: zero math-node intermediates between param slots and FloatsToBuffer
//      (only unwrappers: Vector4Components, Vector2Components, IntToFloat). Direct cbuffer feed.
//   ④ No gradient/curve-LUT/asset-texture/feedback/sim-state dependency.
//   ⑤ Noise is purely procedural: hash22 with unsigned integer multiply+XOR chain, no external asset.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct WorleyNoiseParams {
  // b0 ParamConstants — field order MUST match WorleyNoise.hlsl cbuffer verbatim.
  float ColorAR, ColorAG, ColorAB, ColorAA;   // float4 ColorA,  default (1,1,1,1)
  float ColorBR, ColorBG, ColorBB, ColorBA;   // float4 ColorB,  default (0,0,0,1)
  float OffsetX, OffsetY;                     // float2 Offset,  default (0,0)
  float StretchX, StretchY;                   // float2 Stretch, default (1,1)
  float Scale;                                // float Scale,    default 5.0
  float Phase;                                // float Phase,    default 5.0
  float ClampingX, ClampingY;                 // float2 Clamping, default (0,1)
  float GainX, BiasY;                         // float2 GainAndBias, default (0.5,0.5)
  float Method;                               // float Method (IntToFloat), default 0.0 (Worley_F1)
  float Randomness;                           // float Randomness, default 12.6
  float FxTextureBlend;                       // float FxTextureBlend (TextureBlend), default 1.0
  float IsTextureValid;                       // float IsTextureValid (auto); 0.0 = generator (no tex)
  float _pad[2];                              // pad offsets 22-23 → 24 floats = 96 bytes
};

struct WorleyNoiseResolution {
  // Mirrors WorleyNoise.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad to 16 bytes
};

enum WorleyNoiseBinding {
  WORLEYNOISE_Params     = 0,  // constant WorleyNoiseParams& (b0)
  WORLEYNOISE_Resolution = 1,  // constant WorleyNoiseResolution& (b1)
  // t0 = input texture (dummy 1×1 when no input wired; shader branches on IsTextureValid<0.5)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(WorleyNoiseParams) == 96,
              "WorleyNoiseParams must be 96 bytes (6 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(WorleyNoiseResolution) == 16, "WorleyNoiseResolution 16 bytes");
#endif
