// MosiacTiling: TiXL-ported quadtree mosaic image filter (lane multi-image, image/fx/stylize).
// Faithful 1:1 port of
//   external/tixl Operators/Lib/Assets/shaders/img/fx/MosiacTiling.hlsl.
//
// The frame is recursively subdivided into quad cells. At each step the FxImage (2nd input, t1) is
// sampled at the four cell corners; if the two diagonal pair-distances + a hashed random jitter fall
// below SubdivisionThreshold the branch STOPS, otherwise the cell halves and recurses (up to
// MaxSubdivisions, clamped 1..7). The 2nd input (FxImage) therefore DRIVES the tiling structure
// per-pixel. The settled cell is filled from the Image (1st input, t0) with a Padding/Feather gap
// (GapColor) drawn between tiles.
//
// Forks (named, DX11->Metal):
//   (1) Sampler: MosiacTiling.t3 (via _multiImageFxSetup) binds ONE sampler s0 used for BOTH Image
//       and FxImage = Filter MinMagMipLinear, WrapMode MirrorOnce. Metal equivalent: linear filter +
//       MirrorClampToEdge address (bound in the op cookMosiacTiling). LOAD-BEARING: the cell-corner
//       sample UVs (uv1..uv4) routinely fall OUTSIDE [0,1] near the frame edges, so the MirrorOnce
//       address mode shapes the edge tiles — must match TiXL.
//   (2) HLSL #define fmod(x,y) = x - y*floor(x/y) is ported VERBATIM (swMod below). This is NOT
//       Metal's fmod (truncated/C-style); the floor form wraps negatives toward +y, which the cell
//       math relies on. Using Metal fmod would diverge for p<0 (left/top of Center).
//   (3) SampleLevel(s, uv, 0) -> sample(s, uv, level(0)) (explicit mip 0). hash12 inlined verbatim
//       from shared/hash-functions.hlsl (grain.metal / radialgradient.metal convention).
//   (4) GetDimensions -> get_width()/get_height(); lerp -> mix; clamp/floor/frac->fract direct.
#include <metal_stdlib>
#include "mosiactiling_params.h"   // MosiacTilingParams, MOSIACTILING_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as combinematerialchannels_vs / displace_vs / blur_vs.
vertex VSOut mosiactiling_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL: #define fmod(x, y) ((x) - (y) * floor((x) / (y)))  — verbatim (NOT Metal's fmod). Vector form.
static inline float2 swMod(float2 x, float2 y) {
  return x - y * floor(x / y);
}

// hash12 verbatim from external/tixl shared/hash-functions.hlsl (frac->fract). [fork-hash12-inline]
static inline float hash12(float2 p) {
  float3 p3 = fract(float3(p.xyx) * 0.1031f);
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.x + p3.y) * p3.z);
}

constant float stepOffset = 0.25f;   // static const float stepOffset = 0.25; (HLSL line 40)

// Mirror of MosiacTiling.hlsl psMain().
fragment float4 mosiactiling_fs(VSOut in [[stage_in]],
                                texture2d<float> image   [[texture(0)]],
                                texture2d<float> fxImage [[texture(1)]],
                                sampler texSampler       [[sampler(0)]],
                                constant MosiacTilingParams& P [[buffer(MOSIACTILING_Params)]]) {
  float width  = (float)image.get_width();
  float height = (float)image.get_height();

  float aspectRatio = width / height;

  float2 uv = in.texCoord;
  float2 p = uv;
  p -= 0.5f + P.Center * (P.Size / float2(aspectRatio, 1.0f));
  p.x *= aspectRatio;

  float2 pInCell = swMod(p, float2(P.Size));
  float2 pCell = uv - pInCell / float2(aspectRatio, 1.0f);   // (declared in HLSL; unused result, kept)
  (void)pCell;

  float currentSize = P.Size;
  int steps = (int)clamp(P.MaxSubdivisions, 1.0f, 7.0f);

  float4 c1, c2, c3, c4;
  float2 uv1 = float2(0.0f), uv2 = float2(0.0f), uv3, uv4, avgUv;

  int step;
  for (step = 0; step < steps; ++step) {
    uv1 = uv - (pInCell - (currentSize) * (0.5f + float2(-stepOffset, -stepOffset))) / float2(aspectRatio, 1.0f);
    uv2 = uv - (pInCell - (currentSize) * (0.5f + float2( stepOffset,  stepOffset))) / float2(aspectRatio, 1.0f);
    uv3 = uv - (pInCell - (currentSize) * (0.5f + float2( stepOffset, -stepOffset))) / float2(aspectRatio, 1.0f);
    uv4 = uv - (pInCell - (currentSize) * (0.5f + float2(-stepOffset,  stepOffset))) / float2(aspectRatio, 1.0f);

    c1 = fxImage.sample(texSampler, uv1, level(0));
    c2 = fxImage.sample(texSampler, uv2, level(0));

    c3 = fxImage.sample(texSampler, uv3, level(0));
    c4 = fxImage.sample(texSampler, uv4, level(0));

    avgUv = (uv1 + uv2) / 2.0f;
    float hashV = hash12(avgUv);

    if (step == steps - 1 ||
        max(length(c1 - c2), length(c3 - c4)) + hashV * P.Randomize < P.SubdivisionThreshold)
      break;

    currentSize /= 2.0f;
    pInCell = swMod(pInCell, float2(currentSize));
  }

  float4 imageColor = image.sample(texSampler, (uv1 + uv2) / 2.0f, level(0));
  float2 pFromCenter = abs(pInCell / (currentSize) - 0.5f) * 2.0f;
  float d = 1.0f - max(pFromCenter.x, pFromCenter.y);
  float gapFactor = smoothstep(P.Padding - P.Feather, P.Padding + P.Feather, d / (float)(step + 1));
  float4 returnColor = mix(P.GapColor, mix(float4(1.0f), imageColor, P.MixOriginal), gapFactor);
  return returnColor;
}
