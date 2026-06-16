// SinForm: TiXL-ported sinusoidal wave pattern generator.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/SinForm.hlsl psMain.
//
// Generates `Copies` stacked sinusoidal curves (with optional per-copy UV offset via OffsetCopies),
// smoothstep-feathered to LineWidth, composited Fill colour over Background colour, then alpha-
// composited over the optional input texture (orgColor). The UV space is aspect-corrected and
// rotated by Rotate degrees before the sin evaluation.
//
// Kernel verbatim translation (SinForm.hlsl lines 47-103):
//   uv = texCoord
//   orgColor = inputTexture.SampleLevel(s, uv, 0)
//   aspectRatio = TargetWidth/TargetHeight
//   p = uv - 0.5
//   imageRotationRad = (-Rotate - 90) / 180 * PI
//   sina = sin(-imageRotationRad - PI/2)
//   cosa = cos(-imageRotationRad - PI/2)
//   p.x *= aspectRatio
//   p = (cosa*p.x - sina*p.y, cosa*p.y + sina*p.x)    // rotation matrix
//   p.x /= aspectRatio
//   cc = 0
//   copiesCount = clamp((int)Copies, 1, 20)  // floor: HLSL (int)Copies+0.5 = floor, not round
//   feather = LineWidth * Fade / 2
//   for i in [0, copiesCount):
//     pp.y = p.y + sin(pp.x/Size.x*PI + Offset.x*PI + OffsetCopies.x*PI*2*i) * Size.y/2
//                + Offset.y + OffsetCopies.y*i
//     c = abs(pp.y)
//     c = smoothstep(LineWidth/2 + feather, LineWidth/2 - feather, c)
//     c = smoothstep(0, 1, c)
//     cc = max(cc, c)
//   col = lerp(Background, Fill, cc)
//   a = clamp(orgColor.a + col.a - orgColor.a*col.a, 0, 1)
//   rgb = (1 - col.a)*orgColor.rgb + col.a*col.rgb
//   return float4(rgb, a)
//
// Forks (named, DX11->Metal):
//   - DX11 PS (VS+PS) -> Metal fullscreen-triangle VS+FS (same fork class as ChromaticAbberation).
//   - HLSL #define mod(x,y) inlined (not needed by this shader, but kept note for parity).
//   - HLSL b1 Resolution cbuffer (TargetWidth/TargetHeight) bound at Metal fragment index 1
//     (host-filled from the output size — same pattern as VoronoiCells/ChromaticAbberation).
//   - SinForm has optional Image input (default null in TiXL t3). When no upstream texture is
//     wired, the host binds a 1x1 transparent-black dummy so the shader always has a valid
//     texture2d handle. orgColor then evaluates to (0,0,0,0) → the wave is drawn over black
//     (TiXL behaviour with no input: output = alpha-composite of Fill wave on black).
//   - Fixed linear+clamp sampler (TiXL .t3 Wrap=Clamp verbatim).
//   - HLSL `static float PI2 = 2*3.141578` (note: 3.141578, not exact pi) preserved verbatim.
//     The inner sin uses PI2/2 = 3.141578 = ~PI, matching HLSL lines 83-85 exactly.
//   - `aspectRation` (sic, HLSL line 54) is defined but overwritten by `aspectRatio` (line 60);
//     the rotation uses `aspectRation` (lines 65/73) which equals `aspectRatio` since both are
//     TargetWidth/TargetHeight — same value, named fork: use a single variable `ar`.
//   - [fork-copiesCount-floor]  HLSL L77: `int copiesCount = clamp((int)Copies+0.5, 1, 20)`.
//     In HLSL/C++, `(int)Copies` truncates to int FIRST, then `+0.5` promotes to float, then
//     the outer `int` assignment truncates again → net effect = floor(Copies), clamp [1,20].
//     A naive reading as `(int)(Copies+0.5)` would be round-half-up, which diverges for any
//     Copies with fractional part ≥ 0.5. Correct MSL: `(int)P.Copies` (floor truncation).
#include <metal_stdlib>
#include "sinform_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut sinform_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of SinForm.hlsl psMain.
fragment float4 sinform_fs(VSOut in [[stage_in]],
                           texture2d<float> inputTexture [[texture(0)]],
                           sampler texSampler            [[sampler(0)]],
                           constant SinFormParams&     P [[buffer(SINFORM_Params)]],
                           constant SinFormResolution& R [[buffer(SINFORM_Resolution)]]) {
  // HLSL lines 49-52: uv, orgColor
  float2 uv = in.texCoord;
  float4 orgColor = inputTexture.sample(texSampler, uv, level(0.0f));

  // HLSL line 54: aspectRation (sic) = TargetWidth/TargetHeight
  // HLSL line 60: aspectRatio = TargetWidth/TargetHeight  (same value; fork: merged to `ar`)
  float ar = R.TargetWidth / R.TargetHeight;

  // HLSL lines 55-56: p = uv - 0.5
  float2 p = uv - 0.5f;

  // HLSL lines 59-63: rotation angles
  // static float PI2 = 2*3.141578 (HLSL, preserved verbatim: 3.141578 not exact pi)
  const float PI2 = 2.0f * 3.141578f;
  float imageRotationRad = (-P.Rotate - 90.0f) / 180.0f * 3.141578f;
  float sina = sin(-imageRotationRad - 3.141578f / 2.0f);
  float cosa = cos(-imageRotationRad - 3.141578f / 2.0f);

  // HLSL line 65: p.x *= aspectRation
  p.x *= ar;

  // HLSL lines 67-70: rotation matrix (HLSL: cosa*p.x - sina*p.y, cosa*p.y + sina*p.x)
  p = float2(cosa * p.x - sina * p.y,
             cosa * p.y + sina * p.x);

  // HLSL line 73: p.x /= aspectRation
  p.x /= ar;

  // HLSL lines 76-92: copies loop
  // HLSL L77: `clamp((int)Copies+0.5, 1, 20)`.
  // Operator precedence: (int)Copies truncates FIRST (floor), then +0.5 is in float domain,
  // then assignment to int truncates again → net = floor(Copies), NOT round-half-up.
  // [fork-copiesCount-floor] We use (int)P.Copies to match this floor-truncation exactly.
  float cc = 0.0f;
  int copiesCount = clamp((int)P.Copies, 1, 20);
  float2 pp = p;
  float feather = P.LineWidth * P.Fade / 2.0f;

  for (int i = 0; i < copiesCount; i++) {
    // HLSL lines 82-86:
    //   pp.y = p.y + sin(pp.x / Size.x * PI2/2
    //                    + Offset.x/2 * PI2
    //                    + OffsetCopies.x * PI2 * i) * Size.y/2
    //                + Offset.y + OffsetCopies.y * i
    pp.y = p.y
         + sin(pp.x / P.SizeX * PI2 / 2.0f
               + P.OffsetX / 2.0f * PI2
               + P.OffCopX * PI2 * (float)i)
           * P.SizeY / 2.0f
         + P.OffsetY
         + P.OffCopY * (float)i;

    // HLSL lines 88-91:
    float c = abs(pp.y);
    c = smoothstep(P.LineWidth / 2.0f + feather, P.LineWidth / 2.0f - feather, c);
    c = smoothstep(0.0f, 1.0f, c);
    cc = max(cc, c);
  }

  // HLSL line 95: col = lerp(Background, Fill, cc)
  float4 Fill = float4(P.FillR, P.FillG, P.FillB, P.FillA);
  float4 Bg   = float4(P.BgR,   P.BgG,   P.BgB,   P.BgA);
  float4 col  = mix(Bg, Fill, cc);

  // HLSL lines 99-101: alpha composite over orgColor
  float a = clamp(orgColor.a + col.a - orgColor.a * col.a, 0.0f, 1.0f);
  float3 rgb = (1.0f - col.a) * orgColor.rgb + col.a * col.rgb;

  return float4(rgb, a);
}
