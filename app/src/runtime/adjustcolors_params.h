// Shared host<->shader params for the TiXL-ported AdjustColors IMAGE FILTER (lane F3-3).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/AdjustColors.hlsl and
// AdjustColors.cs/.t3. TiXL authority: AdjustColors.cs (Texture2d/Colorize/Saturation/Hue/
// Contrast/Exposure/Brightness/PreventClamping/Vignette/OrangeTeal/Background inputs) +
// AdjustColors.hlsl (the single-pass kernel: HSB ops, vignette, colorize, contrast S-curve,
// brightness, background composite).
//
// Packed layout: Vec4 fields kept whole; Vec2 PreventClamping split as two floats.
// 80 bytes (16-byte multiple).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct AdjustColorsParams {
  // TiXL AdjustColors.hlsl cbuffer order: Colorize, Background, Exposure, Contrast, Saturation,
  // OrangeTeal, PreventClamping(Vec2), Brightness, Hue, Vignette
  float ColorizeR, ColorizeG, ColorizeB, ColorizeA;  // TiXL Colorize (Vec4), default (1,1,1,0) — alpha=blend
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA;  // TiXL Background (Vec4), default ~(0,0,0,1)
  float Exposure;         // TiXL Exposure (Single), default 1.0; HSB.z multiplier before HSB ops
  float Contrast;         // TiXL Contrast (Single), default 0.0; S-curve amount (+1 in shader)
  float Saturation;       // TiXL Saturation (Single), default 1.0; HSB.y *= Saturation
  float OrangeTeal;       // TiXL OrangeTeal (Single), default 0.0; complementary colorize mix
  float PreventClampX;    // TiXL PreventClamping.x (Single), default 0.0; blend range
  float PreventClampY;    // TiXL PreventClamping.y (Single), default 5.0; headroom
  float Brightness;       // TiXL Brightness (Single), default 0.0; lift or crush
  float Hue;              // TiXL Hue (Single), default 0.0; degrees, wrapping
  float Vignette;         // TiXL Vignette (Single), default 0.0; radial darkening
  float _pad[3];          // pad 84 -> 96 (16-byte multiple)
};

enum AdjustColorsBinding {
  ADJUSTCOLORS_Params = 0,  // constant AdjustColorsParams& (b0)
  // texture(0) = inputTexture, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(AdjustColorsParams) == 80, "AdjustColorsParams 80 bytes (16-byte multiple)");
#endif
