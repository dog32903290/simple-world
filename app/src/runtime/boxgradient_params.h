// Shared host<->shader params for the TiXL-ported BoxGradient IMAGE FILTER
// (image/generate/basic). Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/
// BoxGradient.hlsl (b0 ParamConstants, lines 4-19) + BoxGradient.cs/.t3 defaults.
//
// TiXL authority:
//   BoxGradient.cs   — slot declarations, types, enum maps (BlendMode = SharedEnums.RgbBlendModes)
//   BoxGradient.t3   — default values + the FloatsToBuffer connection-order cbuffer fill
//   BoxGradient.hlsl — cbuffer b0 field order (lines 4-19) + b1 Resolution (lines 21-25)
//
// b0 ParamConstants layout (VERBATIM HLSL cbuffer field order, BoxGradient.hlsl lines 4-19):
//   float2 Center;          offset   0   (default (0,0))
//   float2 Size;            offset   8   (default (0.25,0.25); fills register 0 [0-15])
//   float4 CornersRadius;   offset  16   (default (0,0,0,0); fills register 1 [16-31])
//   float  Rotation;        offset  32   (default 0.0)
//   float  UniformScale;    offset  36   (default 1.0)
//   float  Width;           offset  40   (<- GradientWidth input; default 1.0)
//   float  Offset;          offset  44   (default 0.0; fills register 2 [32-47])
//   float  PingPong;        offset  48   (bool->float; default 1.0 — ★TiXL default TRUE)
//   float  Repeat;          offset  52   (bool->float; default 0.0)
//   float2 GainAndBias;     offset  56   (default (0.5,0.5); fills register 3 [48-63])
//   float  BlendMode;       offset  64   (Int->float; default 0 = Normal; starts register 4 [64-79])
//   float  IsTextureValid;  offset  68   (host-injected; in register 4)
// Total: 18 floats × 4 = 72 bytes → padded to 80 (5 × 16-byte registers) so the struct is a clean
// register multiple. LARGEST cbuffer of the gradient family (float4 CornersRadius pushes it out).
//
// ★16-byte register straddle check (the HARD part of this op): with float2 Center at 0 + float2 Size
// at 8, register 0 [0-15] is exactly Center.xy + Size.xy → no straddle. float4 CornersRadius at 16
// is 16-aligned → fills register 1 cleanly. The scalar run Rotation/UniformScale/Width/Offset at
// 32..47 fills register 2 with no straddle. PingPong/Repeat at 48..55 + float2 GainAndBias at 56
// → GainAndBias sits wholly inside register 3 [48-63] (offset 56, ends 63) → no cross-register
// straddle. BlendMode/IsTextureValid at 64..71 start register 4. C++ struct field offsets line up
// with the MSL/HLSL ones by construction (all scalars, float2s 8-aligned, float4 16-aligned) — the
// static_assert(sizeof==80) below is the tripwire.
//
// ★CornersRadius component reshuffle (named fork) — TRACED from BoxGradient.t3: the FloatsToBuffer
// fills the shader's float4 CornersRadius from the Vector4Components child in the slot order
// (Z, Y, W, X) of the op's CornersRadius input (Vector4Components.cs output GUIDs:
// X=cfb58526, Y=2f8e90dd, Z=162bb4fe, W=e1dede5f; the .t3 wires them into FloatValues as
// 162bb4fe,2f8e90dd,e1dede5f,cfb58526 = Z,Y,W,X). So shader CornersRadius = inputCorners.zywx. The
// .t3 default is (0,0,0,0) so the reshuffle is invisible at default, but we replicate it faithfully
// in the cook so non-default corner radii match TiXL byte-for-byte. [fork-cornersradius-zywx]
//
// ★No Offset routing here (unlike LinearGradient): BoxGradient.t3 wires Offset DIRECTLY into the
// cbuffer (no Multiply/PickFloat). The shader's `- Offset * Width` math lives in the HLSL psMain
// (line 98), not in a .t3 routing node — so the cook fills Offset 1:1 from the input.
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

struct BoxGradientParams {
  // b0 ParamConstants — field order MUST match BoxGradient.hlsl cbuffer verbatim (lines 4-19).
  float CenterX, CenterY;                  // float2 Center,        default (0,0)
  float SizeX, SizeY;                      // float2 Size,          default (0.25,0.25)
  float CornersRadiusX, CornersRadiusY, CornersRadiusZ, CornersRadiusW;  // float4 CornersRadius (0,0,0,0)
  float Rotation;                          // TiXL Rotation (Single), default 0.0 (degrees)
  float UniformScale;                      // TiXL UniformScale (Single), default 1.0
  float Width;                             // <- GradientWidth input (Single), default 1.0
  float Offset;                            // TiXL Offset (Single), default 0.0 (DIRECT, no routing)
  float PingPong;                          // bool->float, ★default 1.0 (TiXL t3 PingPong = true)
  float Repeat;                            // bool->float, default 0.0
  float GainAndBiasX, GainAndBiasY;        // float2 GainAndBias,   default (0.5,0.5)
  float BlendMode;                         // Int->float, default 0 (Normal)
  float IsTextureValid;                    // host-injected: 1.0 if Image wired, else 0.0
  float _pad[2];                           // pad 72 -> 80 bytes (5 × 16-byte HLSL registers)
};

struct BoxGradientResolution {
  // Mirrors BoxGradient.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8->16 bytes
};

enum BoxGradientBinding {
  BOXGRADIENT_Params     = 0,  // constant BoxGradientParams& (folds BoxGradient.hlsl b0)
  BOXGRADIENT_Resolution = 1,  // constant BoxGradientResolution& (b1; Metal index 1)
  // texture(0) = ImageA (Image, optional upstream), sampler(0) = texSampler (Wrap).
  // texture(1) = Gradient (rasterized 1xN row),     sampler(1) = clammpedSampler (ClampToEdge).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BoxGradientParams) == 80,
              "BoxGradientParams must be 80 bytes (5 × 16-byte HLSL cbuffer registers, 72 padded)");
static_assert(sizeof(BoxGradientResolution) == 16, "BoxGradientResolution 16 bytes");
#endif
