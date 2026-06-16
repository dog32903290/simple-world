// FraserGrid: TiXL-ported per-cell Fraser-grid pattern generator.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/FraserGrid.hlsl psMain.
//
// Generates a Fraser grid: a repeating pattern of diamond-shaped cells (rotated boxes) with a
// center bar and gap bars, controlled by ShapeSize/BarWidth/BorderWidth/RotateShapes.
// Optionally tinted from an input image (RAffects_BarWidth, GAffects_ShapeSize, BAffects_LineRatio).
//
// Kernel verbatim translation (FraserGrid.hlsl lines 70-192):
//   uv = texCoord
//   orgColor = inputTexture.SampleLevel(s, uv, 0)
//   aspectRatio = TargetWidth/TargetHeight
//   cellAspect = (Size.x/Size.y, 1)
//   edgeSmooth = Feather / (ScaleFactor * (Size.x + Size.y) / 2) * 100
//   p = uv - 0.5
//   Rotate by rotateImageRad = (-Rotate - 90)/180 * pi  (with aspect correction)
//   divisions = (TargetWidth/Size.x, TargetHeight/Size.y) / ScaleFactor
//   cell decomposition -> pInCell in [0..1]^2, optional row-flip, RowSwift x-shift
//   per-cell image sample for R/G/B affects
//   3 shapes (s1a, s1b, s2) = rotateDeg box, smoothstepped by ShapeSize
//   centerBar = rotate by asin(barWidth*4+edgeSmooth/4), smoothstep on abs(pcb.x)
//   gapBarA/B = same pcb, offset by ±2*barWidth
//   fillA = 1 * lerp(1,s1a,gapBarA) * lerp(1,s1b,gapBarB) * centerBar
//   background = 1 - s1aBorder * s1bBorder * s2border
//   cBorderOrBackground = lerp(Background, FillB, background)
//   cFill = lerp(FillA, cBorderOrBackground, fillA)
//   return cFill
//
// Forks (named, HLSL->Metal):
//   - DX11 PS -> Metal fullscreen-triangle VS+FS (same pattern as SinForm/CheckerBoard).
//   - HLSL static float2 cellAspect -> MSL thread_local or just recomputed inline (MSL has no
//     static mutable inside fragment functions; done inline per invocation as in the kernel).
//   - HLSL #define mod(x,y) ((x)-(y*floor(x/y))) -> MSL: use Metal built-in fmod() is WRONG
//     (fmod truncates toward zero); use (x - y*floor(x/y)) explicitly to match HLSL definition.
//   - HLSL (int) cast -> MSL (int) cast: both truncate toward zero (floor for non-negative inputs).
//     FraserGrid uses (int(cellId.y+1000.0001)+10)%2 for even/odd row detection — same semantics.
//   - Sampler: TiXL .t3 Wrap=Clamp -> MTLSamplerAddressModeClampToEdge (host-set).
//   - pi constant: HLSL uses 3.141578 (approximate) verbatim — preserved in this port.
//   - Resolution cbuffer (b1) bound at Metal fragment buffer index 1 (matches SinForm/Checkerboard).
//
// [no-new-helpers]: This shader uses no helpers beyond the built-in Metal stdlib.
#include <metal_stdlib>
using namespace metal;

// FraserGrid params struct (mirrors frasergrid_params.h exactly — fields must stay in sync).
struct FraserGridParams {
  float FillAR, FillAG, FillAB, FillAA;
  float FillBR, FillBG, FillBB, FillBA;
  float BgR, BgG, BgB, BgA;
  float SizeX, SizeY;
  float OffsetX, OffsetY;
  float ScaleFactor;
  float Rotate;
  float Feather;
  float RotateShapes;
  float ShapeSize;
  float BarWidth;
  float BorderWidth;
  float RowSwift;
  float RAffects_BarWidth;
  float GAffects_ShapeSize;
  float BAffects_LineRatio;
};

struct FraserGridResolution {
  float TargetWidth;
  float TargetHeight;
  float _pad[2];
};

struct VertexOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen-triangle vertex shader (same convention as sinform/checkerboard).
vertex VertexOut frasergrid_vs(uint vid [[vertex_id]]) {
  VertexOut out;
  float2 pos = float2((vid == 1) ? 3.0 : -1.0, (vid == 2) ? 3.0 : -1.0);
  out.position = float4(pos, 0.0, 1.0);
  out.texCoord = float2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);
  return out;
}

// HLSL mod macro: #define mod(x,y) ((x)-(y*floor(x/y)))  — floor-based, NOT fmod (which truncates).
static float fg_mod(float x, float y) { return x - y * floor(x / y); }
static float2 fg_mod2(float2 x, float2 y) { return x - y * floor(x / y); }

// box(p) = max(abs(p.x), abs(p.y)) — Chebyshev / L-inf norm used as diamond SDF.
static float fg_box(float2 p) { return max(abs(p.x), abs(p.y)); }

// rotateDeg: rotate p by angleInDeg degrees, with cellAspect pre-scale.
// Mirrors FraserGrid.hlsl lines 47-57 verbatim.
// NOTE: HLSL uses 3.141578 (approximate pi) — preserved verbatim for parity.
static float2 fg_rotateDeg(float2 p, float angleInDeg, float2 cellAspect) {
  p *= cellAspect;
  float a = angleInDeg / 180.0f * 3.141578f;
  float sina = sin(-a - 3.141578f / 2.0f);
  float cosa = cos(-a - 3.141578f / 2.0f);
  return float2(cosa * p.x - sina * p.y,
                cosa * p.y + sina * p.x);
}

// rotate: rotate p by angle (radians), no cellAspect scale.
// Mirrors FraserGrid.hlsl lines 59-67 verbatim.
static float2 fg_rotate(float2 p, float angle) {
  float sina = sin(-angle - 3.141578f / 2.0f);
  float cosa = cos(-angle - 3.141578f / 2.0f);
  return float2(cosa * p.x - sina * p.y,
                cosa * p.y + sina * p.x);
}

fragment float4 frasergrid_fs(
    VertexOut in [[stage_in]],
    texture2d<float> inputTexture [[texture(0)]],
    sampler texSampler [[sampler(0)]],
    constant FraserGridParams& p [[buffer(0)]],
    constant FraserGridResolution& res [[buffer(1)]])
{
  float2 uv = in.texCoord;

  // Repack params into local HLSL-named variables for direct formula correspondence.
  float4 FillA     = float4(p.FillAR, p.FillAG, p.FillAB, p.FillAA);
  float4 FillB_    = float4(p.FillBR, p.FillBG, p.FillBB, p.FillBA);
  float4 Background = float4(p.BgR, p.BgG, p.BgB, p.BgA);
  float2 Size      = float2(p.SizeX, p.SizeY);
  float2 Offset    = float2(p.OffsetX, p.OffsetY);
  float ScaleFactor = p.ScaleFactor;
  float Rotate      = p.Rotate;
  float Feather     = p.Feather;
  float RotateShapes = p.RotateShapes;
  float ShapeSize   = p.ShapeSize;
  float BarWidth    = p.BarWidth;
  float BorderWidth = p.BorderWidth;
  float RowSwift    = p.RowSwift;
  float TargetWidth  = res.TargetWidth;
  float TargetHeight = res.TargetHeight;

  // [HLSL line 73] orgColor = inputTexture.SampleLevel(texSampler, uv, 0.0)
  // (When no Image wired, host binds 1x1 transparent-black dummy -> orgColor=(0,0,0,0) which
  //  means RAffects_BarWidth/etc. have no effect — matches TiXL behaviour with Image=null.)
  float4 orgColor = inputTexture.sample(texSampler, uv, level(0.0f));

  // [HLSL line 75] aspectRatio = TargetWidth / TargetHeight
  float aspectRatio = TargetWidth / TargetHeight;

  // [HLSL line 76] cellAspect = (Size.x / Size.y, 1)
  float2 cellAspect = float2(Size.x / Size.y, 1.0f);

  // [HLSL line 78] edgeSmooth
  float edgeSmooth = Feather / (ScaleFactor * (Size.x + Size.y) / 2.0f) * 100.0f;

  // [HLSL line 80-81] p = uv - 0.5
  float2 pt = uv;
  pt -= 0.5f;

  // [HLSL lines 83-94] Rotate the UV plane by Rotate degrees (with aspect correction).
  float rotateImageRad = (-Rotate - 90.0f) / 180.0f * 3.141578f;
  float sina = sin(-rotateImageRad - 3.141578f / 2.0f);
  float cosa = cos(-rotateImageRad - 3.141578f / 2.0f);
  pt.x *= aspectRatio;
  pt = float2(cosa * pt.x - sina * pt.y,
              cosa * pt.y + sina * pt.x);
  pt.x /= aspectRatio;

  // [HLSL lines 98-111] Compute raster cells.
  float2 divisions = float2(TargetWidth / Size.x, TargetHeight / Size.y) / ScaleFactor;
  float2 p1 = pt + Offset * float2(-1.0f, 1.0f) / divisions;

  float2 ppp = fg_mod2(p1, float2(1.0f / divisions.x, 1.0f / divisions.y));
  float2 pInCell = ppp * divisions;
  float2 cellId = (p1 - ppp) * divisions;

  // [HLSL line 106] Even/odd row flip.
  // HLSL: (int(cellId.y + 1000.0001) + 10) % 2 == 1 -> flip pInCell.y
  if (((int(cellId.y + 1000.0001f) + 10) % 2) == 1) {
    pInCell.y = 1.0f - pInCell.y;
  }
  // [HLSL line 111] RowSwift x-shift.
  pInCell.x = fg_mod(pInCell.x + cellId.y * RowSwift, 1.0f);

  // [HLSL lines 113-120] Per-cell image modulation of barWidth/shapeSize/borderWidth.
  float barWidth    = BarWidth;
  float shapeSize   = ShapeSize;
  float borderWidth = BorderWidth;

  float4 imgColorForCel = inputTexture.sample(texSampler, uv, level(0.0f));
  barWidth    = mix(barWidth,    imgColorForCel.r, p.RAffects_BarWidth);
  shapeSize   = mix(shapeSize,   imgColorForCel.r, p.GAffects_ShapeSize);
  borderWidth = mix(borderWidth, imgColorForCel.b, p.BAffects_LineRatio);

  // [HLSL lines 152-164] Three shapes using rotateDeg box.
  // Shape 1a (left): centered at (0, 0.5)
  float s1a_raw    = fg_box(fg_rotateDeg(pInCell - float2(0.0f, 0.5f), RotateShapes, cellAspect));
  float s1aBorder  = smoothstep(shapeSize - edgeSmooth, shapeSize + edgeSmooth, s1a_raw - borderWidth);
  float s1a        = smoothstep(shapeSize - edgeSmooth, shapeSize + edgeSmooth, s1a_raw);

  // Shape 1b (right): centered at (1, 0.5)
  float s1b_raw    = fg_box(fg_rotateDeg(pInCell - float2(1.0f, 0.5f), RotateShapes, cellAspect));
  float s1bBorder  = smoothstep(shapeSize - edgeSmooth, shapeSize + edgeSmooth, s1b_raw - borderWidth);
  float s1b        = smoothstep(shapeSize - edgeSmooth, shapeSize + edgeSmooth, s1b_raw);

  // Shape 2 (center): centered at (0.5, 0.5)
  float s2_raw     = fg_box(fg_rotateDeg(pInCell - float2(0.5f, 0.5f), RotateShapes, cellAspect));
  float s2border   = smoothstep(shapeSize - edgeSmooth, shapeSize + edgeSmooth, s2_raw - borderWidth);
  float s2         = smoothstep(shapeSize - edgeSmooth, shapeSize + edgeSmooth, s2_raw);
  (void)s2;  // HLSL uses s2 only through s2border; s2 itself is unused in final expr

  // [HLSL lines 168-175] Center bar and gap bars.
  float ta = asin(clamp(barWidth * 4.0f + edgeSmooth / 4.0f, -1.0f, 1.0f));
  float2 pcb = fg_rotate(pInCell - float2(0.5f, 0.5f), ta);
  float centerBar = smoothstep(barWidth - edgeSmooth, barWidth + edgeSmooth, abs(pcb.x));
  float gapBarA   = smoothstep(barWidth - edgeSmooth, barWidth + edgeSmooth, abs(pcb.x - barWidth * 2.0f));
  float gapBarB   = smoothstep(barWidth - edgeSmooth, barWidth + edgeSmooth, abs(pcb.x + barWidth * 2.0f));

  // [HLSL lines 178-191] Compose final colour.
  float fillA = 1.0f * mix(1.0f, s1a, gapBarA) * mix(1.0f, s1b, gapBarB) * centerBar;
  float background = 1.0f - s1aBorder * s1bBorder * s2border;

  float4 cBorderOrBackground = mix(Background, FillB_, background);
  float4 cFill = mix(FillA, cBorderOrBackground, fillA);

  return cFill;
}
