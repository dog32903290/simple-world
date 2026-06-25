// ImageLevels: TiXL-ported histogram/levels visualization-overlay image filter, single pass.
// Faithful line-by-line port of external/tixl Operators/Lib/Assets/shaders/img/fx/ImageLevels.hlsl
// (self-contained — no shared include beyond the params struct).
//
// ============================== LOAD-BEARING PARITY NOTES (named) ==============================
// [fork-sampler]    Fixed POINT filter + WRAP address mode = ImageLevels.t3's _ImageFxShaderSetup2
//   (Filter=MinMagMipPoint; the Setup2 Wrap input defaults to "Wrap", and ImageLevels.t3 leaves it
//   unset). Set in the cook, not here. AddressW=Clamp in the Setup2 SamplerState is W-only (3D),
//   irrelevant for 2D sampling.
// [fork-time-folded] The .hlsl reads cbuffer b1 (TimeConstants) field `beatTime` for the clamp-
//   highlight zebra pattern and cbuffer b2 (Resolution) TargetWidth/TargetHeight for the aspect
//   ratio. We fold both into the single ImageLevelsParams cbuffer (BeatTime / TargetWidth /
//   TargetHeight) rather than bind three cbuffers — same values, faithful. globalTime/time/runTime
//   are NOT read by psMain so they are dropped.
// NOTE: this op is a VISUALIZATION overlay (draws the levels curve / subdivision lines / clamp
//   zebra over the dimmed original), not a color-grade. Ported verbatim; no visual improvement.
#include <metal_stdlib>
#include "imagelevels_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
// Same convention as colorgrade_vs / pixelate_vs across the image filters.
vertex VSOut imagelevels_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// HLSL helper SubdivisionLine (ImageLevels.hlsl lines 40-50). `lineThickness` was a static global in
// HLSL; here it is passed explicitly (Metal has no mutable module-scope statics across functions).
static float SubdivisionLine(float n, float r, float lineThickness, float2 Range) {
  float t = lineThickness * (Range.y - Range.x);
  float f = 1.0f - saturate(t * r * 3.0f);
  float x = (fmod(n, ((1.0f + 1.0f / 255.0f) / r)) < t) ? 1.0f : 0.0f;
  return x * f;
}

// Mirror of ImageLevels.hlsl psMain (lines 52-128), line for line.
fragment float4 imagelevels_fs(VSOut psInput [[stage_in]],
                               texture2d<float> inputTexture [[texture(0)]],
                               sampler texSampler            [[sampler(0)]],
                               constant ImageLevelsParams& P [[buffer(IL_Params)]]) {
  float2 Center       = float2(P.CenterX, P.CenterY);
  float  Width        = P.Width;
  float  Rotation     = P.Rotation;
  float2 Range        = float2(P.RangeX, P.RangeY);
  float  ShowOriginal = P.ShowOriginal;
  float  TargetWidth  = P.TargetWidth;
  float  TargetHeight = P.TargetHeight;
  float  beatTime     = P.BeatTime;

  uint width  = inputTexture.get_width();
  uint height = inputTexture.get_height();

  float2 uv = psInput.texCoord;
  float4 orgColor = inputTexture.sample(texSampler, uv);

  float aspectRation = TargetWidth / TargetHeight;
  float2 p = uv;
  p -= 0.5f;
  p.x *= aspectRation;

  float radians = Rotation / 180.0f * 3.141578f;
  float2 angle = float2(sin(radians), cos(radians));
  float distanceFromCenter = dot(p - Center * float2(1.0f, -1.0f), angle);
  float normalizedDistance = -distanceFromCenter / Width;
  float4 visibleOrgColor = mix(float4(0.0f, 0.0f, 0.0f, 0.0f), orgColor, ShowOriginal);

  float lineThickness = 1.1f / float(height) / Width;
  float nInRange = (normalizedDistance) * (Range.y - Range.x) + Range.x;
  float4 subdivisionLines = (SubdivisionLine(nInRange, 8.0f, lineThickness, Range) * float4(0.0f, 0.0f, 0.0f, 0.3f) +
                             SubdivisionLine(nInRange, 1.0f, lineThickness, Range) * float4(0.0f, 0.0f, 0.0f, 1.0f) +
                             SubdivisionLine(nInRange, 256.0f, lineThickness, Range) * float4(0.0f, 0.0f, 0.0f, 0.3f)) *
                            (normalizedDistance < 1.0f ? 1.0f : 0.0f);

  float2 pixelposition = uv * float2(float(width), float(height));

  float2 pOnLine = p;
  pOnLine += (-distanceFromCenter) * angle;
  pOnLine.x /= aspectRation;
  pOnLine += 0.5f;
  float4 colorOnLine = inputTexture.sample(texSampler, pOnLine);
  colorOnLine = (colorOnLine - Range.x) / (Range.y - Range.x);

  // Curves...
  float4 curveColor = 0.0f;
  float4 curveShape2 = smoothstep(float4(normalizedDistance), float4(normalizedDistance + lineThickness), colorOnLine.rgba);

  float channelAlpha = 0.1f;
  float n = -0.2f;
  float4 curveShape = curveShape2.r * float4(1.0f, n, n, channelAlpha)
                      + curveShape2.g * float4(n, 1.0f, n, channelAlpha)
                      + curveShape2.b * float4(n, n, 1.0f, channelAlpha)
                      + curveShape2.a * float4(1.0f, 1.0f, 1.0f, channelAlpha);

  float4 curveLines = smoothstep(float4(normalizedDistance) + lineThickness * float4(1.0f, 1.0f, 1.0f, 1.5f), float4(normalizedDistance), colorOnLine.rgba)
                      * smoothstep(float4(normalizedDistance) - lineThickness * float4(1.0f, 1.0f, 1.0f, 1.5f), float4(normalizedDistance), colorOnLine.rgba)
                      * float4(2.0f, 2.0f, 2.0f, 1.5f * ((fmod(pixelposition.x + pixelposition.y, 10.0f) > 5.0f) ? 1.0f : 0.0f));

  curveLines.a += length(curveLines.rgb) * 0.3f;
  if (normalizedDistance < 0.0f) {
    curveShape = 0.0f;
  }

  curveColor.rgba = curveLines + curveShape * 0.6f + subdivisionLines;

  if (normalizedDistance < 0.0f)
    curveColor *= 0.1f;

  // Zebra pattern for highlight clamping.
  // HLSL line 117: `float3 clamping = (col.rgb > 1 || col.rgb < 0) ? float3(1,1,1) : float3(0,0,0);`
  // — the vector `?:` is COMPONENT-WISE select (per-channel), so we use select() per channel, not any().
  float3 clamping = select(float3(0.0f), float3(1.0f),
                           (colorOnLine.rgb > 1.0f) || (colorOnLine.rgb < 0.0f));

  float pattern = (fmod(pixelposition.x + pixelposition.y + 0.5f + beatTime * 100.0f, 8.0f) < 2.0f) ? 1.0f : -1.0f;

  float3 clampedAreaRGB = clamping * curveShape.rgb * ((normalizedDistance > 1.0f || normalizedDistance < 0.0f) ? 1.0f : 0.0f);
  float4 clampedArea = float4(clampedAreaRGB, length(clampedAreaRGB) * pattern * 0.5f);

  bool isBetweenCurveRange = normalizedDistance >= 0.0f && normalizedDistance <= 1.0f;

  return curveColor + clampedArea + visibleOrgColor * (isBetweenCurveRange ? 0.2f : 1.0f);
}
