// Shared host<->shader params for the TiXL-ported ToneMapping IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl and ToneMapping.cs.
// TiXL authority: ToneMapping.cs (Texture2d/Mode/CorrectGamma/Gamma/Exposure inputs) +
// ToneMap.hlsl (the single-pass kernel: per-mode tonemap curve + optional gamma correction).
//
// cbuffer layout order matches ToneMap.hlsl b0 verbatim:
//   float Mode        — MappedType enum: 0=Aces 1=Reinhard 2=Filmic 3=Uncharted2 4=AgX 5=AgX_Punchy 6=None
//   float CorrectGamma — bool (>0.5 = true)
//   float GammaValue  — Gamma input (TiXL InputSlot<float> Gamma)
//   float Exposure    — pre-multiplies rgb before tonemap
// Total: 4 floats = 16 bytes (already 16-byte aligned).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ToneMappingParams {
  // TiXL ToneMap.hlsl cbuffer b0 order (verbatim):
  float Mode;          // 0=Aces 1=Reinhard 2=Filmic 3=Uncharted2 4=AgX 5=AgX_Punchy 6=None
  float CorrectGamma;  // bool >0.5 = apply gamma correction; TiXL CorrectGamma (bool), default false
  float GammaValue;    // TiXL Gamma (float), default 2.2
  float Exposure;      // TiXL Exposure (float), default 1.0; pre-multiplies rgb before curve
};

enum ToneMappingBinding {
  TONEMAPPING_Params = 0,  // constant ToneMappingParams& (b0)
  // texture(0) = inputTexture, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ToneMappingParams) == 16, "ToneMappingParams 16 bytes (16-byte multiple)");
#endif
