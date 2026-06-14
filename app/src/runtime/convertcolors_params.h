// Shared host<->shader params for the TiXL-ported ConvertColors IMAGE FILTER (image/color).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/adjust/img-fx-ConvertColors.hlsl and
// ConvertColors.cs/.t3. The kernel takes a single float "Mode" cbuffer field (b0) — the .cs int
// enum (RgbToOKLab=0/OKLabToRgb=1/RgbToLCh=2/LChToRgb=3) is dispatched by float thresholds
// (Mode<0.5 / <1.5 / <2.5 / <3.5), the _ForceKind int->float pattern used across the filters.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ConvertColorsParams {
  // img-fx-ConvertColors.hlsl cbuffer ParamConstants(b0): float Mode.
  float Mode;       // 0=RgbToOKLab 1=OKLabToRgb 2=RgbToLCh 3=LChToRgb; else passthrough
  float _pad[3];    // pad 4 -> 16 (16-byte multiple)
};

enum ConvertColorsBinding {
  CC_Params = 0,  // constant ConvertColorsParams& (b0)
  // texture(0) = inputTexture, sampler(0) = point+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ConvertColorsParams) == 16, "ConvertColorsParams 16 bytes (16-byte multiple)");
#endif
