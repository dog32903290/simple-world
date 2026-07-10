// mathv_ref_getimagebrightness.h — CPU reference oracle for GetImageBrightness (TEXTURE_COMPUTE_SEAM stage
// 3: SRV-tex read → uint UAV reduction). TRANSCRIBED from the TiXL HLSL (P5-safe — the authority is the
// .hlsl text, never sw's own output):
//   external/tixl/Operators/Lib/Assets/shaders/img/analyze/cs-GetImageBrightness.hlsl (main, :33-48)
//     float4 color = InputTexture.Load(DTid);
//     float luminance = (color.r + color.g + color.b) / 3 * color.a;
//     uint scaledLuminance = (uint)(luminance * scaleFactor);
//     InterlockedAdd(localSum, scaledLuminance); ... InterlockedAdd(ResultBuffer[0], localSum);
//   → ResultBuffer[0] = Σ over all in-bounds texels of (uint)(((r+g+b)/3·a)·scaleFactor). The groupshared
//     partial sums are a perf optimization; the total is order-independent (integer add). The `clear` pass
//     (which zeroes ResultBuffer[0]) is folded to buffer zero-init (see the kernel header).
//
// FLOAT DISCIPLINE: the per-texel math is done in FLOAT (not double) so the truncation `(uint32_t)(x)`
// matches the MSL kernel's `(uint)(x)` texel-for-texel. The sole residual divergence is the compiler's
// choice for `/3.0f` (IEEE divide vs Metal fast-math reciprocal), ~1 ULP — which can flip a truncation only
// for a texel whose product lands within ~1 ULP of an integer. The mathv sweep chooses fixtures that keep
// products clear of integer boundaries so the sum is EXACT; the golden compares with the same oracle.
#pragma once

#include <cstdint>

namespace sw {
namespace mathv_ref {

// One texel's contribution: (uint)(((r+g+b)/3·a)·scaleFactor). Float, truncating cast (colors ≥ 0 → floor).
inline uint32_t getImageBrightnessTexel(float r, float g, float b, float a, uint32_t scaleFactor) {
  const float luminance = (r + g + b) / 3.0f * a;       // .hlsl:34 verbatim
  return (uint32_t)(luminance * (float)scaleFactor);    // .hlsl:37 (float→uint truncates toward 0)
}

// The full reduction: sum every in-bounds RGBA texel's contribution. `rgba` is W·H·4 floats, row-major.
inline uint32_t getImageBrightnessSum(const float* rgba, uint32_t W, uint32_t H, uint32_t scaleFactor) {
  uint32_t sum = 0;
  const uint32_t n = W * H;
  for (uint32_t i = 0; i < n; ++i)
    sum += getImageBrightnessTexel(rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3],
                                   scaleFactor);
  return sum;
}

}  // namespace mathv_ref
}  // namespace sw
