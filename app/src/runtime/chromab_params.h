// Shared host<->shader params for the TiXL-ported ChromaticAbberation IMAGE FILTER (lane F3-2).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaticAbberation.hlsl and
// ChromaticAbberation.cs/.t3. TiXL authority: ChromaticAbberation.cs (Image/Size/Strength/
// SampleCount/Distort inputs) + ChromaticAbberation.hlsl (the single-pass kernel: radial
// chromatic fringe loop, lens distortion, R/B separate from G/A).
//
// Resolution cbuffer (b1) holds TargetWidth/TargetHeight — needed by the hlsl for aspect. The
// host fills it from c.output->width()/height(). 16 bytes (all-scalar, 16-byte aligned).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ChromaBAParams {
  // TiXL ChromaticAbberation.hlsl cbuffer (b0): Size, Strength, SampleCount, Distort
  float Size;         // TiXL Size (Single), default 1.0; radial offset scale
  float Strength;     // TiXL Strength (Single), default 0.3; blend original <-> fringed
  float SampleCount;  // TiXL SampleCount (int), default 8; loop iterations (clamped 3..20)
  float Distort;      // TiXL Distort (Single), default 0.0; barrel lens distortion amount
};

struct ChromaBAResolution {
  // TiXL Resolution cbuffer (b1): TargetWidth, TargetHeight
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum ChromaBABinding {
  CHROMAB_Params      = 0,  // constant ChromaBAParams& (b0)
  CHROMAB_Resolution  = 1,  // constant ChromaBAResolution& (b1)
  // texture(0) = Image, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ChromaBAParams) == 16, "ChromaBAParams 16 bytes");
static_assert(sizeof(ChromaBAResolution) == 16, "ChromaBAResolution 16 bytes");
#endif
