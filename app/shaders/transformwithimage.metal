// transformwithimage — faithful port of external/tixl
// .../Assets/shaders/points/modify/TranslateWithImage.hlsl (the op GUID is TransformWithImage). A
// texture-into-points seam consumer: each point samples an Image to derive a per-point strength, then
// applies a host-composed TRS TransformMatrix, lerp-blended into Position/Rotation/Scale by that strength.
//
// TiXL parity (TranslateWithImage.hlsl:46-118):
//   pos = p.Position - Center;
//   posInObject = mul(float4(pos,0), transformSampleSpace).xyz;          // for uv (w=0)
//   c = sample(posInObject.xy*(0.5,-0.5)+0.5); gray = (r+g+b)/3;
//   f = gray + (hash11u(index)-0.5)*Scatter; f = ApplyGainAndBias(f, GainAndBias);
//   strength = Strength*(f+StrengthOffset)*(StrengthFactor==0?1 : ==1?p.FX1 : p.FX2);
//   if (TranslationSpace < 0.5) { pos = 0; rotation = identity; } else rotation = orgRot;
//   pos = mul(float4(pos,1), TransformMatrix).xyz;                        // TRS (pivot 0, shear identity)
//   newRotation = TR (recovered from the matrix; we pass it directly);
//   if (TranslationSpace < 0.5) newRotation = qMul(orgRot, newRotation); else newRotation = qMul(newRotation, orgRot);
//   if (TranslationSpace == 0) { pos = qRotate(pos, orgRot)+p.Position; p.Scale *= lerp(1, TScale3, strength); }
//   p.Position = lerp(p.Position, pos, strength);
//   p.Rotation = qSlerp(p.Rotation, newRotation, strength);
//
// Both matrices are TRS (pivot 0, shear identity), so we compose them IN-SHADER from raw scalars (NO packed
// host float4x4 — see transformwithimage_params.h). mul(float4(pos,1), TransformMatrix) ==
// qRotateVec3(pos · TScale3, TR) + Translate, TR = CreateFromYawPitchRoll(Y,X,Z). The matrix-decompose in
// the TiXL kernel (ExtractScale + qFromMatrix3Precise) recovers exactly TScale3 and TR, so passing them
// directly is byte-faithful. fork-channel-scalefx-dead: Channel/ScaleFx1/ScaleFx2 are read by the .cs but
// unused in this kernel. TranslationSpace enum (.cs Spaces): Point=0, Object=1 (.t3 default 1 = Object).
#include <metal_stdlib>
#include "tixl_point.h"                  // SwPoint (64B)
#include "transformwithimage_params.h"   // TransformImgParams, TFIMG_* bindings
#include "shared/quat.metal.h"           // qFromAngleAxis, qMul, qRotateVec3, qSlerp
using namespace metal;

// hash11u — verbatim from shared/hash-functions.hlsl (same as dither.metal).
static inline float tfimgHash11u(uint x) {
  const uint k = 1103515245u;
  const uint _PRIME0 = 13331u;
  x *= _PRIME0;
  x = ((x >> 8u) ^ x) * k;
  x = ((x >> 8u) ^ x) * k;
  return float(x) * (1.0f / float(0xffffffffu));
}
// ApplyGainAndBias — verbatim from shared/bias-functions.hlsl (same as boxgradient.metal).
static inline float tfimgGetBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static inline float tfimgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * tfimgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * tfimgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
static inline float tfimgApplyGainAndBias(float value, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = tfimgGetBias(b, value); value = tfimgGetSchlickBias(g, value); }
  else { value = tfimgGetSchlickBias(g, value); value = tfimgGetBias(b, value); }
  return value;
}

static inline float4 tfimgEulerQuat(float3 degXYZ) {
  float3 rad = degXYZ * (M_PI_F / 180.0f);
  return qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
              qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                   qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll
}

kernel void transformwithimage(const device SwPoint*     src [[buffer(TFIMG_SourcePoints)]],
                               device SwPoint*            dst [[buffer(TFIMG_ResultPoints)]],
                               constant TransformImgParams& P [[buffer(TFIMG_Params)]],
                               texture2d<float>           inputTexture [[texture(TFIMG_InputTexture)]],
                               sampler                    texSampler   [[sampler(TFIMG_TexSampler)]],
                               uint                       tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];
  uint index = tid;

  float3 pos = float3(p.Position) - float3(P.CenterX, P.CenterY, P.CenterZ);

  // transformSampleSpace (uv): posInObject = qRotateVec3(pos · SScale3, SR) (w=0 drops translation).
  float4 SR = tfimgEulerQuat(float3(P.SRotX, P.SRotY, P.SRotZ));
  float3 posInObject = qRotateVec3(pos * float3(P.SScaleX, P.SScaleY, P.SScaleZ), SR);
  float2 uv = posInObject.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
  float4 c = inputTexture.sample(texSampler, uv, level(0.0f));
  float gray = (c.r + c.g + c.b) / 3.0f;

  float f = gray + (tfimgHash11u(index) - 0.5f) * P.Scatter;
  f = tfimgApplyGainAndBias(f, float2(P.GainX, P.GainY));

  float fx = (P.StrengthFactor == 0) ? 1.0f : ((P.StrengthFactor == 1) ? p.FX1 : p.FX2);
  float strength = P.Strength * (f + P.StrengthOffset) * fx;

  // TranslationSpace: Point=0, Object=1 (.t3 default 1). <0.5 == Point.
  float4 orgRot = p.Rotation;
  float4 rotation = orgRot;
  if (P.TranslationSpace < 0.5f) { pos = float3(0.0f); rotation = float4(0, 0, 0, 1); }

  // TRS: mul(float4(pos,1), TransformMatrix) == qRotateVec3(pos · TScale3, TR) + Translate.
  float3 tScale3 = float3(P.TScaleX, P.TScaleY, P.TScaleZ);
  float4 TR = tfimgEulerQuat(float3(P.TRotX, P.TRotY, P.TRotZ));
  float3 movedPos = qRotateVec3(pos * tScale3, TR) + float3(P.TransX, P.TransY, P.TransZ);

  float4 newRotation = normalize(TR);
  if (P.TranslationSpace < 0.5f) newRotation = qMul(orgRot, newRotation);
  else                           newRotation = qMul(newRotation, orgRot);

  if (P.TranslationSpace == 0) {  // Point space
    movedPos = qRotateVec3(movedPos, orgRot) + float3(p.Position);
    p.Scale = float3(p.Scale) * mix(float3(1.0f), tScale3, strength);
  }

  float3 newPos = mix(float3(p.Position), movedPos, strength);
  p.Position = SW_PACKED3{newPos.x, newPos.y, newPos.z};
  p.Rotation = qSlerp(p.Rotation, newRotation, strength);

  dst[tid] = p;
}
