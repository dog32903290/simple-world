// Shared host<->shader params for the TiXL-ported FeedbackAdjustImage shader — the ONE new fragment
// AdvancedFeedback needs. Authority: external/tixl Operators/Lib/Assets/shaders/img/fx/
// FeedbackAdjustImage.hlsl (cbuffer ParamConstants b0, lines 1-12). The shader is the value-range
// STABILIZER that makes AdvancedFeedback's implicit decay stable (LimitDarks/LimitBrights pull values
// toward the [0.1, 0.8] band, ShiftBrightness/edge add a tiny per-frame nudge, then an HSV hue/sat
// shift). Getting these constants right is what keeps the feedback loop from white-out / black-hole.
//
// cbuffer order (FeedbackAdjustImage.hlsl:1-12), preserved EXACTLY:
//   float LimitDarks; float LimitBrights; float ShiftBrightness; float Hue; float Saturation;
//   float DetectEdges;  float SampleRadius;
// 7 floats = 28 bytes; pad to 32 (16-byte multiple). All-scalar (particle_params.h discipline).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct FeedbackAdjustParams {
  float LimitDarks;       // .hlsl b0 #0 — quadratic pull of darks toward lowerRange (0.1)
  float LimitBrights;     // .hlsl b0 #1 — quadratic pull of brights toward upperRange (0.8)
  float ShiftBrightness;  // .hlsl b0 #2 — constant rgb add (the per-frame brightness nudge)
  float Hue;              // .hlsl b0 #3 — HSV hue shift in degrees (AdvancedFeedback ShiftHue * 360 — see op)
  float Saturation;       // .hlsl b0 #4 — HSV saturation shift (AdvancedFeedback ShiftSaturation)
  float DetectEdges;      // .hlsl b0 #5 — edge amplification (= AdvancedFeedback AmplifyEdges)
  float SampleRadius;     // .hlsl b0 #6 — box/edge sample reach in texels
  float _pad0;            // pad 28 -> 32 (16-byte multiple)
};

enum FeedbackAdjustBinding {
  FEEDBACKADJUST_Params = 0,  // constant FeedbackAdjustParams& (b0)
  // texture(0) = Image, sampler(0) = linear clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FeedbackAdjustParams) == 32, "FeedbackAdjustParams 32 bytes (16-byte multiple)");
#endif
