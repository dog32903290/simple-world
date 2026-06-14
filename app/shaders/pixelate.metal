// Pixelate: TiXL-ported tile-quantize image filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Pixelate.hlsl (psMain).
// Quantizes UVs into a tile grid, point-samples the tile-center color, multiplies by a Color
// multiplier. The grid is set by Divisor (Divisor>0.5 -> floor(resolution/(Divisor*2)) tiles)
// or, when Divisor<=0.5, by an explicit TileAmount.
//
// FORK (named — Shape texture omitted): Pixelate.hlsl line 75 does
//   return tileShape * imageColor * Color;
// where tileShape = Shape.Sample(texSampler, frac(uv1)). Pixelate.t3 defaults Shape to
// Lib:images/basic/white.png (solid white) so tileShape = (1,1,1,1) for the default node — a
// visual no-op. We omit the second `Shape` texture input and fold its default into a constant
// tileShape = 1.0 (see pixelate_params.h). The `frac(uv1)` / Shape sample is therefore dropped;
// behaviour is identical to the default-wired TiXL node. Custom Shape = follow-up.
//
// FORK (named, DX11->Metal): HLSL SampleLevel(uv,0) -> Metal sample(uv, level(0)); HLSL
// GetDimensions(width,height) -> we pass the source dims in via PixelateResolution (host fills
// from the input texture). Sampler: fixed linear+clamp (Pixelate.t3 WrapMode=Clamp verbatim).
#include <metal_stdlib>
#include "pixelate_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut pixelate_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of Pixelate.hlsl psMain.
fragment float4 pixelate_fs(VSOut in [[stage_in]],
                            texture2d<float> Image       [[texture(0)]],
                            sampler texSampler           [[sampler(0)]],
                            constant PixelateParams& P    [[buffer(PIXELATE_Params)]],
                            constant PixelateResolution& R [[buffer(PIXELATE_Resolution)]]) {
  // HLSL: Image.GetDimensions(width,height); float2 resolution = float2(width,height);
  // We pass the source image dims in via R (PixelateResolution).
  float2 resolution = float2(R.TargetWidth, R.TargetHeight);

  float2 uv = in.texCoord;
  // HLSL keeps uv1 = input.texCoord but only uses it for the (forked-out) Shape sample. Dropped.

  // HLSL: float divisor = Divisor * 2.0; // ensure divisor is a multiple of 2
  float divisor = P.Divisor * 2.0f;

  // HLSL: float2 tileSize = 1.0;  float2 dimensions = floor(resolution / divisor);
  float2 tileSize = float2(1.0f, 1.0f);
  float2 dimensions = floor(resolution / divisor);

  // HLSL: if (Divisor > 0.5) tileSize = 1/dimensions; else tileSize = 1/TileAmount.
  if (P.Divisor > 0.5f) {
    tileSize = 1.0f / dimensions;
    // HLSL also does uv1 *= dimensions (Shape tiling) — forked out with Shape.
  } else {
    tileSize = 1.0f / float2(P.TileAmountX, P.TileAmountY);
    // HLSL also does uv1 *= TileAmount (Shape tiling) — forked out with Shape.
  }

  // FORK: tileShape = Shape.Sample(texSampler, frac(uv1)) -> constant white (default Shape).
  float4 tileShape = float4(1.0f, 1.0f, 1.0f, 1.0f);

  // HLSL: uv = floor(uv / tileSize) * tileSize + tileSize * 0.5; (snap to tile center)
  uv = floor(uv / tileSize) * tileSize + tileSize * 0.5f;

  // HLSL: float4 imageColor = Image.SampleLevel(texSampler, uv, 0);
  float4 imageColor = Image.sample(texSampler, uv, level(0));

  float4 Color = float4(P.ColorR, P.ColorG, P.ColorB, P.ColorA);
  // HLSL: return tileShape * imageColor * Color;
  return tileShape * imageColor * Color;
}
