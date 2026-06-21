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

// Sample `g` at N uniform t = i/(N-1) for i in [0,N), append RGBA (4 floats/texel) to `out4`.
// Verbatim GradientsToTexture.cs:69-74 / :83-90 per-sample math. Shared by cookGradientsToTexture and
// rasterizeGradientRow so the sampling formula lives in ONE place.
inline void sampleGradientRowRGBA(const SwGradient& g, int sampleCount, std::vector<float>& out4) {
  out4.reserve(out4.size() + (size_t)sampleCount * 4);
  for (int i = 0; i < sampleCount; ++i) {
    const float t = (float)i / (sampleCount - 1.0f);  // GradientsToTexture.cs:69
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
inline MTL::Texture* rasterizeGradientRow(MTL::Device* dev, const SwGradient& g, int sampleCount) {
  if (!dev || sampleCount < 1) return nullptr;
  std::vector<float> row;
  sampleGradientRowRGBA(g, sampleCount, row);

  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatRGBA32Float, (NS::UInteger)sampleCount, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);
  if (!tex) return nullptr;
  tex->replaceRegion(MTL::Region::Make2D(0, 0, (NS::UInteger)sampleCount, 1), 0, row.data(),
                     (NS::UInteger)sampleCount * 4 * sizeof(float));
  return tex;
}

}  // namespace sw
