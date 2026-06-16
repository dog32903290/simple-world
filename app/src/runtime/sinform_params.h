// Shared host<->shader params for the TiXL-ported SinForm IMAGE GENERATOR (Phase C, C-3).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/SinForm.hlsl and SinForm.cs/.t3.
// TiXL authority: SinForm.cs (Image/Fill/Background/LineWidth/Fade/Size/Offset/Rotate/Copies/
// OffsetCopies/Resolution/TextureFormat inputs) + SinForm.t3 (defaults) + SinForm.hlsl (psMain:
// aspect-corrected rotation, copiesCount-loop sin-wave generation, smoothstep feather, alpha
// composite over input orgColor).
//
// b0 ParamConstants order (SinForm.hlsl lines 5-16):
//   float4 Fill; float4 Background; float2 Size; float2 Offset; float2 OffsetCopies;
//   float Rotate; float LineWidth; float Fade; float Copies;
// Layout: 16 + 16 + 8 + 8 + 8 + 4 + 4 + 4 + 4 = 72 bytes → pad to 80 (16-byte multiple).
//
// b1 Resolution (SinForm.hlsl lines 27-31): float TargetWidth, TargetHeight → 8 bytes → pad to 16.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SinFormParams {
  // TiXL SinForm.hlsl cbuffer ParamConstants (b0) — field names and order verbatim.
  float FillR, FillG, FillB, FillA;        // Fill (Vec4), TiXL t3 default (1,1,1,1)
  float BgR, BgG, BgB, BgA;               // Background (Vec4), TiXL t3 default (0,0,0,0)
  float SizeX, SizeY;                       // Size (Vec2), TiXL t3 default (1,1)
  float OffsetX, OffsetY;                   // Offset (Vec2), TiXL t3 default (0,0)
  float OffCopX, OffCopY;                  // OffsetCopies (Vec2), TiXL t3 default (0,0.05)
  float Rotate;                             // Rotate (float), TiXL t3 default 0.0
  float LineWidth;                          // LineWidth (float), TiXL t3 default 0.04333334
  float Fade;                               // Fade (float), TiXL t3 default 1.0
  float Copies;                             // Copies (float), TiXL t3 default 0.0 (→ 1 copy)
  float _pad[2];                            // pad 72 -> 80 (16-byte multiple)
};

struct SinFormResolution {
  // SinForm.hlsl b1 Resolution cbuffer (TargetWidth/TargetHeight); host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum SinFormBinding {
  SINFORM_Params     = 0,  // constant SinFormParams& (b0)
  SINFORM_Resolution = 1,  // constant SinFormResolution& (b1, bound at Metal fragment index 1)
  // texture(0) = inputTexture (or 1x1 black dummy), sampler(0) = linear+clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SinFormParams) == 80, "SinFormParams 80 bytes (16-byte multiple)");
static_assert(sizeof(SinFormResolution) == 16, "SinFormResolution 16 bytes");
#endif
