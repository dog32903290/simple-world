// Shared host<->shader params for the TiXL-ported ChromaKey IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaKey.hlsl.
//
// ChromaKey has NO .cs (port authority = ChromaKey.hlsl cbuffer b0, declaration order/types
// copied verbatim — orchestrator guardrail: no invented ports):
//   KeyColor         (float4)  ChromaKey.hlsl:7   — colour to key out (HSB distance reference)
//   Background       (float4)  ChromaKey.hlsl:8   — composite/replacement colour
//   Exposure         (float)   ChromaKey.hlsl:9   — distance gain before Amplify
//   WeightHue        (float)   ChromaKey.hlsl:11  — hue channel weight in the HSB distance
//   WeightSaturation (float)   ChromaKey.hlsl:12  — saturation weight
//   WeightBrightness (float)   ChromaKey.hlsl:13  — brightness weight
//   Amplify          (float)   ChromaKey.hlsl:14  — subtracted from distance (key tightening)
//   Mode             (float)   ChromaKey.hlsl:15  — output selector (4 branches, see kernel)
//   ChokeRadius      (float)   ChromaKey.hlsl:16  — neighbour offset px (min over 4-neighbourhood)
//
// FORK (named): ChromaKey.hlsl b1 TimeConstants (globalTime/time/runTime/beatTime, lines 19-25)
// is unused by psMain -> NOT bound, NOT a port. Image dims read in-shader (GetDimensions) -> no
// Resolution cbuffer. The HLSL `static float PI = 3.141578` (line 50) is declared but unused ->
// dropped. Fixed linear+clamp sampler.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ChromaKeyParams {
  // ChromaKey.hlsl ParamConstants (b0), verbatim order.
  float KeyR, KeyG, KeyB, KeyA;            // KeyColor (Vec4)
  float BgR, BgG, BgB, BgA;                // Background (Vec4)
  float Exposure;                          // distance gain
  float WeightHue;                         // hue weight
  float WeightSaturation;                  // saturation weight
  float WeightBrightness;                  // brightness weight
  float Amplify;                           // distance offset
  float Mode;                              // output selector
  float ChokeRadius;                       // neighbour offset px
  float _pad;                              // pad 15 floats -> 16 = 64 bytes (16-byte multiple)
};

enum ChromaKeyBinding {
  CHROMAKEY_Params = 0,  // constant ChromaKeyParams& (b0)
  // texture(0) = Image, sampler(0); dimensions read in-shader.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ChromaKeyParams) == 64, "ChromaKeyParams 64 bytes (16-byte multiple)");
#endif
