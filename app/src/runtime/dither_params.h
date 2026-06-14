// Shared host<->shader params for the TiXL-ported Dither IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Dither.hlsl and
// Operators/Lib/image/fx/stylize/Dither.cs.
//
// TiXL authority (ports = Dither.cs InputSlots, layout = Dither.hlsl cbuffer b0):
//   ShadowColor    -> Black            (Vector4)   Dither.cs:14-15
//   HighlightColor -> White            (Vector4)   Dither.cs:17-18
//   GrayScaleWeights                   (Vector4)   Dither.cs:23-24
//   GainAndBias                        (Vector2)   Dither.cs:26-27
//   Scale                              (float)     Dither.cs:29-30
//   Method (enum FloydSteinberg/Diffusion -> Bayer/hash) (int) Dither.cs:20-21
//   Offset                             (Vector2)   Dither.cs:32-33
//   BlendMethod    -> BlendMode        (int)       Dither.cs:35-36
//
// FORK (named — GrayScaleWeights declared but unused): Dither.hlsl declares GrayScaleWeights in
// cbuffer b0 (line 9) but psMain NEVER reads it — grayScale = ApplyGainAndBias(saturate(color),
// GainAndBias) (Dither.hlsl:75) ignores the weights. We keep the port (it IS a Dither.cs input,
// orchestrator guardrail: expose .cs inputs) and pass it to the shader for cbuffer-layout fidelity,
// but the kernel leaves it unused exactly as TiXL does. Default (0.299,0.587,0.114,0) (luma).
//
// FORK (named — float4->float truncation): grayScale is a `float` assigned from
// ApplyGainAndBias(float4,...) (Dither.hlsl:75) — HLSL implicit float4->float takes .x. We port
// ApplyGainAndBias on the .r channel only (see dither.metal), matching that truncation.
//
// IsTextureValid (Dither.hlsl:18 cbuffer, line 86 branch) is the host "is a texture wired" flag,
// NOT a Dither.cs InputSlot. We set it host-side from c.inputTexture (1 if wired, else 0) — not a
// port. Resolution cbuffer (b1) TargetWidth/Height filled host-side from the source image dims
// (= Dither.hlsl's framework-injected Resolution), same pattern as Pixelate.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct DitherParams {
  // Dither.hlsl ParamConstants (b0), in declaration order.
  float BlackR, BlackG, BlackB, BlackA;             // ShadowColor (Vec4)
  float WhiteR, WhiteG, WhiteB, WhiteA;             // HighlightColor (Vec4)
  float GrayR, GrayG, GrayB, GrayA;                 // GrayScaleWeights (Vec4, declared/unused)
  float GainAndBiasX, GainAndBiasY;                 // GainAndBias (Vec2)
  float Scale;                                       // Scale (float)
  float Method;                                      // Method (int: <0.5 Bayer / else hash)
  float OffsetX, OffsetY;                            // Offset (Vec2)
  float BlendMode;                                   // BlendMethod (int)
  float IsTextureValid;                              // host flag (1 if input texture wired)
  // 20 floats = 80 bytes = 16 * 5, already a 16-byte multiple (no pad needed).
};

struct DitherResolution {
  // Dither.hlsl Resolution cbuffer (b1): TargetWidth, TargetHeight (framework-injected).
  // Host fills from the SOURCE image dimensions.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum DitherBinding {
  DITHER_Params     = 0,  // constant DitherParams& (b0)
  DITHER_Resolution = 1,  // constant DitherResolution& (b1)
  // texture(0) = Image, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DitherParams) == 80, "DitherParams 80 bytes (16-byte multiple)");
static_assert(sizeof(DitherResolution) == 16, "DitherResolution 16 bytes");
#endif
