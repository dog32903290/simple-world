// Shared host<->shader params for the TiXL-ported NGonGradient IMAGE FILTER
// (image/generate/basic). Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/
// NGonGradient.hlsl (b0 ParamConstants, lines 4-24) + NGonGradient.cs/.t3 defaults.
//
// TiXL authority:
//   NGonGradient.cs   — slot declarations, types, enum map (BlendMode = SharedEnums.RgbBlendModes)
//   NGonGradient.t3   — default values + the GradientsToTexture→t1 plumbing + Vector2Components routing
//   NGonGradient.hlsl — cbuffer b0 field order (lines 4-24) + b1 Resolution (lines 26-30)
//
// b0 ParamConstants layout (VERBATIM HLSL cbuffer field order, NGonGradient.hlsl lines 4-24):
//   float2 Position;       offset   0   (default (0,0))
//   float  Sides;          offset   8   (default 5.0)
//   float  Radius;         offset  12   (default 0.33; fills register 0 [0-15])
//   float  Curvature;      offset  16   (default 0.0)
//   float  Blades;         offset  20   (default 0.0)
//   float  Roundness;      offset  24   (default 1.0)
//   float  Rotate;         offset  28   (default 180.0; fills register 1 [16-31])
//   float  Width;          offset  32   (default 0.14)
//   float  Offset;         offset  36   (default 0.0)
//   float  PingPong;       offset  40   (bool->float; default 0)
//   float  Repeat;         offset  44   (bool->float; default 0; fills register 2 [32-47])
//   float  BlendMode;      offset  48   (Int->float; default 0 = Normal)
//   float2 GainAndBias;    offset  52   (<- BiasAndGain input; default (0.5,0.5))
//   float  IsTextureValid; offset  60   (host-injected; fills register 3 [48-63])
// Total: 64 bytes (4 × 16-byte registers — exact multiple).
//
// ★HLSL-register straddle note: GainAndBias is a float2 declared at offset 52. In the HLSL cbuffer
//   it would normally be promoted to a 16-byte boundary (would straddle register 3/4), BUT the field
//   sits between BlendMode (48) and IsTextureValid (60) with no padding in the source, so the source
//   packs it tight: BlendMode @48, GainAndBias @52-59, IsTextureValid @60. We mirror that tight pack
//   here (14 floats × 4 = 56)... actually 16 floats? No: count the SCALARS — Position(2) + Sides +
//   Radius + Curvature + Blades + Roundness + Rotate + Width + Offset + PingPong + Repeat + BlendMode
//   + GainAndBias(2) + IsTextureValid = 2+10+1+2+1 = 16 floats × 4 = 64 bytes. The host struct lays
//   them out in declaration order (tight, no implicit 16B promotion) and setFragmentBytes copies the
//   raw 64 bytes; the MSL struct in ngongradient.metal has the IDENTICAL field order so the offsets
//   line up exactly. (Same convention as NGon/LinearGradient — single source-order pack, no cbuffer
//   reshuffle.)
//
// ★Position.yx note: the cbuffer field is `float2 Position` in (x,y) order. NGonGradient.hlsl:149 does
//   `p += Position.yx` — the .yx SWAP lives in the SHADER, not the cbuffer. So the host fills
//   PositionX/PositionY from the Position.x/Position.y params straight; the shader swaps. Ported verbatim.
//
// ★GainAndBias note: the .cs input is named `BiasAndGain` (default (0.5,0.5)); the .t3 routes it through
//   a Vector2Components (X out=1cee5adb first, Y out=305d321d second) into the FloatsToBuffer at the
//   cbuffer's `GainAndBias` slot. So GainAndBias.x = BiasAndGain.x, GainAndBias.y = BiasAndGain.y
//   (no component swap). Replicated as a straight per-component fill in the cook fn.
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

struct NGonGradientParams {
  // b0 ParamConstants — field order MUST match NGonGradient.hlsl cbuffer verbatim (lines 4-24).
  float PositionX, PositionY;  // float2 Position, default (0,0). Shader does p += Position.yx (:149).
  float Sides;                 // TiXL Sides (Single),     default 5.0
  float Radius;                // TiXL Radius (Single),    default 0.33
  float Curvature;             // TiXL Curvature (Single), default 0.0
  float Blades;                // TiXL Blades (Single),    default 0.0
  float Roundness;             // TiXL Roundness (Single), default 1.0
  float Rotate;                // TiXL Rotate (Single),    default 180.0 degrees
  float Width;                 // TiXL Width (Single),     default 0.14
  float Offset;                // TiXL Offset (Single),    default 0.0
  float PingPong;              // TiXL PingPong (bool->float), default 0
  float Repeat;                // TiXL Repeat (bool->float),   default 0
  float BlendMode;             // TiXL BlendMode (Int->float), default 0 (Normal)
  float GainAndBiasX, GainAndBiasY;  // float2 GainAndBias (<- BiasAndGain input), default (0.5,0.5)
  float IsTextureValid;        // host-injected: 1.0 if Image wired, else 0.0
  // 16 floats × 4 = 64 bytes (4 × 16-byte HLSL cbuffer registers, exact multiple). No padding.
};

struct NGonGradientResolution {
  // Mirrors NGonGradient.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8->16 bytes
};

enum NGonGradientBinding {
  NGONGRADIENT_Params     = 0,  // constant NGonGradientParams& (folds NGonGradient.hlsl b0)
  NGONGRADIENT_Resolution = 1,  // constant NGonGradientResolution& (b1; Metal index 1)
  // texture(0) = ImageA (Image, optional upstream), sampler(0) = texSampler (Wrap).
  // texture(1) = Gradient (rasterized 1xN row),     sampler(1) = clampedSampler (ClampToEdge).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(NGonGradientParams) == 64,
              "NGonGradientParams must be 64 bytes (4 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(NGonGradientResolution) == 16, "NGonGradientResolution 16 bytes");
#endif
