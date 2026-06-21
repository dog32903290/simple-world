// Shared host<->shader params for the TiXL-ported RadialGradient IMAGE FILTER
// (image/generate/basic). Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/
// RadialGradient.hlsl (b0 ParamConstants, lines 5-22) + RadialGradient.cs/.t3 defaults.
//
// TiXL authority:
//   RadialGradient.cs   — slot declarations, types, enum maps (BlendMode = SharedEnums.RgbBlendModes)
//   RadialGradient.t3   — default values (Center=(0,0), Width=1, Offset=0, PingPong/Repeat=false,
//                         PolarOrientation=false, BiasAndGain=(0.5,0.5), Stretch=(1,1), Noise=0,
//                         BlendMode=0) + the Gradient→GradientsToTexture→t1 plumbing.
//   RadialGradient.hlsl — cbuffer b0 field order (lines 7-21) + b1 Resolution (lines 24-28) + psMain.
//
// b0 ParamConstants layout (VERBATIM HLSL cbuffer field order, RadialGradient.hlsl lines 7-21).
// HLSL packs scalars/float2 tightly UNLESS a var straddles a 16-byte register; trace shows NO straddle:
//   float2 Center;          offset   0   (default (0,0))         reg0 [0-7]
//   float  Width;           offset   8   (default 1.0)           reg0 [8-11]
//   float  Offset;          offset  12   (default 0.0)           reg0 [12-15]  (reg0 full)
//   float  PingPong;        offset  16   (bool->float; default 0) reg1 [16-19]
//   float  Repeat;          offset  20   (bool->float; default 0) reg1 [20-23]
//   float  PolarOrientation;offset  24   (bool->float; default 0) reg1 [24-27]
//   float  BlendMode;       offset  28   (Int->float; default 0)  reg1 [28-31]  (reg1 full)
//   float2 GainAndBias;     offset  32   (<- BiasAndGain input; default (0.5,0.5)) reg2 [32-39]
//   float2 Stretch;         offset  40   (default (1,1))          reg2 [40-47]  (reg2 full)
//   float  Noise;           offset  48   (default 0.0)            reg3 [48-51]
//   float  IsTextureValid;  offset  52   (host-injected)          reg3 [52-55]
// Total: 14 floats × 4 = 56 bytes. Last register [48-63] is half-used ([56-63] unused) — HLSL pads the
// cbuffer to 64 internally, but the HOST only writes the first 56 bytes the shader reads (Metal's
// setFragmentBytes copies the struct verbatim; field offsets, not the trailing register pad, are what
// must match — and these match byte-for-byte). No straddle → no interior padding needed.
//
// ★HLSL cbuffer field name vs RadialGradient.cs input name (the only rename trap):
//   HLSL "GainAndBias" (b0) <- .cs input "BiasAndGain" (Vector2, .t3 default (0.5,0.5)). The HLSL field
//   is sampled as float2(GainAndBias.x = gain, GainAndBias.y = bias) by ApplyGainAndBias. We name the
//   host field GainAndBias (HLSL order) and feed it from the BiasAndGain input (cook fn maps the names).
//
// ★Offset routing: UNLIKE LinearGradient (which routes Offset via a Multiply+PickFloat compound), the
//   RadialGradient.t3 wires the op's Offset input DIRECTLY into the shader cbuffer Offset (no Multiply,
//   no PickFloat child in the .t3 — traced). So the host writes the raw Offset; no scalar reshuffle.
//
// b1 Resolution{TargetWidth, TargetHeight} — host-filled from output size; bound at Metal fragment
// cbuffer index 1 (same pattern as LinearGradient / Rings / NGon).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RadialGradientParams {
  // b0 ParamConstants — field order MUST match RadialGradient.hlsl cbuffer verbatim (lines 7-21).
  float CenterX, CenterY;            // float2 Center,    default (0,0)
  float Width;                       // TiXL Width (Single),   default 1.0
  float Offset;                      // TiXL Offset (Single),  default 0.0 (wired DIRECT in .t3)
  float PingPong;                    // TiXL PingPong (bool->float), default 0
  float Repeat;                      // TiXL Repeat (bool->float),   default 0
  float PolarOrientation;            // TiXL PolarOrientation (bool->float), default 0
  float BlendMode;                   // TiXL BlendMode (Int->float, RgbBlendModes), default 0 (Normal)
  float GainAndBiasX, GainAndBiasY;  // float2 GainAndBias <- BiasAndGain input, default (0.5,0.5)
  float StretchX, StretchY;          // float2 Stretch,   default (1,1)
  float Noise;                       // TiXL Noise (Single),   default 0.0
  float IsTextureValid;              // host-injected: 1.0 if Image wired, else 0.0
  // 14 floats × 4 = 56 bytes. No straddle → no interior padding (see header note).
};

struct RadialGradientResolution {
  // Mirrors RadialGradient.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8->16 bytes
};

enum RadialGradientBinding {
  RADIALGRADIENT_Params     = 0,  // constant RadialGradientParams& (folds RadialGradient.hlsl b0)
  RADIALGRADIENT_Resolution = 1,  // constant RadialGradientResolution& (b1; Metal index 1)
  // texture(0) = ImageA (Image, optional upstream), sampler(0) = texSampler (Wrap).
  // texture(1) = Gradient (rasterized 1xN row),     sampler(1) = clampedSampler (ClampToEdge).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RadialGradientParams) == 56,
              "RadialGradientParams must be 56 bytes (14 floats; HLSL b0 field offsets, no straddle)");
static_assert(sizeof(RadialGradientResolution) == 16, "RadialGradientResolution 16 bytes");
#endif
