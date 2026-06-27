// displacepoints2d — faithful port of external/tixl
// .../Assets/shaders/points/modify/DisplacePoints2d.hlsl. A texture-into-points seam consumer: each point
// samples a DisplaceMap, takes the CENTRAL-DIFFERENCE gradient of the gray map (±SampleRadius/mapDim),
// turns it into an angle, and shifts Position.xy along that direction by DisplaceAmount/100.
//
// TiXL parity (DisplacePoints2d.hlsl:36-92):
//   pos = P.Position - Center;
//   posInObject = mul(float4(pos,0), WorldToObject).xyz;            // op-local transform inverse (w=0)
//   uv = posInObject.xy * float2(1,-1) + float2(0.5,0.5);
//   sx = SampleRadius / mapW; sy = SampleRadius / mapH;
//   x1 = gray(uv+(sx,0)); x2 = gray(uv-(sx,0)); y1 = gray(uv+(0,sy)); y2 = gray(uv-(0,sy));
//   d = float2(x1-x2, y1-y2); d.y *= -1;
//   a = (d==0) ? 0 : atan2(d.x, d.y) + Twist/180*pi;               // TiXL literal 3.14158 (typo for pi)
//   direction = float2(sin(a), cos(a)); len = length(d);
//   if (len > 0.0001) Position += float3(direction*DisplaceAmount/100, 0);
//
// WorldToObject (op-local, NO camera — fork-worldtoobject-op-local, see displacepoints2d_params.h):
//   ObjectToWorld 3x3 = R · diag(Scale3); posInObject = mul(pos, (R·S)^-1) == qRotateVec3(pos,conj(R))/Scale3.
// The DisplaceMap sampler (wrap from TextureMode + Linear: TiXL SamplerState default LINEAR for a gradient
// read) is bound host-side. DisplaceOffset is read by the .cs but UNUSED in the kernel (dead; not passed).
#include <metal_stdlib>
#include "tixl_point.h"                 // SwPoint (64B)
#include "displacepoints2d_params.h"    // DisplaceParams2d, DISP2D_* bindings
#include "shared/quat.metal.h"          // qFromAngleAxis, qMul, qRotateVec3, qConjugate
using namespace metal;

static inline float dp2dGray(texture2d<float> tex, sampler s, float2 uv) {
  float4 c = tex.sample(s, uv, level(0.0f));
  return (c.r + c.g + c.b) / 3.0f;
}

kernel void displacepoints2d(const device SwPoint*    src [[buffer(DISP2D_SourcePoints)]],
                             device SwPoint*           dst [[buffer(DISP2D_ResultPoints)]],
                             constant DisplaceParams2d& P  [[buffer(DISP2D_Params)]],
                             texture2d<float>          displaceMap [[texture(DISP2D_DisplaceMap)]],
                             sampler                   texSampler  [[sampler(DISP2D_TexSampler)]],
                             uint                      tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  float3 pos = float3(p.Position) - float3(P.CenterX, P.CenterY, P.CenterZ);

  // posInObject = mul(float4(pos,0), WorldToObject) == qRotateVec3(pos, conj(R)) / Scale3 (w=0, op-local).
  float3 rad = float3(P.RotX, P.RotY, P.RotZ) * (M_PI_F / 180.0f);
  float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                  qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                       qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll
  float3 scale3 = float3(P.ScaleX, P.ScaleY, P.ScaleZ);
  float3 posInObject = qRotateVec3(pos, qConjugate(R)) / scale3;

  float2 uv = posInObject.xy * float2(1.0f, -1.0f) + float2(0.5f, 0.5f);

  float mapW = (float)displaceMap.get_width();
  float mapH = (float)displaceMap.get_height();
  float sx = P.SampleRadius / mapW;
  float sy = P.SampleRadius / mapH;

  float x1 = dp2dGray(displaceMap, texSampler, float2(uv.x + sx, uv.y));
  float x2 = dp2dGray(displaceMap, texSampler, float2(uv.x - sx, uv.y));
  float y1 = dp2dGray(displaceMap, texSampler, float2(uv.x, uv.y + sy));
  float y2 = dp2dGray(displaceMap, texSampler, float2(uv.x, uv.y - sy));

  float2 d = float2(x1 - x2, y1 - y2);
  d.y *= -1.0f;
  float a = (d.x == 0.0f && d.y == 0.0f) ? 0.0f
                                         : atan2(d.x, d.y) + P.Twist / 180.0f * 3.14158f;  // TiXL literal
  float2 direction = float2(sin(a), cos(a));
  float len = length(d);

  if (len > 0.0001f) {
    float3 np = float3(p.Position) + float3(direction * P.DisplaceAmount / 100.0f, 0.0f);
    p.Position = SW_PACKED3{np.x, np.y, np.z};
  }
  dst[tid] = p;
}
