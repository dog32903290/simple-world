// Shared host<->shader params for the TiXL-ported Fxaa IMAGE FILTER.
// TiXL authority: Operators/Lib/image/use/Fxaa.cs (Image/Preset/KeepAlpha) +
// Fxaa.t3 (defaults: Preset=0, KeepAlpha=false) + Assets/shaders/img/use/FXAA.hlsl
// (NVIDIA FXAA 3.11, single fullscreen pass, single input texture t0, no time/second-tex/feedback).
//
// FORK (named, DX11->Metal): TiXL selects the FXAA preset (0..5) at COMPILE time via
// `#define FXAA_PRESET`, baking EDGE_THRESHOLD / SEARCH_STEPS / SEARCH_ACCELERATION /
// SUBPIX_* as compile-time constants. We have one precompiled metallib, so instead the HOST
// expands the chosen preset's constants into this b0 cbuffer and the shader reads them at
// RUNTIME. The arithmetic is byte-for-byte the same FXAA math — only the constant SOURCE
// moves from compile-time #define to a runtime uniform. SEARCH_STEPS becomes the loop bound,
// SEARCH_ACCELERATION / SUBPIX_FASTER become runtime branches.
//
// b0 = FxaaParams: rcpFrame (1/w, 1/h) + KeepAlpha + the unrolled preset constant set.
// b1 = FxaaResolution: TargetWidth/TargetHeight (mirrors the HLSL Resolution cbuffer; host
//      fills from c.output->width()/height()).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct FxaaParams {
  // HLSL cbuffer b0 (ParamConstants): rcpFrame + KeepAlpha.
  float rcpFrameX;   // 1.0 / TargetWidth
  float rcpFrameY;   // 1.0 / TargetHeight
  float KeepAlpha;   // HLSL float KeepAlpha (bool cast); 0 -> alpha=1, !=0 -> sample alpha
  float edgeThreshold;      // FXAA_EDGE_THRESHOLD     (preset-derived)
  float edgeThresholdMin;   // FXAA_EDGE_THRESHOLD_MIN (preset-derived)
  float searchThreshold;    // FXAA_SEARCH_THRESHOLD   (preset-derived, always 1/4)
  float subpixCap;          // FXAA_SUBPIX_CAP         (preset-derived)
  float subpixTrim;         // FXAA_SUBPIX_TRIM        (preset-derived; trimScale derived in shader)
  int   searchSteps;        // FXAA_SEARCH_STEPS       (preset-derived loop bound)
  int   searchAccel;        // FXAA_SEARCH_ACCELERATION(preset-derived 1..4)
  int   subpixFaster;       // FXAA_SUBPIX_FASTER      (preset-derived 0/1; only preset 0 uses 1)
  int   _pad0;              // pad to 16-byte multiple (44 -> 48)
};

struct FxaaResolution {
  // HLSL cbuffer b1 (Resolution): TargetWidth, TargetHeight.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum FxaaBinding {
  FXAA_Params      = 0,  // constant FxaaParams& (b0)
  FXAA_Resolution  = 1,  // constant FxaaResolution& (b1)
  // texture(0) = Image, sampler(0) = linear+clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FxaaParams) == 48, "FxaaParams 48 bytes");
static_assert(sizeof(FxaaResolution) == 16, "FxaaResolution 16 bytes");
#endif
