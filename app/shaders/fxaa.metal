// Fxaa: TiXL-ported NVIDIA FXAA 3.11 anti-aliasing filter, single fullscreen pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/use/FXAA.hlsl
// (FxaaPixelShader, "Based on the FXAA 3.11 implementation from Timothy Lottes at NVIDIA").
//
// FORK (named, DX11->Metal): the HLSL bakes the preset (0..5) at compile time via
// `#define FXAA_PRESET`. Here the host passes the chosen preset's constants in FxaaParams (b0)
// and we read them at runtime: edgeThreshold / edgeThresholdMin / searchThreshold / subpixCap /
// subpixTrim as floats, searchSteps / searchAccel / subpixFaster as ints. FXAA_SUBPIX is always 1
// in TiXL (every preset sets FXAA_SUBPIX 1), so we hardcode the SUBPIX==1 blendL path and the
// non-FASTER lowpass box unless subpixFaster==1 (preset 0). All arithmetic is identical to the HLSL.
// Sampler: linear+clamp (HLSL anisotropicSampler; clamp matches edge offsets — fork named, as in
// chromab.metal). SampleGrad acceleration path collapses to SampleLevel(0) here: Metal's sample()
// with explicit clamp + our linear sampler reproduces the box-filter intent; the accel >1 presets
// (0..2) still step by accel pixels per loop iter exactly as the HLSL offNP scaling does.
#include <metal_stdlib>
#include "fxaa_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

vertex VSOut fxaa_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// FxaaLuma: rgb.y * (0.587/0.299) + rgb.x  (HLSL FxaaLuma, verbatim).
static inline float fxaaLuma(float3 rgb) {
  return rgb.y * (0.587f / 0.299f) + rgb.x;
}

// FxaaLerp3 (HLSL): (-amountOfA * b) + (a*amountOfA + b)  == lerp(b, a, amountOfA).
static inline float3 fxaaLerp3(float3 a, float3 b, float amountOfA) {
  return (-amountOfA * b) + (a * amountOfA + b);
}

// Sampled fetch helpers. HLSL uses SampleLevel(0) / SampleLevel(0, off) / SampleGrad.
static inline float4 texLod0(texture2d<float> img, sampler s, float2 pos) {
  return img.sample(s, pos, level(0.0f));
}
static inline float4 texOff(texture2d<float> img, sampler s, float2 pos, int2 off) {
  return img.sample(s, pos, level(0.0f), off);
}

// Mirror of HLSL FxaaPixelShader. Returns the filtered rgb. Preset constants come from P.
static float3 fxaaPixelShader(float2 pos,
                              texture2d<float> img, sampler s,
                              float2 rcpFrame,
                              constant FxaaParams& P) {
  float subpixTrimScale = 1.0f / (1.0f - P.subpixTrim);  // FXAA_SUBPIX_TRIM_SCALE

  // --- EARLY EXIT IF LOCAL CONTRAST BELOW EDGE DETECT LIMIT ---
  float3 rgbN = texOff(img, s, pos, int2( 0,-1)).xyz;
  float3 rgbW = texOff(img, s, pos, int2(-1, 0)).xyz;
  float3 rgbM = texOff(img, s, pos, int2( 0, 0)).xyz;
  float3 rgbE = texOff(img, s, pos, int2( 1, 0)).xyz;
  float3 rgbS = texOff(img, s, pos, int2( 0, 1)).xyz;
  float lumaN = fxaaLuma(rgbN);
  float lumaW = fxaaLuma(rgbW);
  float lumaM = fxaaLuma(rgbM);
  float lumaE = fxaaLuma(rgbE);
  float lumaS = fxaaLuma(rgbS);
  float rangeMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
  float rangeMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
  float range = rangeMax - rangeMin;

  if (range < max(P.edgeThresholdMin, rangeMax * P.edgeThreshold)) {
    return rgbM;  // FxaaFilterReturn (FXAA_SRGB_ROP 0 -> identity)
  }

  // FXAA_SUBPIX > 0 (always true for TiXL presets).
  float3 rgbL;
  if (P.subpixFaster != 0) {
    rgbL = (rgbN + rgbW + rgbE + rgbS + rgbM) * (1.0f / 5.0f);
  } else {
    rgbL = rgbN + rgbW + rgbM + rgbE + rgbS;
  }

  // --- COMPUTE LOWPASS (FXAA_SUBPIX != 0) ---
  float lumaL = (lumaN + lumaW + lumaE + lumaS) * 0.25f;
  float rangeL = abs(lumaL - lumaM);
  // FXAA_SUBPIX == 1 path.
  float blendL = max(0.0f, (rangeL / range) - P.subpixTrim) * subpixTrimScale;
  blendL = min(P.subpixCap, blendL);

  // --- CHOOSE VERTICAL OR HORIZONTAL SEARCH ---
  float3 rgbNW = texOff(img, s, pos, int2(-1,-1)).xyz;
  float3 rgbNE = texOff(img, s, pos, int2( 1,-1)).xyz;
  float3 rgbSW = texOff(img, s, pos, int2(-1, 1)).xyz;
  float3 rgbSE = texOff(img, s, pos, int2( 1, 1)).xyz;
  if (P.subpixFaster == 0) {  // (FXAA_SUBPIX_FASTER==0) && (FXAA_SUBPIX>0)
    rgbL += (rgbNW + rgbNE + rgbSW + rgbSE);
    rgbL *= (1.0f / 9.0f);
  }
  float lumaNW = fxaaLuma(rgbNW);
  float lumaNE = fxaaLuma(rgbNE);
  float lumaSW = fxaaLuma(rgbSW);
  float lumaSE = fxaaLuma(rgbSE);
  float edgeVert =
      abs((0.25f * lumaNW) + (-0.5f * lumaN) + (0.25f * lumaNE)) +
      abs((0.50f * lumaW ) + (-1.0f * lumaM) + (0.50f * lumaE )) +
      abs((0.25f * lumaSW) + (-0.5f * lumaS) + (0.25f * lumaSE));
  float edgeHorz =
      abs((0.25f * lumaNW) + (-0.5f * lumaW) + (0.25f * lumaSW)) +
      abs((0.50f * lumaN ) + (-1.0f * lumaM) + (0.50f * lumaS )) +
      abs((0.25f * lumaNE) + (-0.5f * lumaE) + (0.25f * lumaSE));
  bool horzSpan = edgeHorz >= edgeVert;
  float lengthSign = horzSpan ? -rcpFrame.y : -rcpFrame.x;
  if (!horzSpan) lumaN = lumaW;
  if (!horzSpan) lumaS = lumaE;
  float gradientN = abs(lumaN - lumaM);
  float gradientS = abs(lumaS - lumaM);
  lumaN = (lumaN + lumaM) * 0.5f;
  lumaS = (lumaS + lumaM) * 0.5f;

  // --- CHOOSE SIDE OF PIXEL WHERE GRADIENT IS HIGHEST ---
  bool pairN = gradientN >= gradientS;
  if (!pairN) lumaN = lumaS;
  if (!pairN) gradientN = gradientS;
  if (!pairN) lengthSign *= -1.0f;
  float2 posN;
  posN.x = pos.x + (horzSpan ? 0.0f : lengthSign * 0.5f);
  posN.y = pos.y + (horzSpan ? lengthSign * 0.5f : 0.0f);

  gradientN *= P.searchThreshold;

  // --- SEARCH IN BOTH DIRECTIONS ---
  float2 posP = posN;
  float2 offNP = horzSpan ? float2(rcpFrame.x, 0.0f) : float2(0.0f, rcpFrame.y);
  float lumaEndN = lumaN;
  float lumaEndP = lumaN;
  bool doneN = false;
  bool doneP = false;
  int accel = P.searchAccel;
  if (accel == 1) {
    posN += offNP * float2(-1.0f, -1.0f);
    posP += offNP * float2( 1.0f,  1.0f);
  } else if (accel == 2) {
    posN += offNP * float2(-1.5f, -1.5f);
    posP += offNP * float2( 1.5f,  1.5f);
    offNP *= float2(2.0f, 2.0f);
  } else if (accel == 3) {
    posN += offNP * float2(-2.0f, -2.0f);
    posP += offNP * float2( 2.0f,  2.0f);
    offNP *= float2(3.0f, 3.0f);
  } else if (accel == 4) {
    posN += offNP * float2(-2.5f, -2.5f);
    posP += offNP * float2( 2.5f,  2.5f);
    offNP *= float2(4.0f, 4.0f);
  }
  for (int i = 0; i < P.searchSteps; i++) {
    // accel==1 uses SampleLevel(0); accel>1 uses SampleGrad — both resolve to a linear fetch.
    if (!doneN) lumaEndN = fxaaLuma(texLod0(img, s, posN).xyz);
    if (!doneP) lumaEndP = fxaaLuma(texLod0(img, s, posP).xyz);
    doneN = doneN || (abs(lumaEndN - lumaN) >= gradientN);
    doneP = doneP || (abs(lumaEndP - lumaN) >= gradientN);
    if (doneN && doneP) break;
    if (!doneN) posN -= offNP;
    if (!doneP) posP += offNP;
  }

  // --- HANDLE IF CENTER IS ON POSITIVE OR NEGATIVE SIDE ---
  float dstN = horzSpan ? pos.x - posN.x : pos.y - posN.y;
  float dstP = horzSpan ? posP.x - pos.x : posP.y - pos.y;
  bool directionN = dstN < dstP;
  lumaEndN = directionN ? lumaEndN : lumaEndP;

  if (((lumaM - lumaN) < 0.0f) == ((lumaEndN - lumaN) < 0.0f))
    lengthSign = 0.0f;

  // --- COMPUTE SUB-PIXEL OFFSET AND FILTER SPAN ---
  float spanLength = (dstP + dstN);
  dstN = directionN ? dstN : dstP;
  float subPixelOffset = (0.5f + (dstN * (-1.0f / spanLength))) * lengthSign;
  float3 rgbF = texLod0(img, s, float2(
      pos.x + (horzSpan ? 0.0f : subPixelOffset),
      pos.y + (horzSpan ? subPixelOffset : 0.0f))).xyz;
  // FXAA_SUBPIX != 0 -> blend lowpass.
  return fxaaLerp3(rgbL, rgbF, blendL);
}

// Mirror of HLSL psMain.
fragment float4 fxaa_fs(VSOut in [[stage_in]],
                        texture2d<float> Image      [[texture(0)]],
                        sampler texSampler          [[sampler(0)]],
                        constant FxaaParams& P      [[buffer(FXAA_Params)]],
                        constant FxaaResolution& R  [[buffer(FXAA_Resolution)]]) {
  float2 uv = in.texCoord;
  float2 rcpFrame = float2(P.rcpFrameX, P.rcpFrameY);
  float alpha = 1.0f;
  if (P.KeepAlpha != 0.0f)
    alpha = Image.sample(texSampler, uv).a;
  return float4(fxaaPixelShader(uv, Image, texSampler, rcpFrame, P), alpha);
}
