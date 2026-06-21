// samplepointcolorattributes — faithful port of external/tixl
// .../Assets/shaders/points/modify/SamplePointColorAttributes.hlsl. The FIRST Points op with a
// Texture2D INPUT: each point samples `inputTexture` at a uv derived from its position, multiplies by
// BaseColor, and BLENDS the result into its own Color via BlendColors (shared/blend-functions.hlsl).
// This is the proving op for the texture-into-points seam (PointCookCtx::inputTextures).
//
// TiXL parity (SamplePointColorAttributes.hlsl:35-47):
//   pos = p.Position - Center;
//   posInObject = mul(float4(pos,0), transformSampleSpace).xyz;       // w=0 -> only the 3x3 (Scale·Rot)
//   uv = posInObject.xy * float2(1,-1) + float2(0.5,0.5);             // y-flip + center
//   c = inputTexture.SampleLevel(texSampler, uv, 0) * BaseColor;
//   p.Color = BlendColors(p.Color, c, (int)Mode);
//
// transformSampleSpace is composed IN-SHADER from raw scalars (see samplepointcolorattributes_params.h
// for the full .t3 trace). M = CreateTransformationMatrix(pivot=0, scalingRotation=Identity,
// scaling=Scale3, rotation=PitchYawRoll(TextureRotate), translation=Center).Transpose(). The kernel
// samples with mul(float4(pos,0), M): w=0 DROPS the translation row (Center is applied separately by
// `pos -= Center`), so posInObject == qRotateVec3(pos · Scale3, R) — the SAME scale-then-rotate
// composition transformpoints.metal / polartransformpoints.metal use, minus the (dropped) translation.
//   Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, Scale)  [Aspect = texW/texH, folded host-side]
//   R      = qMul(yaw=Y, qMul(pitch=X, roll=Z)) = Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)
//            (the project's refuter-P-verified Euler order; degrees -> radians).
// The sampler (Repeat wrap + Nearest filter, .t3 TextureMode=Wrap / SamplerState=MinMagMipPoint) is
// bound host-side; this kernel just samples at the computed uv.
#include <metal_stdlib>
#include "tixl_point.h"                       // SwPoint (64B)
#include "samplepointcolorattributes_params.h" // SpcaParams, SPCA_* bindings
#include "shared/quat.metal.h"                 // qFromAngleAxis, qMul, qRotateVec3
using namespace metal;

// BlendColors — verbatim port of external/tixl .../Assets/shaders/shared/blend-functions.hlsl:1-72.
// tA = the point's existing color, tB = the (BaseColor-multiplied) sampled texel, blendMode = Mode.
static float4 spcaBlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);

  float a = tA.a + tB.a - tA.a * tB.a;
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);

  switch (blendMode) {
    case 0:  // normal
      rgb = rgbNormalBlended;
      break;
    case 1:  // screen
      rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a);
      break;
    case 2:  // multiply
      rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a);
      break;
    case 3:  // overlay
      rgb = float3(
          tA.r < 0.5f ? (2.0f * tA.r * tB.r) : (1.0f - 2.0f * (1.0f - tA.r) * (1.0f - tB.r)),
          tA.g < 0.5f ? (2.0f * tA.g * tB.g) : (1.0f - 2.0f * (1.0f - tA.g) * (1.0f - tB.g)),
          tA.b < 0.5f ? (2.0f * tA.b * tB.b) : (1.0f - 2.0f * (1.0f - tA.b) * (1.0f - tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a);
      break;
    case 4:  // difference
      rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a);
      break;
    case 5:  // use a
      rgb = tA.rgb;
      break;
    case 6:  // use b
      rgb = tB.rgb;
      break;
    case 7:  // colorDodge
      rgb = tA.rgb / (1.0001f - saturate(tB.rgb));
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

kernel void samplepointcolorattributes(const device SwPoint*  src [[buffer(SPCA_SourcePoints)]],
                                       device SwPoint*         dst [[buffer(SPCA_ResultPoints)]],
                                       constant SpcaParams&    P   [[buffer(SPCA_Params)]],
                                       texture2d<float>        inputTexture [[texture(SPCA_InputTexture)]],
                                       sampler                 texSampler   [[sampler(SPCA_TexSampler)]],
                                       uint                    tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  float3 pos = float3(p.Position) - float3(P.CenterX, P.CenterY, P.CenterZ);

  // transformSampleSpace: mul(float4(pos,0), M) == qRotateVec3(pos · Scale3, R) (w=0 drops translation).
  float3 rad = float3(P.RotX, P.RotY, P.RotZ) * (M_PI_F / 180.0f);
  float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                  qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                       qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll
  float3 scale3 = float3(P.ScaleX, P.ScaleY, P.ScaleZ);
  float3 posInObject = qRotateVec3(pos * scale3, R);

  float2 uv = posInObject.xy * float2(1.0f, -1.0f) + float2(0.5f, 0.5f);

  float4 c = inputTexture.sample(texSampler, uv, level(0.0f));  // SampleLevel(...,0) -> explicit LOD 0
  c *= float4(P.BaseColorR, P.BaseColorG, P.BaseColorB, P.BaseColorA);

  p.Color = spcaBlendColors(p.Color, c, (int)P.Mode);
  dst[tid] = p;
}
