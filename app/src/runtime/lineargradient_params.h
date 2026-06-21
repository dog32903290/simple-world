// Shared host<->shader params for the TiXL-ported LinearGradient IMAGE FILTER
// (image/generate/basic). Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/
// LinearGradient.hlsl (b0 ParamConstants, lines 4-18) + LinearGradient.cs/.t3 defaults.
//
// TiXL authority:
//   LinearGradient.cs   — slot declarations, types, enum maps (Modes/OffsetModes)
//   LinearGradient.t3   — default values (+ the Multiply/PickFloat Offset routing, see below)
//   LinearGradient.hlsl — cbuffer b0 field order (lines 4-18) + b1 Resolution (lines 21-24)
//
// b0 ParamConstants layout (VERBATIM HLSL cbuffer field order, LinearGradient.hlsl lines 4-18):
//   float2 Center;         offset   0   (default (0,0))
//   float  Width;          offset   8   (default 1.0)
//   float  Rotation;       offset  12   (<- Rotate input; default 90.0; fills register 0 [0-15])
//   float  PingPong;       offset  16   (bool->float; default 0)
//   float  Repeat;         offset  20   (bool->float; default 0)
//   float2 GainAndBias;    offset  24   (default (0.5,0.5); fills register 1 [16-31])
//   float  Offset;         offset  32   (ROUTED: see Offset-routing trace below)
//   float  SizeMode;       offset  36   (Int->float; default 0 = AlignToHeight)
//   float  BlendMode;      offset  40   (Int->float; default 0 = Normal)
//   float  IsTextureValid; offset  44   (host-injected; fills register 2 [32-47])
// Total: 48 bytes (3 × 16-byte registers — exact multiple). float2 GainAndBias at offset 24 sits
// wholly inside register 1 [16-31] → no cross-register straddle. No padding needed: 12 floats×4=48.
//
// ★Offset-routing trace (LinearGradient.t3 children Multiply 3c0c9eae + PickFloat 3cf139ae):
//   Multiply  = Width × Offset            (Width->372288fa, Offset->5ae4bb07)
//   PickFloat = FloatValues[OffsetMode mod 2], FloatValues = [Offset, Multiply]  (PickFloat.cs:
//     Selected = connections[Index.Mod(count)]); Index = OffsetMode.
//     OffsetMode=0 (RelativeToImage) -> Offset;  OffsetMode=1 (RelativeToSize) -> Width × Offset.
//   PickFloat.out -> shader cbuffer Offset.  We replicate this as a SCALAR EXPRESSION in the cook
//   fn (Offset = OffsetMode < 0.5 ? Offset : Width*Offset), NOT a cbuffer reshuffle. Defaults
//   (OffsetMode=0, Offset=0) -> shader Offset = 0.
//
// b1 Resolution{TargetWidth, TargetHeight} — host-filled from output size; bound at Metal fragment
// cbuffer index 1 (same pattern as Rings / NGon / VoronoiCells / SinForm).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct LinearGradientParams {
  // b0 ParamConstants — field order MUST match LinearGradient.hlsl cbuffer verbatim (lines 4-18).
  float CenterX, CenterY;  // float2 Center,    default (0,0)
  float Width;             // TiXL Width (Single),   default 1.0
  float Rotation;          // TiXL Rotate (Single),  default 90.0 degrees  [hlsl name = Rotation]
  float PingPong;          // TiXL PingPong (bool->float), default 0
  float Repeat;            // TiXL Repeat (bool->float),   default 0
  float GainAndBiasX, GainAndBiasY;  // float2 GainAndBias, default (0.5,0.5)
  float Offset;            // ROUTED scalar (Offset-routing trace above); default 0
  float SizeMode;          // TiXL SizeMode (Int->float),  default 0 (AlignToHeight)
  float BlendMode;         // TiXL BlendMode (Int->float), default 0 (Normal)
  float IsTextureValid;    // host-injected: 1.0 if Image wired, else 0.0
  // No padding needed: 12 floats × 4 = 48 bytes (3 × 16-byte HLSL registers, exact multiple).
};

struct LinearGradientResolution {
  // Mirrors LinearGradient.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8->16 bytes
};

enum LinearGradientBinding {
  LINEARGRADIENT_Params     = 0,  // constant LinearGradientParams& (folds LinearGradient.hlsl b0)
  LINEARGRADIENT_Resolution = 1,  // constant LinearGradientResolution& (b1; Metal index 1)
  // texture(0) = ImageA (Image, optional upstream), sampler(0) = texSampler (Wrap).
  // texture(1) = Gradient (rasterized 1xN row),     sampler(1) = clampedSampler (ClampToEdge).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(LinearGradientParams) == 48,
              "LinearGradientParams must be 48 bytes (3 × 16-byte HLSL cbuffer registers)");
static_assert(sizeof(LinearGradientResolution) == 16, "LinearGradientResolution 16 bytes");
#endif
