// Shared host<->shader params for the TiXL-ported ColorGrade IMAGE FILTER (image/color).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/ColorGrade.hlsl + ColorGrade.cs/.t3.
//
// cbuffer layout follows the .hlsl ParamConstants(b0) VERBATIM (NOT the .cs [Input] order — the
// NodeSpec ports follow the .cs; the host struct follows the shader cbuffer):
//   float4 Gain; float4 Gamma; float4 Lift; float4 VignetteColor;
//   float2 VignetteCenter; float VignetteRadius; float VignetteBias; float PreSaturate;
//
// NAME FORK [fork-feather-is-bias]: the .cs input is named "VignetteFeather" but it feeds the
// shader's cbuffer field "VignetteBias" (ColorGrade.t3 wires the VignetteFeather slot
// e94da387 straight into the _ImageFxShaderSetupStatic Params multi-input at the VignetteBias
// cbuffer position). Same value, different name — we name the host field VignetteBias to match
// the .hlsl, and the NodeSpec port "VignetteFeather" to match the .cs.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ColorGradeParams {
  // --- mirrors .hlsl cbuffer ParamConstants(b0) order ---
  float GainR, GainG, GainB, GainA;            // float4 Gain
  float GammaR, GammaG, GammaB, GammaA;        // float4 Gamma
  float LiftR, LiftG, LiftB, LiftA;            // float4 Lift
  float VigColorR, VigColorG, VigColorB, VigColorA;  // float4 VignetteColor
  float VigCenterX, VigCenterY;                // float2 VignetteCenter
  float VignetteRadius;                        // float VignetteRadius
  float VignetteBias;                          // float VignetteBias (.cs name: VignetteFeather)
  float PreSaturate;                           // float PreSaturate
  float _pad[3];                               // pad 21 -> 24 floats (96 bytes, 16-byte multiple)
};

enum ColorGradeBinding {
  CG_Params = 0,  // constant ColorGradeParams& (b0)
  // texture(0) = inputTexture, sampler(0) = linear+repeat (ColorGrade.t3 SetupStatic defaults
  // Filter=MinMagMipLinear, Wrap=Wrap).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ColorGradeParams) == 96,
              "ColorGradeParams: 21 floats + 3 pad = 24 floats = 96 bytes");
#endif
