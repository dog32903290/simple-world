// Raster: TiXL-ported halftone dot+line raster grid, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Raster.hlsl.
// Pattern generator/filter: draws a periodic raster of dots and lines. Optional Image input
// drives per-channel modulation (R->DotSize, G->LineWidth, B->LineRatio) and final composite.
//
// ============================== HLSL->MSL NOTES (named forks) ==============================
// [fork-mod-macro]  Raster.hlsl line 43: `#define mod(x, y) (x - y * floor(x / y))`.
//   All uses in psMain are floor-based mod. MSL uses sw_mod() with identical formula.
// [fork-sampler-clamp]  TiXL Raster.t3 Wrap=Clamp (DX11 TextureAddressMode.Clamp).
//   MSL: ClampToEdge on s and t axes.
// [fork-dummy-tex]  When no Image is wired, host passes 1x1 transparent-black dummy texture
//   so the shader always has a valid texture2d handle. Branch guard
//   `if (RAffects_DotSize > 0 || GAffects_LineWidth > 0 || BAffects_LineRatio > 0)` keeps
//   the sampling path dead when all three are zero (TiXL default). orgColor.a=0 means the
//   final composite alpha-blends transparently, matching TiXL null-Image behaviour.
// [fork-output-clamp]  Raster.hlsl line 134 clamps rgb to [0,10000] and a to [0,1].
//   Ported verbatim (float4(clamp(rgb,0,10000), clamp(a,0,1))).
// [fork-rotation-sign]  Rotation formula verbatim: imageRotationRad = (-Rotate - 90)/180*pi.
//   First rotate: sina=sin(-rad-pi/2), cosa=cos(-rad-pi/2). Second rotate (inverse) negates
//   both sin and cos arguments. Ported exactly as in HLSL.
#include <metal_stdlib>
#include "raster_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle — same convention as checkerboard_vs / rings_vs.
vertex VSOut raster_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// [fork-mod-macro] floor-based modulo, mirrors HLSL `#define mod(x, y) (x - y * floor(x/y))`.
static inline float2 sw_mod2(float2 x, float2 y) { return x - y * floor(x / y); }

// Mirror of Raster.hlsl psMain (lines 46-138), line for line.
fragment float4 raster_fs(VSOut in                             [[stage_in]],
                          texture2d<float> inputTexture        [[texture(0)]],
                          sampler texSampler                   [[sampler(0)]],
                          constant RasterParams& P             [[buffer(RASTER_Params)]],
                          constant RasterResolution& R         [[buffer(RASTER_Resolution)]]) {
  float2 uv = in.texCoord;
  float4 orgColor = inputTexture.sample(texSampler, uv);  // SampleLevel(…, 0.0)

  float aspectRatio = R.TargetWidth / R.TargetHeight;
  float2 p = uv;
  p -= 0.5f;

  float edgeSmooth = P.Feather / P.ScaleFactor;

  p.x *= aspectRatio;

  // Rotate — verbatim from Raster.hlsl lines 61-68
  float imageRotationRad = (-P.Rotate - 90.0f) / 180.0f * 3.141578f;

  float sina = sin(-imageRotationRad - 3.141578f / 2.0f);
  float cosa = cos(-imageRotationRad - 3.141578f / 2.0f);
  p = float2(
      cosa * p.x - sina * p.y,
      cosa * p.y + sina * p.x);

  p.x /= aspectRatio;

  // Compute raster cells — Raster.hlsl lines 71-74
  float2 divisions = float2(R.TargetWidth / P.SizeX, R.TargetHeight / P.SizeY) / P.ScaleFactor;
  float2 p1 = p + float2(P.OffsetX, P.OffsetY) * float2(-1.0f, 1.0f) / divisions;
  float2 gridSize = float2(1.0f / divisions.x, 1.0f / divisions.y);
  float2 pInCell = sw_mod2(p1, gridSize);

  float dotSize    = P.DotSize;
  float lineWidth  = P.LineWidth;
  float lineRatio  = P.LineRatio;

  // Per-channel modulation from image — Raster.hlsl lines 80-105
  if (P.RAffects_DotSize > 0.0f || P.GAffects_LineWidth > 0.0f || P.BAffects_LineRatio > 0.0f) {
    // Rotate position back to image space to sample the cell center colour
    float2 pShifted = p1 - gridSize / 2.0f;
    float2 pInCell2 = sw_mod2(pShifted, gridSize);

    float2 pCel = p.xy - pInCell2 + gridSize / 2.0f;
    float sina2 = sin(-(-imageRotationRad - 3.141578f / 2.0f));
    float cosa2 = cos(-(-imageRotationRad - 3.141578f / 2.0f));

    pCel.x *= aspectRatio;
    pCel = float2(
        cosa2 * pCel.x - sina2 * pCel.y,
        cosa2 * pCel.y + sina2 * pCel.x);
    pCel.x /= aspectRatio;
    pCel += 0.5f;

    float4 imgColorForCel = inputTexture.sample(texSampler, pCel);  // SampleLevel(…, 0.0)
    dotSize   = mix(dotSize,   imgColorForCel.r, P.RAffects_DotSize);
    lineWidth = mix(lineWidth, imgColorForCel.g, P.GAffects_LineWidth);
    lineRatio = mix(lineRatio, imgColorForCel.b, P.BAffects_LineRatio);
  }

  pInCell *= divisions;
  float col = 0.0f;

  float2 pInCellCentered = abs(pInCell - 0.5f) - 0.5f;
  float distanceToCorner = length(pInCellCentered);

  // Draw Dots — Raster.hlsl lines 113-114
  col += smoothstep(dotSize + edgeSmooth, dotSize - edgeSmooth, distanceToCorner);

  // Draw Lines — Raster.hlsl lines 116-123
  float2 distanceToEdge = abs(pInCellCentered);
  float line2 = smoothstep(lineWidth / 2.0f + edgeSmooth, lineWidth / 2.0f - edgeSmooth,
                            min(distanceToEdge.x, distanceToEdge.y));

  line2 *= (P.LineRatio < 0.5f)
               ? smoothstep(lineRatio + edgeSmooth, lineRatio - edgeSmooth, distanceToCorner)
               : smoothstep(lineRatio - edgeSmooth, lineRatio + edgeSmooth, distanceToCorner + 0.5f);

  col += line2;

  col = saturate(col);
  float4 fill = float4(P.FillR, P.FillG, P.FillB, P.FillA);
  float4 bg   = float4(P.BackgroundR, P.BackgroundG, P.BackgroundB, P.BackgroundA);
  float4 c    = mix(bg, fill, col);

  // [fork-output-clamp] Raster.hlsl lines 132-134 — alpha-over composite with original image.
  float a   = orgColor.a * saturate(P.MixOriginal)
              + c.a - orgColor.a * saturate(P.MixOriginal) * c.a;
  float3 rgb = (1.0f - c.a) * clamp(orgColor.rgb, 0.0f, 1.0f) + c.a * c.rgb;
  return float4(clamp(rgb, 0.0f, 10000.0f), clamp(a, 0.0f, 1.0f));
}
