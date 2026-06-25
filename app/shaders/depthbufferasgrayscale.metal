// DepthBufferAsGrayScale: TiXL-ported depth-buffer -> linear-distance grayscale converter.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/post-fx/depth-to-linear.hlsl main.
//
// Kernel (verbatim, depth-to-linear.hlsl):
//   depth = InputTexture[i.xy].r
//   if (depth < 0)  -> dithered fill: ((i.x+i.y)%16 > 0) ? 0 : 1   (unwritten-depth marker)
//   c = Mode < 0.5 ? (-Far*Near)/(depth*(Far-Near) - Far)                 // standard reverse-proj
//                  : (2*Near)/(Far+Near - depth*(Far-Near))               // Legacy DoF mode
//   if (OutrangeMin != 0 || OutrangeMax != 0)
//       c = (c - OutrangeMin)/(OutrangeMax - OutrangeMin)                  // OutputRange remap
//   out = ClampRange > 0.5 ? saturate(c) : c                              // optional saturate
//
// Forks (named, DX11 compute -> Metal):
//   - DX11 compute shader (RWTexture2D<float>, [numthreads(16,16,1)]) -> Metal fullscreen-triangle
//     VS+FS (same fork class as DetectEdges/Tint/Pixelate: the engine's image-filter cook drives a
//     fullscreen draw, not a compute dispatch). Pixel coverage + per-pixel result are identical.
//   - TiXL output is a single-channel R16F RWTexture2D (luminance only); this Metal fork writes the
//     grayscale value into RGB (gray=gray=gray) with alpha=1 so it flows through the RGBA8/RGBA16F
//     image-filter texture stream like every other single-input filter [named fork: gray->RGB
//     replicate]. The numeric gray value per pixel is byte-identical to the HLSL .r write.
//   - HLSL InputTexture[i.xy] (integer load of the depth .r) -> Metal point-sampled at pixel centre
//     (sampler is MinMagMipPoint + ClampToEdge, mirroring the .t3 SamplerState — no interpolation,
//     so the sampled value equals the integer load at deep-interior pixels).
//   - HLSL GetDimensions -> Metal get_width()/get_height() (the TiXL GetTextureSize node feeds the
//     same dims; we read the bound texture's own size, so no Resolution cbuffer port).
#include <metal_stdlib>
#include "depthbufferasgrayscale_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut depthbufferasgrayscale_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of depth-to-linear.hlsl main, evaluated per output pixel.
fragment float4 depthbufferasgrayscale_fs(VSOut in [[stage_in]],
                                          texture2d<float> InputTexture [[texture(0)]],
                                          sampler texSampler            [[sampler(0)]],
                                          constant DepthBufferAsGrayScaleParams& P
                                              [[buffer(DEPTHBUFFERASGRAYSCALE_Params)]]) {
  float width  = (float)InputTexture.get_width();
  float height = (float)InputTexture.get_height();

  // Integer pixel coord (HLSL i.xy) recovered from the fullscreen-triangle uv. Point sampler +
  // clamp -> the sampled .r equals InputTexture[i.xy].r at every interior pixel.
  uint ix = (uint)clamp(in.texCoord.x * width,  0.0f, width  - 1.0f);
  uint iy = (uint)clamp(in.texCoord.y * height, 0.0f, height - 1.0f);

  float n = P.Near;
  float f = P.Far;
  float depth = InputTexture.sample(texSampler, in.texCoord).r;

  if (depth < 0.0f) {
    // Unwritten-depth dither marker (HLSL: ((i.x+i.y)%16 > 0) ? 0 : 1).
    float marker = ((ix + iy) % 16u > 0u) ? 0.0f : 1.0f;
    return float4(marker, marker, marker, 1.0f);
  }

  float c = P.Mode < 0.5f
                ? (-f * n) / (depth * (f - n) - f)
                : (2.0f * n) / (f + n - depth * (f - n));  // Legacy Mode for Depth of Field

  if (P.OutrangeMin != 0.0f || P.OutrangeMax != 0.0f) {
    c = (c - P.OutrangeMin) / (P.OutrangeMax - P.OutrangeMin);
  }

  float g = P.ClampRange > 0.5f ? saturate(c) : c;
  return float4(g, g, g, 1.0f);  // gray->RGB replicate (named fork); alpha opaque
}
