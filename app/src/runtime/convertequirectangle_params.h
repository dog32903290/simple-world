// Shared host<->shader params for the TiXL-ported ConvertEquirectangle IMAGE FILTER (lane
// image_filter, render C-bucket bespoke wave). Mirrors external/tixl Operators/Lib/Assets/shaders/
// img/fx/ConvertEquirectangle.hlsl.
//
// TiXL authority: ConvertEquirectangle.cs (Image/Resolution inputs — Resolution just sizes the
// RenderTarget via the compound's GetTextureSize/ResolutionConstBuffer chain, it is NEVER read
// inside psMain) + ConvertEquirectangle.t3 (defaults) + Assets/shaders/img/fx/ConvertEquirectangle.hlsl
// (the kernel: cubemap-direction-from-equirect-uv -> dominant-axis face pick -> sample a 6-face
// horizontal-strip cross image, ported verbatim into convertequirectangle.metal).
//
// ★NO REAL TiXL PARAMS: the HLSL declares `cbuffer Resolution : register(b1) { TargetWidth;
// TargetHeight; }` but NEVER reads either field inside psMain (only a LOCAL shadowing pair from
// `Image.GetDimensions` is used, and even that pair is unused after being computed — dead code in
// the TiXL source, confirmed by a full line-by-line read). So this op takes ZERO real shader
// constants. FaceCount below is NOT a TiXL knob — it is a host-testability seam (see .cpp): the
// HLSL hardcodes the divisor "6" (six cube faces) as a literal in every one of the 6 uv-remap
// branches; we thread that literal through ONE named uniform so a golden's injectBug can corrupt
// the REAL cook path (feed 3.0 instead of 6.0) without touching shader source. Production always
// binds 6.0 — byte-identical to the hardcoded HLSL literal.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ConvertEquirectangleParams {
  float FaceCount;   // host-testability seam only; production always 6.0 (== the HLSL's literal /6)
  float _pad[3];     // pad 4 -> 16 bytes
};

enum ConvertEquirectangleBinding {
  CONVERTEQUIRECTANGLE_Params = 0,  // constant ConvertEquirectangleParams& (b0)
  // texture(0) = Image (the 6-face horizontal-strip cross), sampler(0) = the single TiXL sampler
  // (SamplerState Clamp/Clamp/Clamp, ConvertEquirectangle.t3 :182-201).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ConvertEquirectangleParams) == 16, "ConvertEquirectangleParams 16 bytes");
#endif
