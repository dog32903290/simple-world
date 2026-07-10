// Shared host<->shader params for the TiXL-ported Steps IMAGE FILTER (lane image_filter, render
// C-bucket bespoke wave — a posterize/quantize screen-quad postfx). Mirrors external/tixl
// Operators/Lib/Assets/shaders/img/fx/Steps.hlsl and Steps.cs/.t3.
//
// TiXL authority: Steps.cs (Image/Count/Bias/Offset/Highlight/HighlightIndex/Ramp/Edge/SmoothRadius/
// UseSuperSampling/Repeat/Resolution inputs) + Steps.t3 (defaults + TWO embedded gradient children
// wired into the SAME GradientsToTexture "Gradients" MultiInput slot — see point_ops_steps.cpp
// header for how that 2-row bake is split into two 1-row LUTs) + Assets/shaders/img/fx/Steps.hlsl
// (the kernel: gray -> Bias2 -> step-quantize -> ramp/edge LUT lookup -> highlight blend -> alpha
// composite, optionally 4x supersampled).
//
// CBUFFER SHAPE (own layout, not a byte-for-byte HLSL cbuffer transcription — Metal setFragmentBytes
// does not need DX11 register packing, so fields are just grouped by role):
//   reg0: StepCount, Bias, Offset, HighlightIndex
//   reg1: Highlight (float4: rgba)
//   reg2: Repeat, UseSuperSampling, TargetWidth, TargetHeight
// SmoothRadius (Steps.cs field, Steps.t3 pin) is DEAD CODE in Steps.hlsl — declared in the HLSL
// cbuffer (:9) but never read anywhere in psMain/ComputeColor (confirmed by a full line-by-line
// read, same class of dead field as ConvertEquirectangle's GetDimensions locals). The NodeSpec still
// exposes a SmoothRadius pin (graph-pin parity with TiXL) but the cook function never reads it and
// this struct carries no slot for it.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct StepsParams {
  // reg0
  float StepCount, Bias, Offset, HighlightIndex;   // TiXL Count/Bias/Offset/HighlightIndex
  // reg1
  float HighlightX, HighlightY, HighlightZ, HighlightW;  // TiXL Highlight, default (1,1,1,0)
  // reg2
  float Repeat;           // TiXL Repeat (bool->float), default true(1.0)
  float UseSuperSampling; // TiXL UseSuperSampling (bool->float), default true(1.0)
  float TargetWidth, TargetHeight;  // output dims (supersampling offset scale)
};

enum StepsBinding {
  STEPS_Params = 0,  // constant StepsParams& (b0)
  // texture(0) = ImageA (Image), texture(1) = RampTex (Ramp gradient LUT row), texture(2) = EdgeTex
  // (Edge gradient LUT row). sampler(0) = the single shared TiXL sampler (all three reads).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(StepsParams) == 48, "StepsParams 48 bytes (3 x 16-byte groups)");
#endif
