// ValueRaster: TiXL-ported adaptive raster grid generator (Phase C leaf).
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/ValueRaster.hlsl psMain.
//
// Renders an adaptive log10-decade raster grid over a coordinate range [RangeX, RangeY].
// Grid lines are AA'd via fwidth-based smoothstep. Includes color-temperature gradient
// by world-space p.y position. Optionally composites over an input texture via MixOriginal.
//
// ============================ HLSL→MSL NOTES (named forks) ============================
// [fork-mod-floor-macro]  HLSL L32: `#define mod(x,y) (x-y*floor(x/y))` — floor-based mod.
//   Used in Grid1D: `float s = p / step; ... frac(s) ...`. MSL has no `frac`; use `fract`.
//   The mod macro itself is NOT used in the final shader (HLSL frac() replaces it) — see HLSL
//   Grid1D L43: `float d = min(frac(s), 1.0-frac(s))` — frac, not mod. No sw_mod needed.
// [fork-frac-fract]  HLSL `frac(s)` -> MSL `fract(s)` (identical semantics, different names).
// [fork-fwidth-available]  HLSL `fwidth(s)` available in PS; MSL `fwidth(s)` available in
//   fragment function. No change.
// [fork-sampler-clamp]  TiXL ValueRaster.t3 sets Wrap=Clamp on _ImageFxShaderSetupStatic.
//   Sampler = linear+ClampToEdge on all axes (matching DX11 TextureAddressMode.Clamp).
// [fork-cbuffer-binding]  HLSL b1 Resolution(TargetWidth/TargetHeight) is framework-injected;
//   bound at Metal fragment cbuffer index 1 (host fills from c.output->width()/height()).
// [fork-generator-dummy]  ValueRaster.cs has an Image input (default null). When no upstream
//   texture is wired the host binds a 1×1 transparent-black dummy, so orgColor=(0,0,0,0).
//   TiXL behaviour with no input: grid composited over black/transparent.
// [fork-log10-helper]  HLSL L34-35: `static const float INV_LN10 = 0.4342944819f` and
//   `log10f(x)=log(max(x,1e-20))*INV_LN10` — verbatim in Metal (log() = natural log in both).
// [fork-distancefromcenter-gradient]  HLSL L96-100: color temperature gradient blended in by
//   p.y. Kept verbatim including the `pow(saturate(abs(p.y*0.1)), 0.3)` formula and the
//   `lerp(1.5, 1, saturate(abs(p.y*100)))` center-line amplification.
#include <metal_stdlib>
#include "valueraster_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut valueraster_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);    // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);          // flip Y: NDC up vs texture down
  return o;
}

// ---- helpers ported verbatim from ValueRaster.hlsl ----------------------------------------

// [fork-log10-helper] HLSL L34-35.
static inline float vr_log10f(float x) {
  const float INV_LN10 = 0.4342944819f;  // 1/ln(10)
  return log(max(x, 1e-20f)) * INV_LN10;
}

// HLSL L37-47: Grid1D — AA distance to nearest grid line in scaled space.
// frac() -> MSL fract(). fwidth() available in fragment.
static inline float Grid1D(float p, float step, float thicknessPx) {
  float s = p / step;
  float fs = fwidth(s);                        // "cells" per pixel (AA width in scaled space)
  float d = min(fract(s), 1.0f - fract(s));   // distance to nearest grid line in [0..0.5]
  float w = thicknessPx * fs;                  // line half-width in same scaled space
  // 1 at line, 0 away from line (smooth AA)
  return 1.0f - smoothstep(w, w + fs, d);
}

// HLSL L49-52: Grid2D — max of two Grid1D (horizontal + vertical lines).
static inline float Grid2D(float2 p, float2 step, float thicknessPx) {
  return max(Grid1D(p.x, step.x, thicknessPx), Grid1D(p.y, step.y, thicknessPx));
}

// ---- psMain → valueraster_fs ---------------------------------------------------------------
// Mirror of ValueRaster.hlsl psMain (lines 54-107).
fragment float4 valueraster_fs(VSOut in                        [[stage_in]],
                               texture2d<float> inputTexture   [[texture(0)]],
                               sampler texSampler              [[sampler(0)]],
                               constant ValueRasterParams&     P [[buffer(VALUERASTER_Params)]],
                               constant ValueRasterResolution& R [[buffer(VALUERASTER_Resolution)]]) {
  float2 uv = in.texCoord;
  // HLSL L58-59: rasterUv flips Y for world-space coordinate mapping.
  float2 rasterUv = float2(uv.x, 1.0f - uv.y);
  // HLSL L59: map UV to world-space p in [Range*.x, Range*.y].
  float2 p = mix(float2(P.RangeXMin, P.RangeYMin),
                 float2(P.RangeXMax, P.RangeYMax),
                 rasterUv);

  // HLSL L61-63: units per pixel in world space (clamped away from zero).
  float2 unitsPerPixel = float2(fwidth(p.x), fwidth(p.y));
  unitsPerPixel = max(unitsPerPixel, 1e-20f);

  // HLSL L65: raw step = units/pixel * Density (controls grid density in world units/cell).
  float2 rawStep = unitsPerPixel * float2(P.DensityX, P.DensityY);

  // HLSL L67-68: compute the decade exponent for each axis.
  float decadeExpX = floor(vr_log10f(rawStep.x));
  float decadeExpY = floor(vr_log10f(rawStep.y));

  // HLSL L70-71: major step = 10^decadeExp per axis.
  float majorStepX = pow(10.0f, decadeExpX);
  float majorStepY = pow(10.0f, decadeExpY);

  // HLSL L73-74: minor step = majorStep / 10.
  float minorStepX = majorStepX / 10.0f;
  float minorStepY = majorStepY / 10.0f;

  // HLSL L77-78: fraction within the decade [0..1) per axis (for fade).
  float fracDecX = vr_log10f(rawStep.x / majorStepX);
  float fracDecY = vr_log10f(rawStep.y / majorStepY);

  // HLSL L80-81: minor line fade per axis (fades out as density increases).
  float minorFadeX = 1.0f - smoothstep(0.1f, 0.9f, fracDecX);
  float minorFadeY = 1.0f - smoothstep(0.1f, 0.9f, fracDecY);

  // HLSL L83-85: major grid masks per axis, combined.
  float majorX = Grid1D(p.x, majorStepX, P.MajorLineWidth);
  float majorY = Grid1D(p.y, majorStepY, P.MajorLineWidth);
  float majorMask = max(majorX, majorY);

  // HLSL L87-90: minor grid masks per axis, faded.
  float minorX = Grid1D(p.x, minorStepX, P.MinorLineWidth) * minorFadeX;
  float minorY = Grid1D(p.y, minorStepY, P.MinorLineWidth) * minorFadeY;
  float minorMask = max(minorX, minorY);

  // HLSL L92: combined line mask.
  float lineMask = max(majorMask, minorMask);

  // HLSL L94: blend BackgroundColor -> LineColor by lineMask * LineColor.a.
  float4 LineColor       = float4(P.LineColorR, P.LineColorG, P.LineColorB, P.LineColorA);
  float4 BackgroundColor = float4(P.BackgroundColorR, P.BackgroundColorG, P.BackgroundColorB, P.BackgroundColorA);
  float4 c = mix(BackgroundColor, LineColor, saturate(lineMask) * LineColor.a);

  // HLSL L96-100: [fork-distancefromcenter-gradient] color temperature gradient by p.y.
  float distanceFromCenter = pow(saturate(abs(p.y * 0.1f)), 0.3f);
  float amplifyCenterLine  = mix(1.5f, 1.0f, saturate(abs(p.y * 100.0f)));
  c *= mix(1.0f,
           saturate(mix(float4(1.0f, 0.6f, 0.2f, 1.0f),
                        float4(0.5f, 0.7f, 1.0f, 1.0f),
                        saturate(p.y))),
           distanceFromCenter) * amplifyCenterLine;

  // HLSL L102: sample input (or 1x1 black dummy when no upstream is wired).
  float4 orgColor = inputTexture.sample(texSampler, uv);

  // HLSL L104-106: alpha composite c over orgColor, clamped to valid range.
  float a   = orgColor.a * saturate(P.MixOriginal) + c.a - orgColor.a * saturate(P.MixOriginal) * c.a;
  float3 rgb = (1.0f - c.a) * clamp(orgColor.rgb, 0.0f, 1.0f) + c.a * c.rgb;
  return float4(clamp(rgb, 0.0f, 10000.0f), clamp(a, 0.0f, 1.0f));
}
