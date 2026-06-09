// setpointattributes — faithful port of external/tixl
// .../Assets/shaders/points/modify/SetPointAttributes.hlsl. A MODIFIER op: reads a bag of
// SwPoints (SourcePoints), writes a count-preserving bag (ResultPoints) where each Set* flag,
// when on, lerps (Rotation: slerps) the attribute toward a supplied target by `strength`.
//
// TiXL parity (SetPointAttributes.hlsl lines 52-76):
//   strength = Amount * (AmountFactor==0 ? 1 : AmountFactor==1 ? FX1 : FX2)
//   if SetColor:    Color    = lerp(Color, target, strength)
//   if SetPosition: Position = lerp(Position, target, strength)
//   if SetStretch:  Scale    = lerp(Scale, targetStretch, strength)   (.cs "Extend")
//   if SetF1:       FX1      = lerp(FX1, target, strength)
//   if SetF2:       FX2      = lerp(FX2, target, strength)
//   if SetRotation: Rotation = qSlerp(Rotation, qFromAngleAxis(angle*PI/180, axis), strength)
// Order matches the .hlsl (Color first, then Position/Stretch/F1/F2/Rotation). Because each
// branch reads its OWN field and writes the same field, ordering only matters if AmountFactor
// pointed at FX1/FX2 while SetF1/SetF2 also fired; TiXL computes `strength` once up front from
// the ORIGINAL FX1/FX2 (before any write), and so do we — strength is captured before the writes.
//
// NOTE on AmountFactor==1/2 with packed_float3: SwPoint.Scale is packed_float3; we read FX1/FX2
// scalars (which is what TiXL weights by), not Scale, so no packed read of a vector is needed for
// the factor. Stretch writes go to p.Scale via a packed_float3 store (mix returns float3 -> assign).
#include <metal_stdlib>
#include "tixl_point.h"                 // SwPoint (64B)
#include "setpointattributes_params.h"  // SetPointAttributesParams, SetPtAttrBinding
#include "shared/quat.metal.h"          // qFromAngleAxis, qSlerp
using namespace metal;

kernel void setpointattributes(const device SwPoint*                  src [[buffer(SETPTATTR_SourcePoints)]],
                               device SwPoint*                        dst [[buffer(SETPTATTR_ResultPoints)]],
                               constant SetPointAttributesParams&     P   [[buffer(SETPTATTR_Params)]],
                               uint                                   tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  // strength captured from the ORIGINAL FX1/FX2 (before any Set* write), TiXL line 54.
  float factor = (P.AmountFactor == 0) ? 1.0f : (P.AmountFactor == 1) ? p.FX1 : p.FX2;
  float strength = P.Amount * factor;

  if (P.SetColor != 0)
    p.Color = mix(p.Color, float4(P.ColorR, P.ColorG, P.ColorB, P.ColorA), strength);

  if (P.SetPosition != 0)
    p.Position = mix(float3(p.Position), float3(P.PositionX, P.PositionY, P.PositionZ), strength);

  if (P.SetStretch != 0)
    p.Scale = mix(float3(p.Scale), float3(P.StretchX, P.StretchY, P.StretchZ), strength);

  if (P.SetFx1 != 0)
    p.FX1 = mix(p.FX1, P.Fx1, strength);

  if (P.SetFx2 != 0)
    p.FX2 = mix(p.FX2, P.Fx2, strength);

  if (P.SetRotation != 0) {
    float4 target = qFromAngleAxis(P.RotationAngle / 180.0f * PI,
                                   float3(P.RotationAxisX, P.RotationAxisY, P.RotationAxisZ));
    p.Rotation = qSlerp(p.Rotation, target, strength);
  }

  dst[tid] = p;
}
