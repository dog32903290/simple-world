// Shared host<->shader params for the TiXL-ported ImageLevels IMAGE FILTER (image/analyze).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ImageLevels.hlsl + ImageLevels.cs/.t3.
//
// ImageLevels is a histogram/levels *visualization overlay* op: it draws the curve/subdivision/
// clamping overlay on top of the (optionally dimmed) original image. cbuffer layout follows the
// .hlsl ParamConstants(b0) VERBATIM (NOT the .cs [Input] order):
//   float2 Center; float Width; float Rotation; float2 Range; float ShowOriginal;
//
// The host also carries the TargetWidth/TargetHeight (the .hlsl Resolution cbuffer b2) and beatTime
// (TimeConstants cbuffer b1) so the leaf can feed the aspect-ratio + zebra-pattern math without a
// separate cbuffer binding. beatTime drives the clamp-highlight zebra ONLY; the golden forces it to
// a deterministic value.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ImageLevelsParams {
  // --- mirrors .hlsl cbuffer ParamConstants(b0) order ---
  float CenterX, CenterY;   // float2 Center
  float Width;              // float  Width
  float Rotation;           // float  Rotation
  float RangeX, RangeY;     // float2 Range
  float ShowOriginal;       // float  ShowOriginal
  // --- framework cbuffers folded in (Resolution b2 + TimeConstants b1 beatTime) ---
  float TargetWidth, TargetHeight;  // float2 (b2) Resolution
  float BeatTime;                   // float (b1) beatTime — zebra pattern only
  float _pad[2];                    // pad 10 -> 12 floats (48 bytes, 16-byte multiple)
};

enum ImageLevelsBinding {
  IL_Params = 0,  // constant ImageLevelsParams& (b0)
  // texture(0) = inputTexture, sampler(0) = point+wrap (ImageLevels.t3 _ImageFxShaderSetup2
  // Filter=MinMagMipPoint; address mode = the setup default Wrap/Repeat — code uses Repeat).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ImageLevelsParams) == 48,
              "ImageLevelsParams: 10 floats + 2 pad = 12 floats = 48 bytes");
#endif
