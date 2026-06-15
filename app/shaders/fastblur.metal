// FastBlur: TiXL-ported MULTI-PASS COMPUTE image filter (Dual Kawase++).
// Faithful compute port of TWO pixel shaders the TiXL FastBlur.t3 compound runs via
// _ExecuteFastBlurPasses.cs:
//   down: external/tixl Operators/Lib/Assets/shaders/img/blur/FastBlur-DownsamplePS.hlsl
//   up:   external/tixl Operators/Lib/Assets/shaders/img/blur/FastBlur-UpsampleAcculuatePS.hlsl
//
// PS->compute port (the load-bearing transform):
//   - The HLSL pixel shaders run at the DESTINATION level's resolution and read i.uv (0..1 across the
//     destination), sampling the SOURCE texture with a LINEAR sampler. In compute there is no
//     SV_Position/uv interpolator: we recover uv = (gid + 0.5) / outSize, then sample the source the
//     same way. The bilinear box taps (down) / tent taps (up) are IDENTICAL math once uv is recovered.
//   - SampleLevel(samp, uv, 0) -> Src.sample(samp, uv, level(0)). Both passes sample mip0 of a
//     discrete half-res pyramid texture (NOT a mip chain) — the Dual Kawase _levels[] are separate
//     render targets, mirrored here by per-level cached scratch textures (point_ops_fastblur.cpp).
//   - return c (PS) -> Result.write(c, gid) (RWTexture2D). HLSL t0 (Src/Low) & u0 (Result) are
//     separate register namespaces; MSL [[texture(n)]] is one -> Src @0, Result @1 (fastblur_params.h).
//   - HLSL DisabledBlendState (_ExecuteFastBlurPasses.cs:80): the up pass OVERWRITES the destination,
//     it does NOT additively blend. Result.write is a plain store -> same semantics.
//
// Non-8-divisible output guard: dispatchThreadgroups launches ceil(size/8)*8 threads, so the last
// tile can overrun the output; skip those threads (same idiom as crop.metal).
#include <metal_stdlib>
#include "fastblur_params.h"
using namespace metal;

// Downsample + blur (Kawase 4-tap box, DC gain 1.0). One destination texel = average of 4 bilinear
// fetches on a square around its source-uv. Verbatim FastBlur-DownsamplePS.hlsl psMain.
kernel void fastblur_down_cs(texture2d<float, access::sample> Src    [[texture(FASTBLUR_Src)]],
                             texture2d<float, access::write>  Result [[texture(FASTBLUR_Result)]],
                             constant FastBlurDownParams&     P      [[buffer(FASTBLUR_Params)]],
                             sampler                          samp   [[sampler(FASTBLUR_Sampler)]],
                             uint2                            gid    [[thread_position_in_grid]]) {
  if (gid.x >= Result.get_width() || gid.y >= Result.get_height()) return;

  float2 outSize = float2((float)Result.get_width(), (float)Result.get_height());
  float2 uv = (float2(gid) + 0.5f) / outSize;            // destination uv (matches PS i.uv)

  float2 inv = float2(P.InvSrcSize[0], P.InvSrcSize[1]);
  float2 d = inv * P.OffsetPx;                            // tap offset in SOURCE uv

  // 4 taps on a square (bilinear fetches). Weights sum to 1 (multiply by 0.25 below).
  float4 c =
      Src.sample(samp, uv + float2(-d.x, -d.y), level(0)) +
      Src.sample(samp, uv + float2( d.x, -d.y), level(0)) +
      Src.sample(samp, uv + float2(-d.x,  d.y), level(0)) +
      Src.sample(samp, uv + float2( d.x,  d.y), level(0));

  Result.write(c * 0.25f, gid);
}

// Upsample + blur (9-tap tent, weights NORMALIZED to sum 1 by the host). Verbatim
// FastBlur-UpsampleAcculuatePS.hlsl psMain. Overwrites the destination (no additive blend).
kernel void fastblur_up_cs(texture2d<float, access::sample> Low    [[texture(FASTBLUR_Src)]],
                           texture2d<float, access::write>  Result [[texture(FASTBLUR_Result)]],
                           constant FastBlurUpParams&       P      [[buffer(FASTBLUR_Params)]],
                           sampler                          samp   [[sampler(FASTBLUR_Sampler)]],
                           uint2                            gid    [[thread_position_in_grid]]) {
  if (gid.x >= Result.get_width() || gid.y >= Result.get_height()) return;

  float2 outSize = float2((float)Result.get_width(), (float)Result.get_height());
  float2 uv = (float2(gid) + 0.5f) / outSize;

  float2 inv = float2(P.InvLowSize[0], P.InvLowSize[1]);
  float2 d = inv * P.OffsetPx;                            // tap offset in LOW uv

  float4 c = 0;
  c += Low.sample(samp, uv, level(0)) * P.WCenter;

  // cardinals
  c += Low.sample(samp, uv + float2( d.x, 0), level(0)) * P.WCard;
  c += Low.sample(samp, uv + float2(-d.x, 0), level(0)) * P.WCard;
  c += Low.sample(samp, uv + float2(0,  d.y), level(0)) * P.WCard;
  c += Low.sample(samp, uv + float2(0, -d.y), level(0)) * P.WCard;

  // diagonals
  c += Low.sample(samp, uv + float2( d.x,  d.y), level(0)) * P.WDiag;
  c += Low.sample(samp, uv + float2(-d.x,  d.y), level(0)) * P.WDiag;
  c += Low.sample(samp, uv + float2( d.x, -d.y), level(0)) * P.WDiag;
  c += Low.sample(samp, uv + float2(-d.x, -d.y), level(0)) * P.WDiag;

  Result.write(c, gid);
}
