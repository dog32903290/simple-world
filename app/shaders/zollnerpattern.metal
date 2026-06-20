// ZollnerPattern: TiXL-ported Zöllner optical-illusion pattern generator.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/ZollnerGrid.hlsl.
//
// TiXL: generates a Zöllner optical illusion — horizontal bars with angled hooks that create
// the visual impression of non-parallel lines. Optional Image input modulates bar/hook params
// via the input's R/G/B channels (color-driven morphology). When Image=null (default), all
// affect params contribute 0 → clean deterministic pattern.
//
// HLSL->MSL port forks (named):
//   fork[mod-macro]: HLSL defines `#define mod(x,y) ((x)-(y*floor(x/y)))`.
//     Metal has fmod() but fmod has different sign behaviour for negatives vs floor-based mod.
//     We implement verbatim as `(x) - (y * floor(x/y))` to match TiXL exactly for all UV
//     ranges including negative coordinates.
//   fork[pi-literal]: TiXL uses 3.141578f (a slight approximation of pi). Kept verbatim for
//     byte-identical output. NOT corrected to M_PI_F.
//   fork[cellAspect-static]: TiXL has `static float2 cellAspect = float2(1,2)` used in
//     rotateDeg() but rotateDeg() is never called in psMain (only rotate() is called, which
//     does NOT apply cellAspect). We omit rotateDeg() and cellAspect entirely — dead code.
//   fork[dummy-1x1]: TiXL Image=null default → imgColorForCel samples from a 1×1 black dummy
//     (same convention as Blob/SinForm, Cut 61). All affect values become 0 — identical to
//     TiXL with no Image wired.
//   fork[sampler-clamp]: TiXL _ImageFxShaderSetupStatic has Wrap=Clamp. We use Clamp address
//     mode on the sampler (unlike most filter ops which use Repeat). ZollnerPattern.t3 sets
//     Wrap=Clamp explicitly.
#include <metal_stdlib>
#include "zollnerpattern_params.h"
using namespace metal;

// HLSL mod macro: floor-based modulo (matches TiXL's #define mod(x,y) for all reals).
// Metal fmod() differs from HLSL/GLSL mod() for negative x — floor form is faithful.
static inline float sw_mod(float x, float y) {
  return x - y * floor(x / y);
}

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle vertex shader (same pattern as all other image-filter ops).
vertex VSOut zollnerpattern_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs. texture down
  return o;
}

// Mirror of ZollnerGrid.hlsl psMain (verbatim math, forks named above).
// fork[pi-literal]: 3.141578f matches TiXL's approximation — NOT corrected.
fragment float4 zollnerpattern_fs(
    VSOut in                                   [[stage_in]],
    texture2d<float> ImageA                    [[texture(ZOLLNER_Texture)]],
    sampler texSampler                         [[sampler(ZOLLNER_Sampler)]],
    constant ZollnerPatternParams& p           [[buffer(ZOLLNER_Params)]],
    constant ZollnerPatternResolution& res     [[buffer(ZOLLNER_Resolution)]])
{
  float2 uv = in.texCoord;

  // Derived params (match TiXL local-variable names in psMain).
  float barWidth     = p.BarWidth / 2.0f;
  float hookLength   = p.HookLength / 2.0f;
  float hookWidth    = p.HookWidth / 2.0f;
  float hookRotation = p.HookRotation + 90.0f;

  // Sample optional Image input — zero when 1×1 black dummy (no Image wired).
  float4 imgColorForCel = ImageA.sample(texSampler, uv);

  // Color-channel modulation of bar/hook params (TiXL verbatim).
  barWidth     += imgColorForCel.r * p.RAffects_BarWidth;
  hookLength   += imgColorForCel.r * p.GAffects_HookLength;  // NOTE: TiXL uses .r here too (not .g)
  hookWidth    += imgColorForCel.r * p.GAffects_HookLength;
  hookRotation += imgColorForCel.b * p.BAffects_HookRotation;

  float aspectRatio = res.TargetWidth / res.TargetHeight;
  float edgeSmooth  = p.Feather / (p.ScaleFactor * (p.StretchX + p.StretchY) / 2.0f);

  float2 pt = uv;
  pt -= 0.5f;

  // Rotate canvas (verbatim TiXL: rotateCanvasRad = (-Rotate)/180*pi).
  float rotateCanvasRad = (-p.Rotate) / 180.0f * 3.141578f;
  float sina = sin(-rotateCanvasRad - 3.141578f / 2.0f);
  float cosa = cos(-rotateCanvasRad - 3.141578f / 2.0f);

  pt.x *= aspectRatio;
  pt = float2(
    cosa * pt.x - sina * pt.y,
    cosa * pt.y + sina * pt.x
  );
  pt.x /= aspectRatio;

  // Compute raster cells.
  // divisions = float2(aspectRatio, 1) * 4 / (ScaleFactor * Stretch)
  float2 divisions = float2(aspectRatio, 1.0f) * 4.0f / (p.ScaleFactor * float2(p.StretchX, p.StretchY));
  float2 pCentered = (pt + float2(p.OffsetX, p.OffsetY) / divisions * float2(-1.0f, 1.0f));

  float2 pScaled = pCentered * divisions;
  float2 pInCell = float2(
    pCentered.x * divisions.x,
    sw_mod(pScaled.y, 1.0f)
  );
  int2 cell = int2(pScaled - pInCell);

  // Even rows: flip Y and shift X by 0.5 (TiXL verbatim: cell.y % 2 == 0).
  if (cell.y % 2 == 0) {
    pInCell.y = 1.0f - pInCell.y;
    pInCell.x += 0.5f;
  }

  float2 p1 = pInCell;

  // AmplifyIllusion: shear pInCell.y by x-position (amplifies the illusion effect).
  pInCell.y -= p1.x * p.AmplifyIllusion;

  // Shear X by hookRotation (TiXL verbatim):
  {
    float a    = hookRotation / 180.0f * 3.141578f;
    float sina_h = sin(-a - 3.141578f / 2.0f);
    float cosa_h = cos(-a - 3.141578f / 2.0f);
    float sin_neg_a = sin(-a);

    p1.x = (cosa_h * p1.x - sina_h * pInCell.y) / sin_neg_a;
    p1.x += (float)cell.y * p.RowSwift;
  }

  pInCell.x = sw_mod(p1.x, 1.0f);

  // Smoothstep SDF tests (verbatim from TiXL, decreasing-order smoothstep = inside→1, outside→0).
  float sHookLine = smoothstep(hookWidth + edgeSmooth * aspectRatio,
                               hookWidth - edgeSmooth * aspectRatio,
                               abs(pInCell.x - 0.5f));
  float sHookBar  = smoothstep(hookLength + edgeSmooth,
                               hookLength - edgeSmooth,
                               abs(pInCell.y - 0.5f));
  float sCenterBar = smoothstep(barWidth + edgeSmooth,
                                barWidth - edgeSmooth,
                                abs(pInCell.y - 0.5f));

  float s = max(min(sHookLine, sHookBar), sCenterBar);

  float4 Fill       = float4(p.FillR, p.FillG, p.FillB, p.FillA);
  float4 Background = float4(p.BgR,   p.BgG,   p.BgB,   p.BgA);
  return mix(Background, Fill, s);
}
