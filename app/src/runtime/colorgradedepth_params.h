// Shared host<->shader params for the TiXL-ported ColorGradeDepth IMAGE FILTER (lane image_filter,
// render C-bucket bespoke wave). Mirrors external/tixl Operators/Lib/Assets/shaders/img/adjust/
// ColorGradeWithDepth.hlsl and ColorGradeDepth.cs/.t3.
//
// TiXL authority: ColorGradeDepth.cs (Texture2d/PreSaturate/Gain/Gamma/Lift/VignetteColor/
// VignetteRadius/VignetteFeather/VignetteCenter/DepthBuffer/Gradient/GradientDepthRange/
// CamNearFarClip inputs) + ColorGradeDepth.t3 (defaults + the embedded GradientsToTexture child that
// bakes the Gradient input to a sampleable row + the FloatsToBuffer cbuffer routing) + Assets/shaders/
// img/adjust/ColorGradeWithDepth.hlsl (the pixel shader: depth->linear-Z->normalizedZ->Gradient LUT
// lookup blended into Lift/Gamma/Gain grading, plus a vignette term).
//
// CBUFFER ORDER (ColorGradeWithDepth.hlsl ParamConstants b0, VERBATIM):
//   float4 Gain; float4 Gamma; float4 Lift; float4 VignetteColor; float2 VignetteCenter;
//   float VignetteRadius; float VignetteBias; float2 GradientDepthRange; float NearClip;
//   float FarClip; float PreSaturate;
// The .t3 FloatsToBuffer feeds those 25 scalars in this exact connection order (ColorGradeDepth.cs
// field order: PreSaturate/Gain/Gamma/Lift/VignetteColor/VignetteRadius/VignetteFeather/
// VignetteCenter/GradientDepthRange/CamNearFarClip.x,.y -> NearClip/FarClip). VignetteFeather (.cs
// name) IS VignetteBias (.hlsl cbuffer field name) — same slot, TiXL's own cs/hlsl naming drift.
//
// HLSL packs into 16-byte registers: reg0=Gain, reg1=Gamma, reg2=Lift, reg3=VignetteColor,
// reg4=(VignetteCenter.xy, VignetteRadius, VignetteBias), reg5=(GradientDepthRange.xy, NearClip,
// FarClip), reg6=(PreSaturate, pad x3). 25 floats packed -> 112 bytes (7 registers x 16B).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ColorGradeDepthParams {
  // reg0
  float GainX, GainY, GainZ, GainW;                   // TiXL Gain, default (0.5,0.5,0.5,0.506)
  // reg1
  float GammaX, GammaY, GammaZ, GammaW;                // TiXL Gamma, default (0.5,0.5,0.5,0.506)
  // reg2
  float LiftX, LiftY, LiftZ, LiftW;                    // TiXL Lift, default (0.5,0.5,0.5,0.25)
  // reg3
  float VignetteColorX, VignetteColorY, VignetteColorZ, VignetteColorW;  // default (0.5,0.5,0.5,0.0)
  // reg4
  float VignetteCenterX, VignetteCenterY;              // TiXL VignetteCenter, default (0,0)
  float VignetteRadius;                                 // TiXL VignetteRadius, default 1.0
  float VignetteBias;                                   // TiXL VignetteFeather, default 1.0
  // reg5
  float GradientDepthRangeX, GradientDepthRangeY;       // TiXL GradientDepthRange, default (0.1,100.0)
  float NearClip;                                        // TiXL CamNearFarClip.X, default 0.01
  float FarClip;                                         // TiXL CamNearFarClip.Y, default 1000.0
  // reg6
  float PreSaturate;                                     // TiXL PreSaturate, default 1.0
  float _pad[3];                                         // pad 100 -> 112 (16-byte multiple)
};

enum ColorGradeDepthBinding {
  COLORGRADEDEPTH_Params = 0,  // constant ColorGradeDepthParams& (b0)
  // texture(0) = InputTexture (Texture2d), texture(1) = DepthBuffer, texture(2) = Gradient (baked
  // LUT row, rasterizeGradientRow). sampler(0) = the single shared sampler (all three reads).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ColorGradeDepthParams) == 112,
              "ColorGradeDepthParams 112 bytes (7 x 16-byte registers)");
#endif
