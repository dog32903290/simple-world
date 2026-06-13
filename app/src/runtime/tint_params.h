// Shared host<->shader params for the TiXL-ported Tint IMAGE FILTER (lane F3-1, image/color).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Tint.hlsl and Tint.cs/.t3.
// TiXL authority: Tint.cs (Amount/MapBlackTo/MapWhiteTo/Exposure/ChannelWeights/GainAndBias inputs)
// + Tint.hlsl (the single-pass kernel: luminance via ChannelWeights dot, GainAndBias remap,
// lerp black->white, mix with Amount).
//
// Packed layout (particle_params.h discipline): Vec4 fields kept whole (no .x/.y split at this
// layer — the host unrolls them from cookParam calls; the struct is the GPU cbuffer mirror).
// 80 bytes (next 16-byte multiple from 76).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TintParams {
  // TiXL Tint.hlsl cbuffer order: MapBlackTo, MapWhiteTo, ChannelWeights, Amount, GainAndBias, Exposure
  float MapBlackR, MapBlackG, MapBlackB, MapBlackA;   // MapBlackTo (Vec4), default ~(0,0,0,1)
  float MapWhiteR, MapWhiteG, MapWhiteB, MapWhiteA;   // MapWhiteTo (Vec4), default (1,1,1,1)
  float ChannelR, ChannelG, ChannelB, ChannelA;       // ChannelWeights (Vec4), default (1,1,1,0)
  float Amount;           // TiXL Amount (Single), default 1.0; blend original <-> mapped
  float GainX, GainY;     // TiXL GainAndBias (Vec2), default (0.5,0.5); identity
  float Exposure;         // TiXL Exposure (Single), default 1.0; pre-multiplies rgb
  float _pad[4];          // pad 64 -> 80 (16-byte multiple)
};

enum TintBinding {
  TINT_Params = 0,  // constant TintParams& (b0)
  // texture(0) = inputTexture, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TintParams) == 80, "TintParams 80 bytes (16-byte multiple)");
#endif
