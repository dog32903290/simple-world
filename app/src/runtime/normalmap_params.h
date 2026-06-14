// Shared host<->shader params for the TiXL-ported NormalMap IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl.
//
// NormalMap has NO .cs (port authority = NormalMap.hlsl cbuffer b0, declaration order/types
// copied verbatim — orchestrator guardrail: no invented ports):
//   Impact       (float)   NormalMap.hlsl:6  — gradient->normal tilt strength
//   SampleRadius (float)   NormalMap.hlsl:7  — neighbour offset in pixels (finite difference)
//   Twist        (float)   NormalMap.hlsl:8  — degrees added to the gradient angle
//   Mode         (float)   NormalMap.hlsl:9  — output encoding selector (see kernel branches)
//
// FORK (named): NormalMap.hlsl b1 TimeConstants (globalTime/time/runTime/beatTime, lines 11-17)
// is unused by psMain -> NOT bound, NOT a port. b2 Resolution (TargetWidth/Height, lines 19-23)
// replaced by reading the bound texture's own dimensions in-shader (same fork class as
// DetectEdges) -> no Resolution cbuffer port. Fixed linear+clamp sampler.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct NormalMapParams {
  // NormalMap.hlsl ParamConstants (b0), verbatim order.
  float Impact;        // default 1.0 (NormalMap.hlsl gradient tilt)
  float SampleRadius;  // default 1.0 (neighbour offset px)
  float Twist;         // default 0.0 (degrees added to gradient angle)
  float Mode;          // default 0.0 (encoding selector: <0.5 Gray->RGB flipped-Y)
  // 4 floats = 16 bytes, already a 16-byte multiple.
};

enum NormalMapBinding {
  NORMALMAP_Params = 0,  // constant NormalMapParams& (b0)
  // texture(0) = DisplaceMap (the input image), sampler(0); dimensions read in-shader.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(NormalMapParams) == 16, "NormalMapParams 16 bytes (16-byte multiple)");
#endif
