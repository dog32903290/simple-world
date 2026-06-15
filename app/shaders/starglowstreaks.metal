// StarGlowStreaks: TiXL-ported directional glow-streak stylize filter, single pass.
// Faithful line-by-line port of external/tixl Operators/Lib/Assets/shaders/img/fx/
// StarGlowStreaks.hlsl (+ its #include "shared/blend-functions.hlsl", inlined below as
// BlendColors so this leaf shares no .metal file).
//
// ============================== LOAD-BEARING PARITY NOTES (named) ==============================
// [fork-range-const]   The .hlsl declares `const float range = 0.3;` and `const float steps = 0.002;`
//   LOCALLY and then the LOOP uses the cbuffer `Range` (NOT the local `range`) as its bound:
//   `for (float i = -Range; i < Range; i += steps)`. The local `range` const is DEAD (shadowed/unused
//   by the active path). We port the LIVE behavior: loop bound = cbuffer Range, step = 0.002 literal.
//   The dead `const range=0.3` is intentionally NOT carried (it changes nothing). steps=0.002 verbatim.
// [fork-glaremodes-branch] The .hlsl GlareModes routing is written with redundant tautological
//   sub-conditions; ported VERBATIM (not simplified) so the active-direction set matches bit-for-bit:
//     branch1 (diagonals): (GlareModes==0 && <3) || (GlareModes==2 && <3)  => modes {0 Diagonals, 2 Star}
//     branch2 outer:       GlareModes==1 || ==2 || >2                       => modes {1,2,3,4}
//       horizontal sub:    GlareModes==3 || ==1 || ==2                      => modes {1 Cross,2 Star,3 Horizontal}
//       vertical sub:      GlareModes==4 || ==1 || ==2                      => modes {1 Cross,2 Star,4 Vertical}
//   Net active-direction sets: 0=Diag1+Diag2; 1=H+V; 2=Diag1+Diag2+H+V; 3=H; 4=V.
// [fork-diag1-scalar-uv] Diagonal-1 samples `uv + i` where i is a SCALAR added to a float2 (HLSL
//   scalar-to-vector promotion) => uv + float2(i,i). Ported as uv + float2(i,i) verbatim.
// [fork-vertical-i2]   Vertical samples `uv + float2(0, i*2)` — the vertical stride is DOUBLE (i*2),
//   not i. Ported verbatim (matters: vertical streaks are 2x longer than horizontal).
// [fork-samplelevel-quality] HLSL Image.SampleLevel(samp, pos, Quality) selects an explicit mip
//   LOD = Quality. Ported as ImageA.sample(samp, pos, level(P.Quality)). The final orgColor uses
//   Image.Sample (mip 0, hardware-chosen) -> ImageA.sample(samp, uv) verbatim.
// [fork-sampler-wrap]  Sampler = MirrorOnce + linear, matching StarGlowStreaks.t3
//   _ImageFxShaderSetupStatic Wrap=MirrorOnce, Filter default MinMagMipLinear. Set in the cook.
// [fork-blend-inline]  BlendColors is inlined from shared/blend-functions.hlsl verbatim (switch on
//   (int)BlendMode). t3 default BlendMode=8 (linearDodge: rgb = tA+tB).
#include <metal_stdlib>
#include "starglowstreaks_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as transformimage_vs / convertcolors_vs.
vertex VSOut starglowstreaks_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Inlined verbatim from external/tixl Operators/Lib/Assets/shaders/shared/blend-functions.hlsl.
static float4 BlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);

  float a = tA.a + tB.a - tA.a * tB.a;

  float normalRatio = saturate(tB.a * 2 - 1);  // declared in .hlsl (unused below); kept for parity

  float3 rgbNormalBlended = (1.0 - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0);

  switch ((int)blendMode) {
    case 0:  // normal
      rgb = rgbNormalBlended;
      break;
    case 1:  // screen
      rgb = 1 - (1 - tA.rgb) * (1 - tB.rgb * tB.a);
      break;
    case 2:  // multiply
      rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a);
      break;
    case 3:  // overlay
      rgb = float3(
          tA.r < 0.5 ? (2.0 * tA.r * tB.r) : (1.0 - 2.0 * (1.0 - tA.r) * (1.0 - tB.r)),
          tA.g < 0.5 ? (2.0 * tA.g * tB.g) : (1.0 - 2.0 * (1.0 - tA.g) * (1.0 - tB.g)),
          tA.b < 0.5 ? (2.0 * tA.b * tB.b) : (1.0 - 2.0 * (1.0 - tA.b) * (1.0 - tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a);
      break;
    case 4:  // difference
      rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0 - tB.a);
      break;
    case 5:  // use a
      rgb = tA.rgb;
      break;
    case 6:  // use b
      rgb = tB.rgb;
      break;
    case 7:  // colorDodge
      rgb = tA.rgb / (1.0001 - saturate(tB.rgb));
      break;
    case 8:  // linearDodge
      rgb = tA.rgb + tB.rgb;
      break;
    case 9:
      a = tA.a * tB.a;
      break;
  }
  return float4(rgb, a);
}

// Mirror of StarGlowStreaks.hlsl psMain (lines 32-101), line for line.
fragment float4 starglowstreaks_fs(VSOut input [[stage_in]],
                                   texture2d<float> ImageA           [[texture(0)]],
                                   sampler texSampler                [[sampler(0)]],
                                   constant StarGlowStreaksParams& P [[buffer(SGS_Params)]]) {
  float2 uv = input.texCoord;

  // float width, height; Image.GetDimensions(width, height); — declared in .hlsl but the dims
  // are unused by the active math (sampling is in normalized UV). Omitted (no behavior change).

  // [fork-range-const] local `const float range = 0.3;` is DEAD in the .hlsl (the loop uses the
  // cbuffer Range). steps is the literal 0.002.
  const float steps = 0.002f;

  float4 streaksColor = float4(0.0f, 0.0f, 0.0f, 1.0f);

  for (float i = -P.Range; i < P.Range; i += steps) {

    float falloff = 1.0f - abs(i / P.Range);

    // [fork-diag1-scalar-uv] HLSL `uv + i` = uv + float2(i,i).
    float4 blur = ImageA.sample(texSampler, uv + float2(i, i), level(P.Quality));

    if ((P.GlareModes == 0 && P.GlareModes < 3) || (P.GlareModes == 2 && P.GlareModes < 3)) {
      // Diagonal 1
      if (blur.r + blur.g + blur.b > P.Threshold * 3.0f) {
        streaksColor += blur * falloff * steps * P.Brightness;
      }
      // Diagonal 2
      blur = ImageA.sample(texSampler, uv + float2(i, -i), level(P.Quality));
      if (blur.r + blur.g + blur.b > P.Threshold * 3.0f) {
        streaksColor += blur * falloff * steps * P.Brightness;
      }
    }
    if (P.GlareModes == 1 || P.GlareModes == 2 || P.GlareModes > 2) {

      if (P.GlareModes == 3 || P.GlareModes == 1 || P.GlareModes == 2) {
        // Horizontal
        blur = ImageA.sample(texSampler, uv + float2(i, 0), level(P.Quality));
        if (blur.r + blur.g + blur.b > P.Threshold * 3.0f) {
          streaksColor += blur * falloff * steps * P.Brightness;
        }
      }

      if (P.GlareModes == 4 || P.GlareModes == 1 || P.GlareModes == 2) {
        // [fork-vertical-i2] Vertical — stride is i*2 (double).
        blur = ImageA.sample(texSampler, uv + float2(0, i * 2), level(P.Quality));
        if (blur.r + blur.g + blur.b > P.Threshold * 3.0f) {
          streaksColor += blur * falloff * steps * P.Brightness;
        }
      }
    }
  }

  float4 Color         = float4(P.Color);
  float4 OriginalColor = float4(P.OriginalColor);

  float4 orgColor = ImageA.sample(texSampler, uv) * OriginalColor;

  return BlendColors(orgColor, streaksColor * Color, (int)P.BlendMode);
}
