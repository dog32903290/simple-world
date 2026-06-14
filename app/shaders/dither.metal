// Dither: TiXL-ported Bayer/hash ordered-dither quantizer, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Dither.hlsl psMain
// (+ its shared helpers Bayer*/hash11u/ApplyGainAndBias/BlendColors).
//
// Kernel (verbatim, Dither.hlsl:50-87):
//   aspectRatio = TargetWidth/TargetHeight; p = texCoord - 0.5;
//   round=1; res = int2(TargetWidth, TargetHeight); epsilonScale = Scale - 0.0001;
//   divisions = res / epsilonScale; fixOffset = Offset*(-1,1)/divisions; p += fixOffset;
//   gridSize = 1/divisions; pInCell = mod(p, gridSize); cellIds = p - pInCell + 0.5;
//   cellTiles = cellIds - fixOffset; pInCell *= divisions;
//   color = Image.Sample(cellTiles);
//   grayScale = ApplyGainAndBias(saturate(color), GainAndBias);   // float4->float = .r truncation
//   fragCoord = cellIds * res;
//   n = Method<0.5 ? Bayer64(fragCoord/epsilonScale)
//                  : hash11u((int)fragCoord.x*21 + (int)fragCoord.y*12112);
//   dithering = (n*2-1)*0.5; blackOrWhite = (dithering+grayScale < 0.5) ? 0 : 1;
//   c = lerp(Black, White, blackOrWhite);
//   return IsTextureValid<0.5 ? c : BlendColors(color, c, (int)BlendMode);
//
// Forks (named, DX11->Metal):
//   - DX11 PS -> Metal fullscreen-triangle VS+FS (same fork class as DetectEdges/Tint).
//   - HLSL framework Resolution(b1) TargetWidth/Height -> passed in via DitherResolution (host
//     fills from the SOURCE image dims, same pattern as Pixelate).
//   - GrayScaleWeights (Dither.hlsl cbuffer line 9) is declared but NEVER read by psMain — kept as
//     a port for .cs fidelity (Dither.cs InputSlot) but unused here, exactly as TiXL.
//   - grayScale is a `float` = ApplyGainAndBias(saturate(float4),...) (Dither.hlsl:75); HLSL
//     truncates float4->float to .x — we port ApplyGainAndBias on the .r channel (scalar form).
//   - IsTextureValid is the host "texture wired" flag (not a .cs input); set host-side.
//   - HLSL #define mod(x,y) ((x)-(y)*floor((x)/(y))) ported inline. Fixed linear+clamp sampler.
#include <metal_stdlib>
#include "dither_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

vertex VSOut dither_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// --- shared helpers, ported verbatim ----------------------------------------------------------
// hash-functions.hlsl hash11u (lines 115-123). _PRIME0 = 13331u (hash-functions.hlsl:4).
// [batch24 refuter fix] 原港的 1597334677u 是憑空常數,非 TiXL 值——只影響 Method>=0.5 hash 分支
// (Bayer 預設路徑不走 hash,故 golden 沒抓到)。權威 external/tixl .../hash-functions.hlsl:4。
static inline float hash11u(uint x) {
  const uint k = 1103515245u;  // GLIB C
  const uint _PRIME0 = 13331u;
  x *= _PRIME0;
  x = ((x >> 8u) ^ x) * k;
  x = ((x >> 8u) ^ x) * k;
  return float(x) * (1.0f / float(0xffffffffu));
}

// Dither.hlsl Bayer2 (lines 42-46) + the Bayer4/8/16/32/64 macro chain (lines 36-40).
static inline float Bayer2(float2 a) {
  a = floor(a);
  return fract(a.x / 2.0f + a.y * a.y * 0.75f);
}
static inline float Bayer4(float2 a)  { return Bayer2(0.5f * a) * 0.25f + Bayer2(a); }
static inline float Bayer8(float2 a)  { return Bayer4(0.5f * a) * 0.25f + Bayer2(a); }
static inline float Bayer16(float2 a) { return Bayer8(0.5f * a) * 0.25f + Bayer2(a); }
static inline float Bayer32(float2 a) { return Bayer16(0.5f * a) * 0.25f + Bayer2(a); }
static inline float Bayer64(float2 a) { return Bayer32(0.5f * a) * 0.25f + Bayer2(a); }

// bias-functions.hlsl GetBias / GetSchlickBias / ApplyGainAndBias (scalar form, lines 6-49).
static inline float GetBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static inline float GetSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * GetBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * GetBias(1.0f - g, x) + 0.5f;
  }
  return x;
}
static inline float ApplyGainAndBias(float value, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = GetBias(b, value);
    value = GetSchlickBias(g, value);
  } else {
    value = GetSchlickBias(g, value);
    value = GetBias(b, value);
  }
  return value;
}

// blend-functions.hlsl BlendColors (lines 1-72), full switch ported verbatim.
static inline float4 BlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);
  float a = tA.a + tB.a - tA.a * tB.a;
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);
  switch (blendMode) {
    case 0: rgb = rgbNormalBlended; break;                                   // normal
    case 1: rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a); break;    // screen
    case 2: rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a); break;                 // multiply
    case 3: {                                                                 // overlay
      rgb = float3(
          tA.r < 0.5f ? (2.0f * tA.r * tB.r) : (1.0f - 2.0f * (1.0f - tA.r) * (1.0f - tB.r)),
          tA.g < 0.5f ? (2.0f * tA.g * tB.g) : (1.0f - 2.0f * (1.0f - tA.g) * (1.0f - tB.g)),
          tA.b < 0.5f ? (2.0f * tA.b * tB.b) : (1.0f - 2.0f * (1.0f - tA.b) * (1.0f - tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a);
      break;
    }
    case 4: rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a); break; // difference
    case 5: rgb = tA.rgb; break;                                              // use a
    case 6: rgb = tB.rgb; break;                                              // use b
    case 7: rgb = tA.rgb / (1.0001f - saturate(tB.rgb)); break;               // colorDodge
    case 8: rgb = tA.rgb + tB.rgb; break;                                     // linearDodge
    case 9: a = tA.a * tB.a; break;
  }
  return float4(rgb, a);
}

// Mirror of Dither.hlsl psMain.
fragment float4 dither_fs(VSOut in [[stage_in]],
                          texture2d<float> Image      [[texture(0)]],
                          sampler texSampler          [[sampler(0)]],
                          constant DitherParams& P     [[buffer(DITHER_Params)]],
                          constant DitherResolution& R [[buffer(DITHER_Resolution)]]) {
  // float aspectRatio = TargetWidth / TargetHeight;  (declared, unused downstream in TiXL)
  float2 p = in.texCoord;
  p -= 0.5f;

  int roundv = 1;
  int2 res = int2((int)R.TargetWidth / roundv, (int)R.TargetHeight / roundv) * roundv;

  float epsilonScale = P.Scale - 0.0001f;  // prevents repetitive pattern artifacts

  float2 fres = float2((float)res.x, (float)res.y);
  float2 divisions = fres / epsilonScale;
  float2 fixOffset = float2(P.OffsetX, P.OffsetY) * float2(-1.0f, 1.0f) / divisions;
  p += fixOffset;

  float2 p1 = p;
  float2 gridSize = float2(1.0f / divisions.x, 1.0f / divisions.y);
  // HLSL #define mod(x,y) ((x)-(y)*floor((x)/(y)))
  float2 pInCell = p1 - gridSize * floor(p1 / gridSize);
  float2 cellIds = (p1 - pInCell + 0.5f);
  float2 cellTiles = cellIds - fixOffset;

  pInCell *= divisions;  // (matches HLSL; pInCell not used after this in TiXL)

  float4 color = Image.sample(texSampler, cellTiles);
  // grayScale (float) = ApplyGainAndBias(saturate(color), GainAndBias): HLSL float4->float = .r
  float grayScale = ApplyGainAndBias(saturate(color.r), float2(P.GainAndBiasX, P.GainAndBiasY));
  float2 fragCoord = cellIds * fres;

  float n = P.Method < 0.5f
                ? Bayer64(fragCoord / epsilonScale)
                : hash11u((uint)((int)(fragCoord.x) * 21 + (int)(fragCoord.y) * 12112));

  float dithering = (n * 2.0f - 1.0f) * 0.5f;
  float blackOrWhite = (dithering + grayScale < 0.5f) ? 0.0f : 1.0f;

  float4 Black = float4(P.BlackR, P.BlackG, P.BlackB, P.BlackA);
  float4 White = float4(P.WhiteR, P.WhiteG, P.WhiteB, P.WhiteA);
  float4 c = mix(Black, White, blackOrWhite);
  return (P.IsTextureValid < 0.5f) ? c : BlendColors(color, c, (int)P.BlendMode);
}
