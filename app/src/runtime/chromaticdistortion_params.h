// Shared host<->shader params for the TiXL-ported ChromaticDistortion IMAGE FILTER (lane
// image_filter). Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaticDistortion.hlsl
// and ChromaticDistortion.cs/.t3. TiXL authority: ChromaticDistortion.cs (Texture2d/Center/Size/
// Colorize/Distort/DistortOffset/ScaleImage/SampleCount inputs) + ChromaticDistortion.hlsl (the
// single-pass kernel: radial bulge warp + N-sample radial blur, RGB split via chromaShift(),
// lerp blurred<->chromarized by Colorize).
//
// Only the ParamConstants(b0) fields are op params. ChromaticDistortion.hlsl's b1 TimeConstants
// (globalTime/time/runTime/beatTime) is framework-injected and UNUSED by the kernel — not bound,
// not exposed as ports (orchestrator guardrail).
//
// HLSL ParamConstants order: float2 Center; then Size/Colorize/Distort/DistortOffset/ScaleImage/
// SampleCount. Layout: Center(8) + 6 floats(24) = 32 bytes (already a 16-byte multiple).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ChromaticDistortionParams {
  // TiXL ChromaticDistortion.hlsl ParamConstants (b0): float2 Center first, then the 6 scalars.
  float CenterX, CenterY;  // TiXL Center (Vec2), default (0,0); distortion focus offset (0..1 uv)
  float Size;              // TiXL Size (Single), default 0.05; radial sample spread
  float Colorize;          // TiXL Colorize (Single), default 0.1; blur<->chromatic blend (0..1)
  float Distort;           // TiXL Distort (Single), default 0.1; barrel bulge amount
  float DistortOffset;     // TiXL DistortOffset (Single), default 0.5; bulge falloff base
  float ScaleImage;        // TiXL ScaleImage (Single), default 1.0; image pre-scale (divides uv)
  float SampleCount;       // TiXL SampleCount (int), default 16; radial samples (clamped 1..100)
};

enum ChromaticDistortionBinding {
  CHROMADIST_Params = 0,  // constant ChromaticDistortionParams& (b0)
  // texture(0) = ImageA, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ChromaticDistortionParams) == 32, "ChromaticDistortionParams 32 bytes");
#endif
