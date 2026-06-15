// Shared host<->shader params for the TiXL-ported KochKaleidoskope IMAGE FILTER (image/fx/distort).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/KochKaleidoscope.hlsl cbuffer
// ParamConstants(b0) — note the cbuffer ORDER differs from KochKaleidoskope.cs [Input] order; the
// host struct follows the cbuffer (this struct), the NodeSpec ports follow the .cs (point_ops file).
//
//   cbuffer ParamConstants(b0):  Scale, CenterX, CenterY, OffsetX, OffsetY,
//                                Angle, Steps, ShadeSteps, ShadeFolds, Rotate     (10 floats)
//
// The HLSL also declares cbuffer Resolution(b2) { TargetWidth, TargetHeight } which feeds `aspect`.
// We fold those two into THIS struct (b0) as a TexCookCtx-derived seam (output dims), so the kernel
// reads one cbuffer. (TimeConstants(b1) is declared-but-unread in the .hlsl → omitted, per spec.)
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct KochKaleidoscopeParams {
  // KochKaleidoscope.hlsl cbuffer ParamConstants(b0) — VERBATIM field order:
  float Scale;        // .hlsl:11
  float CenterX;      // .hlsl:12
  float CenterY;      // .hlsl:13
  float OffsetX;      // .hlsl:15
  float OffsetY;      // .hlsl:16
  float Angle;        // .hlsl:18
  float Steps;        // .hlsl:19 (int in .cs, float in cbuffer — loop bound cast to int in MSL)
  float ShadeSteps;   // .hlsl:20
  float ShadeFolds;   // .hlsl:21
  float Rotate;       // .hlsl:22
  // KochKaleidoscope.hlsl cbuffer Resolution(b2) folded in (psMain aspect = TargetWidth/TargetHeight
  // and the AA offset uses inputTexture.GetDimensions width/height — we pass output dims here).
  float TargetWidth;  // .hlsl:107
  float TargetHeight; // .hlsl:108
  float _pad[4];      // pad 12 -> 16 floats (16-byte multiple = 64 bytes)
};

enum KochKaleidoscopeBinding {
  KK_Params = 0,  // constant KochKaleidoscopeParams& (b0)
  // texture(0) = inputTexture, sampler(0) = wrap(repeat); bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(KochKaleidoscopeParams) == 64,
              "KochKaleidoscopeParams 64 bytes (16 floats, 16-byte multiple)");
#endif
