// attributesfromimagechannels — faithful port of external/tixl
// .../Assets/shaders/points/modify/AttributesFromImageChannels.hlsl. A Points op with a Texture2D INPUT:
// each point samples `inputTexture` at a uv derived from its position, then routes the sampled R/G/B and
// brightness(L) channels — each through a per-channel Factor/Offset gain — into a SELECTED point
// attribute (position xyz / F1 / F2 / rotate xyz / scale xyz / scale-uniform), scaled by Strength·alpha.
//
// TiXL parity (AttributesFromImageChannels.hlsl:110-156):
//   pos = p.Position - Center;
//   posInObject = mul(float4(pos,0), transformSampleSpace).xyz;     // w=0 -> only the 3x3 (Scale·Rot)
//   c = inputTexture.SampleLevel(s, posInObject.xy*float2(0.5,-0.5)+0.5, 0);
//   gray = (c.r+c.g+c.b)/3;
//   rgbl = ApplyGainAndBias(float4(c.rgb, gray), GainAndBias);      // bias-functions.hlsl float4 overload
//   strength = Strength * c.a * (StrengthFactor==0?1 : StrengthFactor==1?p.FX1 : p.FX2);
//   factors[clamp(L,0,12)] += (rgbl.w*LFactor + LOffset) * strength;   // L=Brightness routes luminance
//   factors[clamp(R,0,12)] += (rgbl.r*RFactor + ROffset) * strength;
//   factors[clamp(G,0,12)] += (rgbl.g*GFactor + GOffset) * strength;
//   factors[clamp(B,0,12)] += (rgbl.b*BFactor + BOffset) * strength;
//   p.Position += float3(factors[X],factors[Y],factors[Z]) * strength;   // (TranslationSpace==1 -> rotate)
//   p.Scale    += (float3(factors[ScaleX],factors[ScaleY],factors[ScaleZ]) + factors[ScaleUniform]) * strength;
//   p.FX1 += factors[F1]*strength;  p.FX2 += factors[F2]*strength;
//   deltaRot = qMul over -factors[RotX/Y/Z] (degrees); p.Rotation = normalize(deltaRot);
//
// NOTE the DOUBLE-strength on position/scale/F1/F2/rotate: the per-channel `+= (... )*strength` already
// folds strength into `factors`, then position/scale multiply by `strength` AGAIN (.hlsl:131-149). This
// is a TiXL quirk (faithfully reproduced). The ACTIVE kernel ALWAYS writes p.Rotation (overwriting it
// with the deltaRot built from the routed Rotate_* factors) — the Mode/Spaces inputs feed only the
// large commented-out block (.hlsl:158-222), so Mode is DEAD in the live path (kept in the cbuffer for
// parity). TranslationSpace==1 rotates the position offset into the point frame (.hlsl:136-139).
//
// transformSampleSpace is composed IN-SHADER (see attributesfromimagechannels_params.h for the full .t3
// trace): posInObject == qRotateVec3(pos · Scale3, R) where Scale3 already has Aspect AND the .t3
// TransformMatrix UniformScale=0.5 folded host-side, R = PitchYawRoll(TextureRotate) (Y·X·Z).
#include <metal_stdlib>
#include "tixl_point.h"                          // SwPoint (64B)
#include "attributesfromimagechannels_params.h"  // AficParams, AFIC_* bindings
#include "shared/quat.metal.h"                    // qFromAngleAxis, qMul, qRotateVec3
using namespace metal;

// Attribute routing indices — verbatim from AttributesFromImageChannels.hlsl:58-72 / .cs Attributes enum.
#define ATTR_NotUsed       0
#define ATTR_Position_X    1
#define ATTR_Position_Y    2
#define ATTR_Position_Z    3
#define ATTR_F1            4
#define ATTR_F2            5
#define ATTR_Rotate_X      6
#define ATTR_Rotate_Y      7
#define ATTR_Rotate_Z      8
#define ATTR_Scale_Uniform 9
#define ATTR_Scale_X       10
#define ATTR_Scale_Y       11
#define ATTR_Scale_Z       12
#define ATTR_CountMax      12
#define ATTR_Count         13

// GetBias / GetSchlickBias / ApplyGainAndBias — verbatim port of external/tixl
// .../Assets/shaders/shared/bias-functions.hlsl (float4 overload, lines 50-89). The float4 overload's
// hiMask/loMask "result" is DEAD in TiXL (it returns v4, not result) — reproduced faithfully (no clamp).
// With the .t3 default GainAndBias=(0.5,0.5): GetBias(0.5,x)=x and GetSchlickBias(x,0.5)=x => identity.
static float4 aficGetBias(float bias, float4 x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static float4 aficGetSchlickBias(float4 x, float gain) {
  return select(aficGetBias(1.0f - gain, x * 2.0f - 1.0f) / 2.0f + 0.5f,
                aficGetBias(gain, x * 2.0f) / 2.0f,
                x < 0.5f);
}
static float4 aficApplyGainAndBias(float4 v4, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (g < 0.5f) {
    v4 = aficGetBias(b, v4);
    v4 = aficGetSchlickBias(v4, g);
  } else {
    v4 = aficGetSchlickBias(v4, g);
    v4 = aficGetBias(b, v4);
  }
  return v4;  // TiXL returns v4 (the hiMask/loMask `result` path is dead code there too)
}

kernel void attributesfromimagechannels(const device SwPoint* src [[buffer(AFIC_SourcePoints)]],
                                        device SwPoint*       dst [[buffer(AFIC_ResultPoints)]],
                                        constant AficParams&  P   [[buffer(AFIC_Params)]],
                                        texture2d<float>      inputTexture [[texture(AFIC_InputTexture)]],
                                        sampler               texSampler   [[sampler(AFIC_TexSampler)]],
                                        uint                  tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  float3 pos = float3(p.Position) - float3(P.CenterX, P.CenterY, P.CenterZ);

  // transformSampleSpace: mul(float4(pos,0), M) == qRotateVec3(pos · Scale3, R) (w=0 drops translation).
  float3 rad = float3(P.RotX, P.RotY, P.RotZ) * (M_PI_F / 180.0f);
  float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                  qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                       qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll
  float3 scale3 = float3(P.ScaleX, P.ScaleY, P.ScaleZ);           // Aspect + UniformScale 0.5 folded host
  float3 posInObject = qRotateVec3(pos * scale3, R);

  // uv (.hlsl:116): posInObject.xy * float2(0.5,-0.5) + 0.5  — AFIC's OWN scale (SPCA used (1,-1)).
  float2 uv = posInObject.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
  float4 c = inputTexture.sample(texSampler, uv, level(0.0f));   // SampleLevel(...,0) -> explicit LOD 0
  float gray = (c.r + c.g + c.b) / 3.0f;

  float4 rgbl = aficApplyGainAndBias(float4(c.rgb, gray), float2(P.GainAndBiasX, P.GainAndBiasY));

  float factors[ATTR_Count] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  float strength = P.Strength * c.a *
                   (P.StrengthFactor == 0 ? 1.0f : (P.StrengthFactor == 1 ? p.FX1 : p.FX2));

  factors[clamp(P.L, 0, ATTR_CountMax)] += (rgbl.w * P.LFactor + P.LOffset) * strength;
  factors[clamp(P.R, 0, ATTR_CountMax)] += (rgbl.r * P.RFactor + P.ROffset) * strength;
  factors[clamp(P.G, 0, ATTR_CountMax)] += (rgbl.g * P.GFactor + P.GOffset) * strength;
  factors[clamp(P.B, 0, ATTR_CountMax)] += (rgbl.b * P.BFactor + P.BOffset) * strength;

  float3 offset = float3(factors[ATTR_Position_X], factors[ATTR_Position_Y],
                         factors[ATTR_Position_Z]) * strength;  // (the DOUBLE-strength, .hlsl:131-134)
  if (P.TranslationSpace == 1) {
    offset = qRotateVec3(offset, p.Rotation);
  }
  p.Position = float3(p.Position) + offset;

  p.Scale = float3(p.Scale) + (float3(factors[ATTR_Scale_X], factors[ATTR_Scale_Y],
                                      factors[ATTR_Scale_Z]) +
                               factors[ATTR_Scale_Uniform]) * strength;

  p.FX1 += factors[ATTR_F1] * strength;
  p.FX2 += factors[ATTR_F2] * strength;

  const float kDeg2Rad = M_PI_F / 180.0f;  // radians() (HLSL) -> explicit conversion in MSL
  float4 deltaRot = float4(0, 0, 0, 1);
  deltaRot = qMul(deltaRot, qFromAngleAxis(-factors[ATTR_Rotate_X] * kDeg2Rad, float3(1, 0, 0)));
  deltaRot = qMul(deltaRot, qFromAngleAxis(-factors[ATTR_Rotate_Y] * kDeg2Rad, float3(0, 1, 0)));
  deltaRot = qMul(deltaRot, qFromAngleAxis(-factors[ATTR_Rotate_Z] * kDeg2Rad, float3(0, 0, 1)));
  p.Rotation = normalize(deltaRot);

  dst[tid] = p;
}
