// Shared host<->shader params for the TiXL-ported HSE (Hue/Saturation/Exposure) IMAGE FILTER
// (lane multi-image, image/color). Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/
// HueShift.hlsl. HSE adjusts an image's hue/saturation/exposure; the SECOND input (FxTexture) adds
// its .g channel to the hue shift PER-PIXEL (the Fx-modulation pattern, same shape as Displace's
// 2-texture read). Two FIXED Texture2D inputs (Image t0 + FxTexture t1).
//
// HueShift.hlsl cbuffer ParamConstants(b0) order: { Hue, Saturation, Exposure } — confirmed 1:1 from
// HSE.t3 (_multiImageFxSetupStatic FloatParams multi-input connection order: Hue, Saturation,
// Exposure; matches the cbuffer). .t3 DEFAULTS (HSE.t3): Hue 0.0, Saturation 1.0, Exposure 1.0.
// Packed into one all-scalar struct (particle_params.h discipline — no int/float mix, no Vector2).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct HseParams {
  // ParamConstants (HLSL b0), same order as HueShift.hlsl cbuffer:
  float Hue;         // TiXL Hue (Single), default 0; added (+ FxTexture.g) to the HSB hue, mod 1
  float Saturation;  // TiXL Saturation (Single), default 1; HSB sat *= Saturation, then saturate()
  float Exposure;    // TiXL Exposure (Single), default 1; HSB brightness (z) *= Exposure
  float _pad0;       // pad 12 -> 16 (16-byte multiple)
};

enum HseBinding {
  HSE_Params = 0,  // constant HseParams& (b0)
  // texture(0) = Image, texture(1) = FxTexture, sampler(0) = point/wrap; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(HseParams) == 16, "HseParams 16 bytes (16-byte multiple)");
#endif
