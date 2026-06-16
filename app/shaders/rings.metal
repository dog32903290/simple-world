// Rings: TiXL-ported concentric-ring pattern generator, single pass.
// Faithful line-by-line port of external/tixl Operators/Lib/Assets/shaders/img/generate/Rings.hlsl.
// Generates concentric rings with optional per-segment variation (thickness, fill ratio,
// highlight) driven by hash22/hash42 from seed. Optional BlendMode composite with upstream image.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-mod-dual-semantics]  Rings.hlsl DELIBERATELY uses two different modulo semantics:
//   - HLSL line 67: `#define mod(x, y) (x - y * floor(x / y))` — floor-based mod (= GLSL mod).
//     Call sites that use the macro: ringV (L93), ringAngle2 (L106), segmentAngle (L113).
//     → MSL: sw_mod(x, y) = x - y*floor(x/y)   [floor semantics, not fmod]
//   - HLSL `%` operator on float operands = fmod (truncation toward zero, C semantics).
//     Call sites that use `%`: (Seed+0.5)%312.113 (L96), (ringIndex-Offset)%RingCount (L105),
//     segmentV = ringAngle2%1 (L111), ringIndex%12.31 (L115), Seed%712.1 (L115).
//     → MSL: fmod(x, y)   [truncation semantics]
//   The original code's mixture is intentional; we preserve it exactly. Do NOT flatten to
//   sw_mod everywhere — that silently changes the hash/seed/index math.
// [fork-atan2-arg-order]  HLSL atan2(y,x); MSL atan2(y,x) — identical argument order (both
//   atan2(y,x)). No change needed, noted for clarity.
// [fork-blend-inline]  BlendColors inlined verbatim from shared/blend-functions.hlsl (same as
//   starglowstreaks.metal). Default BlendMode=0 (normal blend).
// [fork-sampler-repeat]  Sampler for the optional upstream ImageA is linear+Repeat, matching
//   TiXL _ImageFxShaderSetupStatic.t3 defaults: AddressU/V=Wrap (DX TextureAddressMode.Wrap),
//   Filter=MinMagMipLinear. Rings.hlsl uses `texSampler` only for ImageA.Sample at the very
//   end; interior math is UV-only, but sampler mode must match TiXL for parity when ImageA is
//   wired. (Previous claim that clamp is the default was incorrect.)
// [fork-cbuffer-binding]  HLSL b1 Resolution(TargetWidth/TargetHeight) is framework-injected;
//   bound at Metal fragment cbuffer index 1 (host fills from c.output->width()/height()).
//   Same pattern as VoronoiCells / ChromaticAbberation.
// [fork-hash-shared]  HLSL `#include "shared/hash-functions.hlsl"` provides hash22/hash42/hash11.
//   We include "shared/hash.metal.h" which now contains these same functions (verbatim port).
// [fork-IsTextureValid]  Rings.hlsl uses IsTextureValid to decide whether to composite with the
//   upstream image or return the pattern directly (line 137). We pass this as part of RingsParams
//   (the _ImageFxShaderSetupStatic framework injects it into the cbuffer; we mirror that by
//   having the host set it to 0.0 when no upstream texture is wired, 1.0 when wired).
#include <metal_stdlib>
#include "rings_params.h"
#include "shared/hash.metal.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as voronoicells_vs / tint_vs.
vertex VSOut rings_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // generates (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// sw_mod: floor-based modulo — mirrors HLSL `#define mod(x,y) (x - y*floor(x/y))` (line 67).
// Used only where Rings.hlsl uses the macro (L93, L106, L113). Where Rings.hlsl uses HLSL `%`
// (fmod/truncation semantics: L96, L105, L111, L115), we use MSL fmod() directly.
static inline float sw_mod(float x, float y) { return x - y * floor(x / y); }

// BlendColors — verbatim port of external/tixl Operators/Lib/Assets/shaders/shared/blend-functions.hlsl.
// [fork-blend-inline] Same inline as starglowstreaks.metal / dither.metal.
static inline float4 BlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);
  float a = tA.a + tB.a - tA.a * tB.a;
  float normalRatio = saturate(tB.a * 2.0f - 1.0f);  // declared in .hlsl (unused below); parity
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);
  switch (blendMode) {
    case 0:  rgb = rgbNormalBlended; break;
    case 1:  rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a); break;
    case 2:  rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a); break;
    case 3:
      rgb = float3(
        tA.r < 0.5f ? (2.0f*tA.r*tB.r) : (1.0f - 2.0f*(1.0f-tA.r)*(1.0f-tB.r)),
        tA.g < 0.5f ? (2.0f*tA.g*tB.g) : (1.0f - 2.0f*(1.0f-tA.g)*(1.0f-tB.g)),
        tA.b < 0.5f ? (2.0f*tA.b*tB.b) : (1.0f - 2.0f*(1.0f-tA.b)*(1.0f-tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a); break;
    case 4:  rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a); break;
    case 5:  rgb = tA.rgb; break;
    case 6:  rgb = tB.rgb; break;
    case 7:  rgb = tA.rgb / (1.0001f - saturate(tB.rgb)); break;
    case 8:  rgb = tA.rgb + tB.rgb; break;
    case 9:  a = tA.a * tB.a; break;
  }
  return float4(rgb, a);
}

// Mirror of Rings.hlsl psMain (lines 69-138), line for line.
// [fork-cbuffer-binding] Resolution arrives at buffer(RINGS_Resolution) = index 1.
fragment float4 rings_fs(VSOut input                            [[stage_in]],
                         texture2d<float> ImageA                [[texture(0)]],
                         sampler texSampler                     [[sampler(0)]],
                         constant RingsParams& P                [[buffer(RINGS_Params)]],
                         constant RingsResolution& Res          [[buffer(RINGS_Resolution)]]) {
  // Rings.hlsl lines 71-78
  float aspectRatio = Res.TargetWidth / Res.TargetHeight;
  float ringRadius = (P.RadiusY - P.RadiusX) / P.RingCount;
  float scaledFeather = P.Feather / ringRadius / 10.0f;
  float2 p = input.texCoord;

  p -= 0.5f;
  p.x *= aspectRatio;
  p -= float2(P.PositionX, P.PositionY) * float2(1.0f, -1.0f);

  // Rings.hlsl lines 80-87
  float d2 = length(p);

  float normalizedDistance = (d2 - P.RadiusX) / (P.RadiusY - P.RadiusX);

  float isInsideRadius = normalizedDistance < 0.0f ? 0.0f : 1.0f;

  normalizedDistance = pow(max(normalizedDistance, 0.0f), P.Distort);

  float c = smoothstep(0.0f - 0.01f, 0.0f, normalizedDistance);
  c *= smoothstep(1.0f + 0.01f, 1.0f, normalizedDistance);

  // Rings.hlsl lines 92-96
  float rings = normalizedDistance * P.RingCount + P.Offset;
  float ringV = sw_mod(rings, 1.0f);
  float ringIndex = floor(rings);

  // Rings.hlsl L96: hash22(float2(..., (Seed+0.5) % 312.113)) — HLSL `%` = fmod (truncation).
  float2 ringHash = hash22(float2((ringIndex + 1.0f) * 124.34f + 1.12f,
                                  fmod(P.Seed + 0.5f, 312.113f)));

  // Rings.hlsl lines 98-106
  float segments = P.SegmentsX + (ringHash.x - 0.5f) * P.SegmentsY;
  float ringCenter = abs(ringV - 0.5f);

  // [fork-atan2-arg-order] HLSL atan2(p.x, p.y) — note reversed args vs typical math convention.
  // MSL atan2(y, x) = identical argument order as HLSL atan2(y, x). Here HLSL is atan2(p.x, p.y)
  // so MSL is also atan2(p.x, p.y). Verbatim.
  float angle = (atan2(p.x, p.y) / 2.0f / 3.141578f + 0.5f)
                + P.Rotate / 180.0f / 3.141578f;
  float2 ringRotate = float2(P.TwistX, P.TwistY) / (180.0f * 3.141578f);
  float ringAngle = angle + 0.5f + (ringHash.x - 0.5f) * ringRotate.y;

  // Rings.hlsl lines 105-106
  // L105: (ringIndex - Offset) % RingCount — HLSL `%` = fmod (truncation).
  float ringIndexFromCenter = fmod((ringIndex - P.Offset), P.RingCount);
  float ringAngle2 = sw_mod((ringAngle + ringRotate.x * ringIndexFromCenter / P.RingCount),
                             1.0f) * segments;

  // Rings.hlsl lines 111-113
  // L111: segmentV = ringAngle2 % 1 — HLSL `%` = fmod (truncation).
  float segmentV = fmod(ringAngle2, 1.0f);
  float segmentIndex = floor(ringAngle2 - segmentV + 0.01f);
  // L113: segmentAngle = mod(ringAngle2, 1) — HLSL macro `mod` = floor semantics.
  float segmentAngle = sw_mod(ringAngle2, 1.0f);

  // Rings.hlsl line 115: `ringIndex % 12.31 + Seed % 712.1` — HLSL `%` = fmod (truncation).
  float seed = (segmentIndex * 1.123f + fmod(ringIndex, 12.31f) + fmod(P.Seed, 712.1f));
  float4 segmentHash = hash42(float2(seed * 9234.131f, (P.Seed + 0.5f) * 13.791f));

  // Rings.hlsl line 118
  float segmentThickness = saturate(P.ThicknessX / 2.0f
                                    + (segmentHash.y - 0.5f) * P.ThicknessY);

  // Rings (ring cross-section, lines 120-123)
  c *= smoothstep(segmentThickness + scaledFeather,
                  segmentThickness - scaledFeather, ringCenter);

  float f = scaledFeather / d2 * 0.1f;
  float segmentRatio = (P.RatioX + (segmentHash.x - 0.5f) * P.RatioY) / 2.0f;
  float brightness = mix(segmentHash.w, 1.0f, P.Contrast);

  // Segment (angular arc, lines 128-129)
  c *= smoothstep(segmentRatio + f, segmentRatio - f, abs(segmentAngle - 0.5f));
  c *= (segmentHash.x > P.FillRatio) ? 0.0f : 1.0f;

  // Color composite (lines 131-134)
  float4 fill       = float4(P.FillR, P.FillG, P.FillB, P.FillA);
  float4 background = float4(P.BackgroundR, P.BackgroundG, P.BackgroundB, P.BackgroundA);
  float4 highlight  = float4(P.HighlightR, P.HighlightG, P.HighlightB, P.HighlightA);

  float4 color = mix(background, fill, c * brightness * isInsideRadius);

  float highlightHash = hash11(seed + P.HighlightSeed);
  float4 colorOut = (highlightHash >= P.HighlightRatio)
                    ? color
                    : highlight * c * isInsideRadius;

  // Rings.hlsl line 137: composite with upstream image if wired
  float4 orgColor = ImageA.sample(texSampler, input.texCoord);
  return (P.IsTextureValid < 0.5f)
         ? colorOut
         : BlendColors(orgColor, colorOut, (int)P.BlendMode);
}
