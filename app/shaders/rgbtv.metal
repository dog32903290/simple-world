// RgbTV: TiXL-ported CRT / RGB-stripe glitch image filter (compute port of a pixel shader).
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl (psMain), with the
// two hash helpers from Operators/Lib/Assets/shaders/shared/hash-functions.hlsl (hash12, hash42)
// inlined verbatim (RgbTV.hlsl #includes that file).
//
// PS->compute port (the load-bearing transform, same idiom as fastblur.metal):
//   - HLSL psMain runs per fragment with psInput.texCoord = uv in [0,1] across the output. In compute
//     there is no interpolator: recover uv = (gid + 0.5) / outSize. Identical math from there.
//   - inputTexture.SampleLevel(clampingSampler, uv2, i)  -> Input.sample(clampS, uv2, level(i)).
//     The input is bound MIPPED (point_ops_rgbtv.cpp blits level0 then generateMipmaps), so LOD 0..7
//     are real mip levels — this op is the first consumer of the Cut-53 mip seam.
//   - noiseTexture.SampleLevel(wrappingSampler, noiseUv, 0) -> Noise.sample(wrapS, noiseUv, level(0)).
//   - HLSL t0/t1 (SRV) + u0 (UAV) are separate register namespaces; MSL [[texture(n)]] is ONE
//     namespace -> Input @0, Noise @1, Result @2 (rgbtv_params.h RGBTV_*). Likewise s0/s1 -> two MSL
//     samplers (clamp @0, wrap @1).
//   - inputTexture.GetDimensions(0,w,h,mipLevelCount) then mipLevelCount=7: the .hlsl OVERWRITES the
//     queried count with the literal 7, so the loop is `for i in 0..7` (8 samples). Ported verbatim:
//     we drop the GetDimensions call (its w/h/mipLevelCount outputs are all unused after the literal).
//   - return float4(r) (PS) -> Result.write(r, gid) (RWTexture2D).
//   - mod(x,y) macro -> a helper modHlsl(x,y) = x - y*floor(x/y) (HLSL fmod differs; the .hlsl defines
//     its own mod, ported exactly). `noise % 1` and `t % 1` are float modulo -> fmod with 1.
#include <metal_stdlib>
#include "rgbtv_params.h"
using namespace metal;

// --- hash-functions.hlsl (verbatim) -----------------------------------------------------------
static inline float hash12(float2 p) {
  float3 p3 = fract(float3(p.x, p.y, p.x) * 0.1031f);
  p3 += dot(p3, float3(p3.y, p3.z, p3.x) + 33.33f);
  return fract((p3.x + p3.y) * p3.z);
}
static inline float4 hash42(float2 p) {
  float4 p4 = fract(float4(p.x, p.y, p.x, p.y) * float4(0.1031f, 0.1030f, 0.0973f, 0.1099f));
  p4 += dot(p4, float4(p4.w, p4.z, p4.x, p4.y) + 33.33f);
  return fract((float4(p4.x, p4.x, p4.y, p4.z) + float4(p4.y, p4.z, p4.z, p4.w)) *
               float4(p4.z, p4.y, p4.w, p4.x));
}

// RgbTV.hlsl: #define mod(x, y) ((x) - ((y)*floor((x) / (y))))  -- ported as a function.
static inline float modHlsl(float x, float y) { return x - y * floor(x / y); }
static inline float2 modHlsl(float2 x, float2 y) { return x - y * floor(x / y); }

constant float3 RgbColors[3] = {float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1)};

// RgbTV.hlsl GetColor (verbatim). Reads ImageBrightness/Contrast/BlackLevel/PatternBlurX/Y from P.
static inline float GetColor(constant RgbTvParams& P, int rgbIndex, float x, float py,
                             float sourceImageChannel) {
  float offset = (rgbIndex - 1) / 3.0f;
  x += offset;
  x = modHlsl(x, 1.03f);
  float center = (0.5f - abs(x - 0.5f)) * 2.0f;
  float xx = center + pow(sourceImageChannel * P.ImageBrightness, P.Contrast) + P.BlackLevel;
  float s = smoothstep(1.0f - P.PatternBlurX, 1.0f + P.PatternBlurY, xx) * P.ImageBrightness;
  return s;
}

// RgbTV.hlsl GetNoiseFromRandom (verbatim). Uses GlitchTime/NoiseSpeed/NoiseColorize/NoiseExponent.
static inline float4 GetNoiseFromRandom(constant RgbTvParams& P, float2 uv) {
  float pxHash = hash12(uv * 431.0f + 111.0f);
  float t = P.GlitchTime * P.NoiseSpeed + pxHash;

  float4 hash1 = hash42((uv * 431.0f + (float)(int)t));
  float4 hash2 = hash42((uv * 431.0f + (float)(int)t + 1.0f));
  float4 hash = mix(hash1, hash2, fmod(t, 1.0f));

  float4 grayScale = float4((hash.r + hash.g + hash.b) / 3.0f);
  float4 noise = (mix(grayScale, hash, P.NoiseColorize) - 0.5f) * 2.0f;

  noise = select(pow(noise, float4(P.NoiseExponent)),
                 -pow(-noise, float4(P.NoiseExponent)),
                 noise < 0.0f);
  return noise;
}

// RgbTV.hlsl psMain, ported 1:1.
kernel void rgbtv_cs(texture2d<float, access::sample> Input  [[texture(RGBTV_Input)]],
                     texture2d<float, access::sample> Noise  [[texture(RGBTV_Noise)]],
                     texture2d<float, access::write>  Result [[texture(RGBTV_Result)]],
                     constant RgbTvParams&            P      [[buffer(RGBTV_Params)]],
                     constant RgbTvResolution&        R      [[buffer(RGBTV_Res)]],
                     sampler                          clampS [[sampler(RGBTV_ClampSampler)]],
                     sampler                          wrapS  [[sampler(RGBTV_WrapSampler)]],
                     uint2                            gid    [[thread_position_in_grid]]) {
  if (gid.x >= Result.get_width() || gid.y >= Result.get_height()) return;

  float2 outSize = float2((float)Result.get_width(), (float)Result.get_height());
  float2 uv = (float2(gid) + 0.5f) / outSize;  // psInput.texCoord
  float aspectRatio = R.TargetWidth / R.TargetHeight;

  float2 p = uv;
  p -= 0.5f;

  // Bulge distort
  p -= p * P.Buldge * P.Visibility * (0.5f - dot(p, p));

  float2 uv2 = p + 0.5f;

  // Distortion (noise asset @ t1)
  float2 noiseOffset = float2(0.1f, 0.1f) * P.GlitchTime;
  float2 noiseUv = p * float2(0.001f, 1.0f) + noiseOffset;
  float4 noiseColor =
      abs(Noise.sample(wrapS, noiseUv, level(0)) - 0.5f) * P.GlitchAmount * P.Visibility;

  // Amplify noise on upper edge
  noiseColor *= (0.4f + pow(1.0f - uv2.y, 6.0f) * 3.0f);
  noiseColor += 0.03f;

  float2 glichOffset = pow(noiseColor.r, 4.0f) * P.GlitchDistort * float2(1.0f, 0.1f);
  uv2 -= glichOffset * P.Visibility;

  // GetDimensions(...) then mipLevelCount = 7 (literal overwrite) — see header note.
  int mipLevelCount = RGBTV_MIP_LEVELS;  // 7

  float4 blurredCol = float4(0.0f);
  float blurredSum = 0.0f;

  float4 glowCol = float4(0.0f);
  float glowSum = 0.0f;
  for (int i = 0; i < mipLevelCount + 1; i++) {
    float4 mipColor = Input.sample(clampS, uv2, level((float)i));

    float f = i / ((float)mipLevelCount);
    float lvl = saturate((pow(f + 1.0f - saturate(P.BlurImage), 20.0f) + 0.001f));
    blurredSum += lvl;
    blurredCol += mipColor * lvl;

    lvl = saturate((pow(f + 1.0f - saturate(P.GlowBlur), 20.0f) + 0.001f));
    glowSum += lvl;
    glowCol += mipColor * lvl;
  }
  blurredCol /= blurredSum;
  float4 imgCol1 = Input.sample(clampS, uv2, level(0.0f));
  blurredCol = mix(imgCol1, blurredCol, saturate(P.BlurImage + 1.0f) * P.Visibility);

  glowCol /= glowSum;
  float4 imgCol = blurredCol + clamp((glowCol) * P.GlowIntensity, 0.0f, 10.0f) * P.Visibility;

  imgCol.rgb *= clamp(1.0f - pow(noiseColor.r, 1.4f) * P.ShadeDistortion * P.GlitchDistort *
                                 P.GlitchAmount * P.Visibility,
                      0.0f, 10.0f);

  imgCol.a = saturate(imgCol.a);

  float2 divisions = float2(aspectRatio, 1.0f) * 4.0f / P.PatternSize;
  float2 pCentered = p;
  float2 pScaled = pCentered * divisions;

  float pInCellX = modHlsl(pScaled.x, 1.0f);
  int cellIdX = (int)(pScaled.x - pInCellX);
  float pInCellY = modHlsl(pScaled.y + cellIdX * P.ShiftColums, 1.0f);
  int cellIdY = (int)(pScaled.y - pInCellY);

  float2 pInCell = float2(pInCellX, pInCellY);

  float4 noise = GetNoiseFromRandom(P, float2((float)cellIdX, (float)cellIdY));
  float4 noiseDelta = abs(noise) *
                      ((pow(noiseColor.r, 2.0f)) * P.GlitchDistort * P.NoiseForDistortion + 1.0f) *
                      P.Noise * P.GlitchAmount;

  int rgbStripeIndex = (int)(pInCellX * 3.0f);

  float xInStripe = (pInCellX - 0.5f) / (1.0f - P.Gaps * 2.0f) + 0.5f;

  float3 noisyImage = imgCol.rgb + noiseDelta.rgb;
  float3 cc = float3(GetColor(P, 0, xInStripe, pInCellY, noisyImage.r),
                     GetColor(P, 1, xInStripe, pInCellY, noisyImage.g),
                     GetColor(P, 2, xInStripe, pInCellY, noisyImage.b));

  float padding = P.Gaps;
  float yBlur = smoothstep(padding - P.PatternBlurY, padding + P.PatternBlurY, abs(pInCellY - 0.5f));

  float4 pattern = float4(cc * yBlur, 1.0f);

  float4 r = mix(imgCol, pattern, P.PatternAmount * P.Visibility);

  // Vignette
  p.x *= 1.5f;
  r.rgb *= mix(1.5f - P.Vignette * (1.0f - (0.5f - dot(p, p)) - 0.5f), 1.0f, P.Visibility + 1.0f);

  Result.write(r, gid);
}
