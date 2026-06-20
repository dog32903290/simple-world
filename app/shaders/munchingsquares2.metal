// MunchingSquares2: TiXL-ported XOR/bitwise munching-squares pattern generator.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl.
// Optional Image input for BlendColors composite (IsTextureValid < 0.5 = pure generator).
//
// Three cbuffers:
//   b0 = MunchingSquares2Params  (Black/White/GrayScaleWeights/GainAndBias/Stretch/Offset/Scale/
//                                  IterationFx/IsTextureValid) — 96 bytes
//   b1 = MunchingSquares2Resolution (TargetWidth/TargetHeight) — 16 bytes
//   b2 = MunchingSquares2IntParams  (Method/Iteration/BlendMode) — 16 bytes (ints)
//
// HLSL originals inlined here (no Metal #include for shared/):
//   ApplyGainAndBias — from shared/bias-functions.hlsl (scalar overload)
//   BlendColors      — from shared/blend-functions.hlsl
//
// Forks (named, DX11 → Metal):
//   [fork-mod-macro]       HLSL: `#define mod(x,y) ((x) - (y)*floor((x)/(y)))`.
//                          Metal: same formula inline via sw_mod() — faithful floor semantics.
//   [fork-int-cbuffer]     HLSL cbuffer b2 uses int/int/int. Metal: int32_t struct at buffer(2).
//   [fork-sampler-mirror-clamp]  _ImageFxShaderSetupStatic.t3 Wrap=MirrorOnce (DirectX).
//                          Metal equivalent: MTL::SamplerAddressModeMirrorClampToEdge.
//   [fork-IsTextureValid]  Host injects IsTextureValid=1.0 when Image wired, else 0.0.
//                          Shader guards the BlendColors path on IsTextureValid < 0.5.
#include <metal_stdlib>
#include "munchingsquares2_params.h"
using namespace metal;

// --- Inline ApplyGainAndBias (scalar) from shared/bias-functions.hlsl -----------------------
// TiXL source: Operators/Lib/Assets/shaders/shared/bias-functions.hlsl lines 6-48
static inline float GetBias_ms(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

static inline float GetSchlickBias_ms(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    return 0.5f * GetBias_ms(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    return 0.5f * GetBias_ms(1.0f - g, x) + 0.5f;
  }
}

static inline float ApplyGainAndBias_ms(float value, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = GetBias_ms(b, value);
    value = GetSchlickBias_ms(g, value);
  } else {
    value = GetSchlickBias_ms(g, value);
    value = GetBias_ms(b, value);
  }
  return value;
}

// --- Inline BlendColors from shared/blend-functions.hlsl ------------------------------------
// TiXL source: Operators/Lib/Assets/shaders/shared/blend-functions.hlsl lines 1-72
static inline float4 BlendColors_ms(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);
  float a = tA.a + tB.a - tA.a * tB.a;
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);
  switch (blendMode) {
    case 0: rgb = rgbNormalBlended; break;
    case 1: rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a); break;
    case 2: rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a); break;
    case 3:
      rgb = float3(
        tA.r < 0.5f ? (2.0f * tA.r * tB.r) : (1.0f - 2.0f * (1.0f - tA.r) * (1.0f - tB.r)),
        tA.g < 0.5f ? (2.0f * tA.g * tB.g) : (1.0f - 2.0f * (1.0f - tA.g) * (1.0f - tB.g)),
        tA.b < 0.5f ? (2.0f * tA.b * tB.b) : (1.0f - 2.0f * (1.0f - tA.b) * (1.0f - tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a);
      break;
    case 4: rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a); break;
    case 5: rgb = tA.rgb; break;
    case 6: rgb = tB.rgb; break;
    case 7: rgb = tA.rgb / (1.0001f - saturate(tB.rgb)); break;
    case 8: rgb = tA.rgb + tB.rgb; break;
    case 9: a = tA.a * tB.a; break;
    default: rgb = rgbNormalBlended; break;
  }
  return float4(rgb, a);
}

// --- Vertex / fragment structs --------------------------------------------------------------
struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer).
vertex VSOut munchingsquares2_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);  // flip Y: NDC up vs texture down
  return o;
}

// [fork-mod-macro] faithful floor-based mod matching TiXL `#define mod(x,y) ((x)-(y)*floor((x)/(y)))`
static inline float sw_mod(float x, float y) {
  return x - y * floor(x / y);
}

// Mirror of MunchingSquares.hlsl psMain (verbatim math).
fragment float4 munchingsquares2_fs(
    VSOut in                                           [[stage_in]],
    constant MunchingSquares2Params&     P             [[buffer(MUNCHINGSQUARES2_Params)]],
    constant MunchingSquares2Resolution& R             [[buffer(MUNCHINGSQUARES2_Resolution)]],
    constant MunchingSquares2IntParams&  IP            [[buffer(MUNCHINGSQUARES2_IntParams)]],
    texture2d<float>                     Image         [[texture(0)]],
    sampler                              texSampler    [[sampler(0)]])
{
  // TiXL MunchingSquares.hlsl psMain verbatim (lines 46-107):
  float2 p = in.texCoord;
  p -= 0.5f;

  // int round = 1; (used to scale resolution by rounding step — TiXL verbatim)
  int roundStep = 1;
  int2 res = int2((int)R.TargetWidth / roundStep, (int)R.TargetHeight / roundStep) * roundStep;

  // This will prevent repetitive artifacts in the pattern
  float epsilonScale = P.Scale - 0.0001f;

  float2 divisions = float2(res) / epsilonScale;
  float2 fixOffset = float2(P.OffsetX, P.OffsetY) * float2(-1.0f, 1.0f) / divisions;
  p += fixOffset;

  float2 p1 = p;
  float2 gridSize = float2(1.0f / divisions.x, 1.0f / divisions.y);
  float2 pInCell = float2(sw_mod(p1.x, gridSize.x), sw_mod(p1.y, gridSize.y));
  float2 cellIds = (p1 - pInCell + 0.5f);

  float2 cellTiles = cellIds - fixOffset;
  pInCell *= divisions;

  // Sample upstream image for per-cell grayscale (optional — Image.Sample returns 0 when invalid)
  float4 colorForCell = Image.sample(texSampler, cellTiles) * float4(P.GrayR, P.GrayG, P.GrayB, P.GrayA);

  float grayScale = (colorForCell.r + colorForCell.g + colorForCell.b) / 3.0f;
  float biased = ApplyGainAndBias_ms(grayScale, float2(P.GainAndBiasX, P.GainAndBiasY));

  float fxIterations = (float)IP.Iteration + P.IterationFx * biased;

  int F = (int)(fxIterations + 0.5f);
  int X = (int)(cellIds.x * divisions.x / P.StretchX + 0.5f);
  int Y = (int)(cellIds.y * divisions.y / P.StretchY + 0.5f);

  int method = abs(IP.Method) % 5;

  int blackOrWhite = 0;
  if (method == 0) {
    blackOrWhite = !((X ^ Y) & F);  // classic munching
  } else if (method == 1) {
    blackOrWhite = !(X & F ^ Y & F);
  } else if (method == 2) {
    blackOrWhite = !((X | Y) & F);
  } else if (method == 3) {
    blackOrWhite = !((X * Y) & F);
  } else if (method == 4) {
    blackOrWhite = !(((X ^ (Y << 1)) | (Y ^ (X << 1))) & F);
  }

  float4 Black = float4(P.BlackR, P.BlackG, P.BlackB, P.BlackA);
  float4 White = float4(P.WhiteR, P.WhiteG, P.WhiteB, P.WhiteA);

  float4 c = mix(Black, White, (float)blackOrWhite);

  // [fork-IsTextureValid] blend with upstream image when wired; pure pattern otherwise.
  if (P.IsTextureValid < 0.5f) {
    return c;
  } else {
    return BlendColors_ms(Image.sample(texSampler, in.texCoord), c, IP.BlendMode);
  }
}
