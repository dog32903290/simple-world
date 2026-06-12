// Shared host<->shader params for the TiXL-ported Blur IMAGE FILTER (lane I, image/fx/blur).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Blur.hlsl (the per-pass shader) and
// the Blur.cs/Blur.t3 composite (two passes: horizontal then vertical). TiXL's Blur is a graph of
// two instances of Blur.hlsl with Direction (1,0) and (0,1); we run the same shader TWICE in one
// op (cookBlur) — the fork (named): no sub-graph machinery, just two render passes, so it stays a
// single leaf op and a single texture flow node.
//
// All-scalar (particle_params.h discipline): Vector2 Direction becomes DirectionX/Y. 32-byte
// (16-byte multiple). The HLSL cbuffer order is Direction, Size, NumberOfSamples, widthToHeight,
// Offset, Glow2 (Glow2 is fed by the Opacity input in TiXL's graph).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct BlurParams {
  float DirectionX, DirectionY;  // pass direction: (1,0) horizontal, (0,1) vertical
  float Size;                    // TiXL Size (Single), default 1.0; blur reach (×0.01 in shader)
  float NumberOfSamples;         // TiXL Samples (Single), default 8.0; taps per side
  float WidthToHeight;           // texture aspect (w/h) so the blur stays circular (TiXL computes it)
  float Offset;                  // TiXL Offset (Single), default 0.0; added constant after blur
  float Glow2;                   // TiXL Opacity -> Glow2 (Single), default 1.0; rgb intensity mul
  float _pad0;                   // pad 28 -> 32 (16-byte multiple)
};

enum BlurBinding {
  BLUR_Params = 0,  // constant BlurParams& (b0)
  // texture(0) = input, sampler(0) = linear; bound directly (no enum needed for tex/sampler).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BlurParams) == 32, "BlurParams 32 bytes (16-byte multiple)");
#endif
