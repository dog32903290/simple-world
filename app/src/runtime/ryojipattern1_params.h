// Shared host<->shader params for the TiXL-ported RyojiPattern1 IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/pattern/RyojiPattern1.cs (Image/Background/
// Foreground/MixOriginal/Contrast/ForgroundRatio/Highlight/HighlightProbability/HighlightSeed/
// Iterations/Splits/SplitProbability/ScrollSpeed/ScrollProbability/Padding/Seed/Resolution/
// GenerateMipmaps) + RyojiPattern1.t3 (defaults) + Assets/shaders/img/generate/RyojiPattern1.hlsl
// (the single-pass kernel: recursively subdivide a cel grid via per-cel hashes, fill each cel with
// a hashed grayscale lerp of Background/Foreground/sampled-image, occasionally Highlight).
//
// cbuffer b0 field order is taken VERBATIM from RyojiPattern1.hlsl ParamConstants (register b0).
// The .t3 wires every scalar/vector input through a FloatsToBuffer with ZERO intermediate math
// (Vec4Components / Vec2Components only — pure component split), so the cbuffer is filled 1:1 from
// the op's Float inputs. cbuffer order (HLSL):
//   Background(f4) | Foreground(f4) | Highlight(f4) | Subdivisions(f2)[="Splits" input]
//   | SplitProbability(f2) | ScrollSpeed(f2) | ScrollProbability(f2) | Padding(f2)
//   | Contrast(f) | Iterations(f) | Seed(f) | ForegroundRatio(f) | HighlightProbability(f)
//   | MixOriginal(f) | HighlightSeed(f)
// = 48 + 40 + 28 = 116 bytes -> padded to 128 (16-byte multiple).
//
// cbuffer b1 = Time (RyojiPattern1.hlsl TimeConstants register b1). The shader only reads
// `beatTime` (the scroll term). FORK (named — see point_ops_ryojipattern1.cpp): host fills BeatTime
// (= a beat clock); in a headless cook with no global clock BeatTime defaults to 0 -> deterministic
// (parity-safe, same class as Grain's host-fed Time). The other TimeConstants fields
// (globalTime/time/runTime) are unused by psMain, so we carry only BeatTime.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RyojiPattern1Params {
  // RyojiPattern1.hlsl cbuffer ParamConstants (b0) — field order verbatim:
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA;  // Background (Vec4), default (0,0,0,1)
  float ForegroundR, ForegroundG, ForegroundB, ForegroundA;  // Foreground (Vec4), default (1,1,1,1)
  float HighlightR, HighlightG, HighlightB, HighlightA;       // Highlight (Vec4), default (1,0,0,1)
  float SubdivisionsX, SubdivisionsY;       // Subdivisions = "Splits" input (Vec2), default (4,3)
  float SplitProbabilityX, SplitProbabilityY;   // SplitProbability (Vec2), default (0, 0.27666667)
  float ScrollSpeedX, ScrollSpeedY;             // ScrollSpeed (Vec2), default (0, -0.23333332)
  float ScrollProbabilityX, ScrollProbabilityY; // ScrollProbability (Vec2), default (0, 0.5)
  float PaddingX, PaddingY;                     // Padding (Vec2), default (0.02, 0.023333333)
  float Contrast;             // Contrast (Single), default 0.75
  float Iterations;           // Iterations (Single), default 7
  float Seed;                 // Seed (Single), default 0
  float ForegroundRatio;      // ForgroundRatio (Single), default 0.5
  float HighlightProbability; // HighlightProbability (Single), default 0.01
  float MixOriginal;          // MixOriginal (Single), default 0
  float HighlightSeed;        // HighlightSeed (Single), default 0
  float _pad[3];              // pad 116 -> 128 (16-byte multiple)
};

struct RyojiPattern1Time {
  // RyojiPattern1.hlsl TimeConstants (b1): psMain only reads beatTime (scroll). Others unused.
  float BeatTime;
  float _pad[3];  // pad 4 -> 16 (16-byte multiple)
};

enum RyojiPattern1Binding {
  RYOJIPATTERN1_Params = 0,  // constant RyojiPattern1Params& (b0)
  RYOJIPATTERN1_Time   = 1,  // constant RyojiPattern1Time&   (b1)
  // texture(0) = ImageA (optional; default Image=null + MixOriginal=0 -> sample unused).
  // sampler(0) = linear + WRAP (RyojiPattern1.t3 _ImageFxShaderSetup2 address mode = "Wrap").
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RyojiPattern1Params) == 128, "RyojiPattern1Params 128 bytes (16-byte multiple)");
static_assert(sizeof(RyojiPattern1Time) == 16, "RyojiPattern1Time 16 bytes");
#endif
