// samplepointattributes — faithful port of external/tixl
// .../Assets/shaders/points/modify/SamplePointAttributes.hlsl. A texture-into-points seam consumer:
// each point samples `inputTexture` at a uv derived from its position, then ROUTES the sampled
// brightness(L)/red(R)/green(G)/blue(B) channels — each via its routing ENUM (Attributes) + per-channel
// Factor/Offset gain — into a chosen point attribute (Position xyz / W=FX1 / Rotation / Stretch=Scale).
//
// TiXL parity (SamplePointAttributes.hlsl:55-149):
//   pos = p.Position - Center;
//   posInObject = mul(float4(pos,0), transformSampleSpace).xyz;     // w=0 -> only the 3x3 (Scale·Rot)
//   c = inputTexture.SampleLevel(texSampler, posInObject.xy*(1,-1)+0.5, 0);  gray = (c.r+c.g+c.b)/3;
//   // Rotation: each axis factor = sum over channels whose enum == 5/6/7; incremental qMul X->Y->Z.
//   // Stretch : each axis factor = PRODUCT over channels whose enum == 8/9/10 (else 1); Mode multiplies p.Stretch.
//   // Position: ff = sum over channels of FactorsForPositionAndW[enum]*(channelValue*Factor+Offset);
//   //           offset = Mode<0.5 ? ff.xyz : ff.xyz*p.Position ; (TranslationSpace>0.5 -> qRotate by p.Rotation)
//   //           newPos = p.Position + offset ; (RotationSpace<0.5 -> qRotate newPos by rot2)
//   //           p.W (== FX1) = Mode<0.5 ? p.W+ff.w : p.W*(1+ff.w)
//
// LegacyPoint <-> SwPoint: LegacyPoint.W == SwPoint.FX1 (@12), LegacyPoint.Stretch == SwPoint.Scale (@48)
// (shared/point.hlsl). transformSampleSpace composed in-shader (see samplepointattributes_params.h):
//   posInObject == qRotateVec3(pos · Scale3, R), R = CreateFromYawPitchRoll(Y,X,Z) (project Euler order).
// Sampler (Repeat/Clamp/Mirror per TextureMode + Nearest) is bound host-side; this kernel just samples.
//
// FORKS (named): fork-alpha-dead-in-kernel — the .cs Alpha/AlphaFactor/AlphaOffset inputs are commented
// OUT in the TiXL kernel (.hlsl:47-49), so Alpha is carried by the NodeSpec for 1:1 parity but routes
// nothing here (faithful). The kernel's tau constant is TiXL's literal 3.141578/180 (a TiXL typo for pi;
// ported VERBATIM — do not "fix" to M_PI, that would diverge from byte parity).
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "samplepointattributes_params.h"   // SampleAttrParams, SAMPLEATTR_* bindings
#include "shared/quat.metal.h"              // qFromAngleAxis, qMul, qRotateVec3
using namespace metal;

// FactorsForPositionAndW (SamplePointAttributes.hlsl:4-13): enum 0 none, 1 x, 2 y, 3 z, 4 w, 5 (rotation,
// no position effect). Indexed by clamp(enum,0,5.1).
static constant float4 kFactorsForPositionAndW[6] = {
  float4(0, 0, 0, 0),  // 0 nothing
  float4(1, 0, 0, 0),  // 1 for x
  float4(0, 1, 0, 0),  // 2 for y
  float4(0, 0, 1, 0),  // 3 for z
  float4(0, 0, 0, 1),  // 4 for w
  float4(0, 0, 0, 0),  // 5 avoid rotation effects
};

kernel void samplepointattributes(const device SwPoint*    src [[buffer(SAMPLEATTR_SourcePoints)]],
                                  device SwPoint*           dst [[buffer(SAMPLEATTR_ResultPoints)]],
                                  constant SampleAttrParams& P  [[buffer(SAMPLEATTR_Params)]],
                                  texture2d<float>          inputTexture [[texture(SAMPLEATTR_InputTexture)]],
                                  sampler                   texSampler   [[sampler(SAMPLEATTR_TexSampler)]],
                                  uint                      tid [[thread_position_in_grid]]) {
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
  float4 c = inputTexture.sample(texSampler, uv, level(0.0f));
  float gray = (c.r + c.g + c.b) / 3.0f;

  const float Lenum = P.L, Renum = P.R, Genum = P.G, Benum = P.B;

  // ---- Rotation (.hlsl:78-119): per-axis factor = sum over channels whose enum == 5/6/7 ----
  float rotXFactor = (Renum == 5 ? (c.r * P.RFactor + P.ROffset) : 0) +
                     (Genum == 5 ? (c.g * P.GFactor + P.GOffset) : 0) +
                     (Benum == 5 ? (c.b * P.BFactor + P.BOffset) : 0) +
                     (Lenum == 5 ? (gray * P.LFactor + P.LOffset) : 0);
  float rotYFactor = (Renum == 6 ? (c.r * P.RFactor + P.ROffset) : 0) +
                     (Genum == 6 ? (c.g * P.GFactor + P.GOffset) : 0) +
                     (Benum == 6 ? (c.b * P.BFactor + P.BOffset) : 0) +
                     (Lenum == 6 ? (gray * P.LFactor + P.LOffset) : 0);
  float rotZFactor = (Renum == 7 ? (c.r * P.RFactor + P.ROffset) : 0) +
                     (Genum == 7 ? (c.g * P.GFactor + P.GOffset) : 0) +
                     (Benum == 7 ? (c.b * P.BFactor + P.BOffset) : 0) +
                     (Lenum == 7 ? (gray * P.LFactor + P.LOffset) : 0);

  float tau = 3.141578f / 180.0f;  // VERBATIM TiXL constant (a TiXL pi typo; ported for byte parity)
  float4 rot2 = float4(0, 0, 0, 1);
  if (rotXFactor != 0) rot2 = qMul(rot2, qFromAngleAxis(rotXFactor * tau, float3(1, 0, 0)));
  if (rotYFactor != 0) rot2 = qMul(rot2, qFromAngleAxis(rotYFactor * tau, float3(0, 1, 0)));
  if (rotZFactor != 0) rot2 = qMul(rot2, qFromAngleAxis(rotZFactor * tau, float3(0, 0, 1)));
  rot2 = normalize(rot2);
  p.Rotation = qMul(p.Rotation, rot2);

  // ---- Stretch (.hlsl:122-148): per-axis factor = PRODUCT over channels whose enum == 8/9/10 (else 1) --
  float3 stretchFactor = float3(
      (Renum == 8 ? (c.r * P.RFactor + P.ROffset) : 1) *
      (Genum == 8 ? (c.g * P.GFactor + P.GOffset) : 1) *
      (Benum == 8 ? (c.b * P.BFactor + P.BOffset) : 1) *
      (Lenum == 8 ? (gray * P.LFactor + P.LOffset) : 1),
      (Renum == 9 ? (c.r * P.RFactor + P.ROffset) : 1) *
      (Genum == 9 ? (c.g * P.GFactor + P.GOffset) : 1) *
      (Benum == 9 ? (c.b * P.BFactor + P.BOffset) : 1) *
      (Lenum == 9 ? (gray * P.LFactor + P.LOffset) : 1),
      (Renum == 10 ? (c.r * P.RFactor + P.ROffset) : 1) *
      (Genum == 10 ? (c.g * P.GFactor + P.GOffset) : 1) *
      (Benum == 10 ? (c.b * P.BFactor + P.BOffset) : 1) *
      (Lenum == 10 ? (gray * P.LFactor + P.LOffset) : 1));
  // p.Scale == LegacyPoint.Stretch. Mode<0.5 (Add) -> stretchOffset = stretchFactor; else * p.Scale.
  float3 stretchOffset = P.Mode < 0.5f ? stretchFactor : (stretchFactor * float3(p.Scale));
  p.Scale = float3(p.Scale) * stretchOffset;

  // ---- Position + W (.hlsl:152-176) ----
  float4 ff = kFactorsForPositionAndW[(uint)clamp(Lenum, 0.0f, 5.1f)] * (gray * P.LFactor + P.LOffset) +
              kFactorsForPositionAndW[(uint)clamp(Renum, 0.0f, 5.1f)] * (c.r * P.RFactor + P.ROffset) +
              kFactorsForPositionAndW[(uint)clamp(Genum, 0.0f, 5.1f)] * (c.g * P.GFactor + P.GOffset) +
              kFactorsForPositionAndW[(uint)clamp(Benum, 0.0f, 5.1f)] * (c.b * P.BFactor + P.BOffset);

  float3 offset = P.Mode < 0.5f ? float3(ff.xyz) : (float3(ff.xyz) * float3(p.Position));
  if (P.TranslationSpace > 0.5f) offset = qRotateVec3(offset, p.Rotation);
  float3 newPos = float3(p.Position) + offset;
  if (P.RotationSpace < 0.5f) newPos = qRotateVec3(newPos, rot2);
  p.Position = SW_PACKED3{newPos.x, newPos.y, newPos.z};

  p.FX1 = P.Mode < 0.5f ? (p.FX1 + ff.w) : (p.FX1 * (1.0f + ff.w));  // LegacyPoint.W == SwPoint.FX1

  dst[tid] = p;
}
