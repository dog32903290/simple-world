// gradient_raster — shared host helper to rasterize a SwGradient into a 1-row RGBA buffer/texture.
//
// The Gradient->t1 image-filter binding seam (LinearGradient + the 3 other gradient generators) needs
// the SAME gradient-row sampling that GradientsToTexture already does (sample at t=i/(N-1) for i in
// [0,N)). To stop the two from drifting, BOTH go through sampleGradientRowRGBA() here.
//
// TiXL authority: GradientsToTexture.cs (the row-fill loop, :62-76 / :83-86) — t = i/(sampleCount-1),
//   col = gradient.Sample(t), written RGBA. LinearGradient.t3 embeds a GradientsToTexture child with
//   Resolution=512 (t3:159-162) feeding the shader's Gradient t1; that 512 is kLinearGradientRowN.
#pragma once

#include <cstdint>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/sw_gradient.h"  // SwGradient (the consumed currency)

namespace sw {

// The embedded GradientsToTexture child's Resolution in LinearGradient.t3 (:159-162) = 512. The 4
// gradient generators all rasterize the gradient at this width before sampling it in the shader.
inline constexpr int kGradientRowN = 512;

// ★GradientSteps SEAM (RemapColor consumer) — what "GradientSteps" actually means, traced from the .t3:
//   RemapColor.t3 wires its GradientSteps input (58f9492f) DIRECTLY into the embedded GradientsToTexture
//   child's *Resolution* input (1f1838e4) — NOT into any floor/quantize node. GradientsToTexture.cs has
//   NO GradientSteps math at all; its only "stepping" knob is `sampleCount = Resolution.Clamp(1,16384)`
//   (:46). So GradientSteps == the rasterized row's TEXEL COUNT. The visible "banding" is an emergent
//   property: the shader samples that N-texel row with a LINEAR clampedSampler, giving N-1 piecewise-
//   linear segments (low N → few coarse bands; high N → smooth). There is NO `floor(t*steps)/(steps-1)`
//   coordinate quantization anywhere in TiXL — the original hypothesis was wrong; this is the verbatim
//   port of the actual .cs/.t3 routing. [fork-gradientsteps-is-resolution]
//
// We expose this as an OPTIONAL `steps` override on the shared row sampler (default 0 = OFF). When
// steps<=0 the function is byte-identical to before (uses the caller's sampleCount = kGradientRowN for
// every existing generator + GradientsToTexture's own Resolution). When steps>0 it rasterizes the row at
// exactly `steps` texels (clamped to [1,16384] like GradientsToTexture.cs:46), reproducing the stepped
// look. ★ZERO-REGRESSION: every existing call site omits the arg → steps=0 → unchanged smooth path.

// Sample `g` at N uniform t = i/(N-1) for i in [0,N), append RGBA (4 floats/texel) to `out4`.
// Verbatim GradientsToTexture.cs:69-74 / :83-90 per-sample math. Shared by cookGradientsToTexture and
// rasterizeGradientRow so the sampling formula lives in ONE place.
//
// `steps` (default 0): GradientSteps row-resolution override. 0 → use `sampleCount` as-is (smooth,
// byte-identical to all existing callers). >0 → the row is rasterized at min(max(steps,1),16384) texels
// instead (GradientsToTexture.cs:46 Clamp), giving the RemapColor stepped gradient.
// [fork-gradientsteps-is-resolution]
inline void sampleGradientRowRGBA(const SwGradient& g, int sampleCount, std::vector<float>& out4,
                                  int steps = 0) {
  const int n = (steps > 0) ? (steps < 1 ? 1 : (steps > 16384 ? 16384 : steps))  // :46 Clamp(1,16384)
                            : sampleCount;
  out4.reserve(out4.size() + (size_t)n * 4);
  for (int i = 0; i < n; ++i) {
    const float t = (float)i / (n - 1.0f);            // GradientsToTexture.cs:69 (n==1 → 0/0, faithful)
    const simd::float4 c = g.sample(t);               // :70
    out4.push_back(c.x);                              // :71-74
    out4.push_back(c.y);
    out4.push_back(c.z);
    out4.push_back(c.w);
  }
}

// Rasterize `g` into a 1×sampleCount RGBA32Float texture and return it OWNED (caller releases after the
// draw, like cookNGon's dummyTex). Used by the gradient generators to bind a sampleable gradient row at
// shader t1. Shared sampling via sampleGradientRowRGBA (can't drift from GradientsToTexture).
//
// ★FORMAT FORK (named): we ship RGBA32Float. LinearGradient.t3's embedded GradientsToTexture child has
//   no explicit Format -> GradientsToTexture's own .t3 default is R16G16B16A16_Float, AND the shader
//   samples the row with a LINEAR-filtered clampedSampler, so a true byte-for-byte TiXL match would
//   want RGBA16Float (linear interpolation between RGBA16F texels differs slightly from RGBA32F). We
//   ship RGBA32F because (a) it is the engine's existing own-tex gradient format (GradientsToTexture
//   here uses RGBA32F too) and (b) at N=512 the inter-texel error from 32F vs 16F is far below the
//   golden's 2e-3 tolerance. If exact 16F parity is ever required, switch the descriptor to
//   PixelFormatRGBA16Float and pack the row as half (this is the only knob). [fork-grad-row-format-32f]
//
// `steps` (default 0): GradientSteps override, forwarded to sampleGradientRowRGBA. 0 → smooth row at
// `sampleCount` texels (byte-identical to every existing caller). >0 → row is `steps` texels wide
// (clamped 1..16384) — the rasterized texture's width follows the actual row length, so the shader's
// linear clampedSampler produces steps-1 bands. [fork-gradientsteps-is-resolution]
inline MTL::Texture* rasterizeGradientRow(MTL::Device* dev, const SwGradient& g, int sampleCount,
                                          int steps = 0) {
  if (!dev || sampleCount < 1) return nullptr;
  std::vector<float> row;
  sampleGradientRowRGBA(g, sampleCount, row, steps);

  // Actual texel count = row size / 4 (steps>0 overrode sampleCount inside the row sampler). The
  // texture width MUST match the row length, else replaceRegion's rowBytes desync the upload.
  const NS::UInteger texelN = (NS::UInteger)(row.size() / 4);
  if (texelN < 1) return nullptr;

  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatRGBA32Float, texelN, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);
  if (!tex) return nullptr;
  tex->replaceRegion(MTL::Region::Make2D(0, 0, texelN, 1), 0, row.data(),
                     texelN * 4 * sizeof(float));
  return tex;
}

}  // namespace sw
