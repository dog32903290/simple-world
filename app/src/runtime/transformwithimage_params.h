// Shared host<->shader params for the TiXL-ported TransformWithImage — a texture-into-points seam consumer.
// Mirrors external/tixl .../Assets/shaders/points/modify/TranslateWithImage.hlsl (the op's GUID is
// TransformWithImage; the shader file is named TranslateWithImage). The .hlsl binds TWO cbuffers:
//
//   b0: float4x4 transformSampleSpace; float4x4 TransformMatrix;
//       float Strength; float3 Translate; float3 Scale; float ScaleUniform;
//       float3 Rotate;  float Scatter;    float ScaleFx1; float ScaleFx2; float2 GainAndBias;
//       float3 Center;  float StrengthOffset;
//   b1: int StrengthFactor; int Channel; int TranslationSpace;
//
// We DON'T pass two packed host float4x4s. BOTH matrices are TRS transforms with pivot=0 / shear=identity
// (TransformWithImage.t3 wires a TransformMatrix child with only Translation/Scale/ScaleUniform/Rotation,
// and the _transformSampleSpace compound), so they decompose to (scale3, eulerRotate, translate) which we
// pass as raw scalars and the shader composes — the SAME discipline as transformpoints_params.h /
// samplepointcolorattributes_params.h. NO camera matrices are involved (the .t3 has no camera input).
//
//   transformSampleSpace (uv only): posInObject = qRotateVec3(pos · SScale3, SR) ; SScale3 from Stretch/
//       ImageScale/Aspect, SR = PitchYawRoll(TextureRotate). uv = posInObject.xy*(0.5,-0.5)+0.5.
//   TransformMatrix (the actual move): mul(float4(pos,1), M) == qRotateVec3(pos · TScale3, TR) + Translate,
//       TScale3 = Scale · ScaleUniform, TR = PitchYawRoll(Rotate). The shader's matrix-decompose recovers
//       exactly TScale3 (ExtractScale) and TR (qFromMatrix3Precise) — so passing them directly is faithful.
//
//   strength = Strength · (f + StrengthOffset) · (StrengthFactor==0 ?1 : ==1 ?p.FX1 : p.FX2);
//   f = ApplyGainAndBias(gray + (hash11u(index)-0.5)·Scatter, GainAndBias).
//   ScaleFx1/ScaleFx2/Channel are read by the .cs but the kernel only uses gray (= (r+g+b)/3) for f and
//   StrengthFactor for the FX gate — Channel/ScaleFx1/ScaleFx2 are DEAD in the active kernel (carried for
//   parity). fork-channel-scalefx-dead.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TransformImgParams {
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float CenterX, CenterY, CenterZ;                 // -> 16. Center, subtracted before uv transform
  // transformSampleSpace (uv) — sample-space Scale3 (Aspect folded host) + TextureRotate Euler.
  float SScaleX, SScaleY, SScaleZ, _padS;          // -> 16.
  float SRotX, SRotY, SRotZ, _padSR;               // -> 16. TextureRotate Euler degrees (Y·X·Z)
  // TransformMatrix (the move) — TScale3 = Scale·ScaleUniform + Rotate Euler + Translate.
  float TScaleX, TScaleY, TScaleZ, _padT;          // -> 16.
  float TRotX, TRotY, TRotZ, _padTR;               // -> 16. Rotate Euler degrees (Y·X·Z)
  float TransX, TransY, TransZ, Strength;          // -> 16. Translate + Strength
  float Scatter, GainX, GainY, StrengthOffset;     // -> 16. Scatter + GainAndBias + StrengthOffset
  float StrengthFactor, TranslationSpace, _pad0, _pad1;  // -> 16. b1 ints as floats (0/1/2)
};

enum TransformImgBinding {
  TFIMG_SourcePoints = 0,  // const device SwPoint* (t0)
  TFIMG_ResultPoints = 1,  // device SwPoint*       (u0)
  TFIMG_Params       = 2,  // constant TransformImgParams& (b0/b1 folded)
};
enum TransformImgTexBinding {
  TFIMG_InputTexture = 0,  // Texture2D<float4> inputTexture (t1; texture(0) in MSL)
};
enum TransformImgSamplerBinding {
  TFIMG_TexSampler = 0,    // sampler texSampler (s0)
};

#ifndef __METAL_VERSION__
// 8 rows × 16 = 128 bytes (Count+Center / SScale3+pad / SRot3+pad / TScale3+pad / TRot3+pad /
// Trans3+Strength / Scatter+Gain2+StrengthOffset / StrengthFactor+TranslationSpace+2pad).
static_assert(sizeof(TransformImgParams) == 128, "TransformImgParams must be 128 bytes (8x16)");
#endif
