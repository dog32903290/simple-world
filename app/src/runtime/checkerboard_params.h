// Shared host<->shader params for the TiXL-ported CheckerBoard IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/basic/CheckerBoard.cs (ColorA/ColorB/Stretch/
// Scale/UseAspectRatio/Offset/Resolution/GenerateMips) + CheckerBoard.t3 (defaults) +
// Assets/shaders/img/generate/CheckerBoard.hlsl (the single-pass kernel: UV-based checkerboard
// pattern, mod/floor hard edges, lerp between ColorA and ColorB).
//
// cbuffer b0 order traced from CheckerBoard.t3 FloatsToBuffer connection order (target slot
// 4ef6f204-1894-4b0a-bb2d-8b5ecbad4040, in document order):
//   ColorA(4f) | ColorB(4f) | Size/Stretch(2f) | UseAspectRatio(1f boolToFloat) | Scale(1f)
//   | Offset(2f)
// Matches CheckerBoard.hlsl cbuffer ParamConstants exactly (ColorA, ColorB, Size, UseAspectRatio,
// Scale, Offset). 56 bytes -> padded to 64 (16-byte multiple).
//
// cbuffer b1 = Resolution: TargetWidth/TargetHeight used for aspect ratio correction in the shader.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CheckerBoardParams {
  // TiXL CheckerBoard.hlsl cbuffer ParamConstants (b0) field order:
  float ColorAR, ColorAG, ColorAB, ColorAA;  // ColorA (Vec4), default ~(0.202,0.202,0.202,1)
  float ColorBR, ColorBG, ColorBB, ColorBA;  // ColorB (Vec4), default ~(0.121,0.121,0.121,1)
  float SizeX, SizeY;                         // Stretch (Vec2 -> "Size" in shader), default (1,1)
  float UseAspectRatio;                        // UseAspectRatio (bool -> BoolToFloat), default 1.0
  float Scale;                                 // Scale (Single), default 1.0
  float OffsetX, OffsetY;                      // Offset (Vec2), default (0,0)
  float _pad[2];                               // pad 56 -> 64 (16-byte multiple)
};

struct CheckerBoardResolution {
  // TiXL CheckerBoard.hlsl Resolution cbuffer (b1): TargetWidth/TargetHeight for aspect ratio.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum CheckerBoardBinding {
  CHECKERBOARD_Params     = 0,  // constant CheckerBoardParams& (b0)
  CHECKERBOARD_Resolution = 1,  // constant CheckerBoardResolution& (b1)
  // No texture input (pure generator). No sampler needed.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(CheckerBoardParams) == 64, "CheckerBoardParams 64 bytes (16-byte multiple)");
static_assert(sizeof(CheckerBoardResolution) == 16, "CheckerBoardResolution 16 bytes");
#endif
