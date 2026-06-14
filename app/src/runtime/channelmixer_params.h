// Shared host<->shader params for the TiXL-ported ChannelMixer IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/MixChannels.hlsl and ChannelMixer.cs/.t3.
// TiXL authority: ChannelMixer.cs (Texture2d/MultiplyR/MultiplyG/MultiplyB/MultiplyA/Add/
// GenerateMipmaps/ClampResult inputs) + MixChannels.hlsl (the single-pass kernel: per-channel
// linear matrix mix + Add offset, optional clamp).
//
// Packed layout: 5 Vec4s (MultiplyR/G/B/A + Add) = 5*16 = 80 bytes, plus ClampResult (4) = 84,
// padded to 96 bytes (16-byte multiple).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ChannelMixerParams {
  // TiXL MixChannels.hlsl cbuffer order: MultiplyR, MultiplyG, MultiplyB, MultiplyA, Add, ClampResult
  // Each MultiplyX row: how much of src R/G/B/A contributes to output channel X.
  // MultiplyR default (1,0,0,0) — red output only from red input (identity row for R).
  float MultiplyRr, MultiplyRg, MultiplyRb, MultiplyRa;  // TiXL MultiplyR (Vec4), default (1,0,0,0)
  float MultiplyGr, MultiplyGg, MultiplyGb, MultiplyGa;  // TiXL MultiplyG (Vec4), default (0,1,0,0)
  float MultiplyBr, MultiplyBg, MultiplyBb, MultiplyBa;  // TiXL MultiplyB (Vec4), default (0,0,1,0)
  float MultiplyAr, MultiplyAg, MultiplyAb, MultiplyAa;  // TiXL MultiplyA (Vec4), default (0,0,0,1)
  float AddR, AddG, AddB, AddA;                          // TiXL Add (Vec4), default (0,0,0,0)
  float ClampResult;    // TiXL ClampResult (bool), default true (1.0f)
  float _pad[3];        // pad 84 -> 96 (16-byte multiple)
};

enum ChannelMixerBinding {
  CHANNELMIXER_Params = 0,  // constant ChannelMixerParams& (b0)
  // texture(0) = inputTexture, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ChannelMixerParams) == 96, "ChannelMixerParams 96 bytes (16-byte multiple)");
#endif
