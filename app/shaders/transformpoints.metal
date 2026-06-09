// transformpoints — faithful port of external/tixl
// .../Assets/shaders/points/modify/TransformPoints.hlsl. A MODIFIER op: reads a bag of
// SwPoints (SourcePoints), writes a transformed bag (ResultPoints). The first modifier in
// lane A — the template the batch-2 fan-out copies (in->out bag, count from input).
//
// TiXL parity (TransformPoints.hlsl):
//  - Builds a TRS transform from Translation / Rotation(Euler) / Stretch·Scale about Pivot,
//    applied to position. The .hlsl receives a prebuilt float4x4 + extracts the rotation quat
//    via qFromMatrix3Precise; we compose the rotation quat directly from the Euler angles
//    (qFromAngleAxis, X then Y then Z) — algebraically identical for a pure rotation, and it
//    skips qFromMatrix3 (not in our quat helpers).
//  - transform(v) = qRotate((v - Pivot) * scale, R) + Pivot + Translation. PointSpace feeds
//    v=0 (the offset is then rotated into the point's own frame and added to its position,
//    TiXL lines 53/87-90); ObjectSpace feeds v=Position (world transform, lines 58/96).
//  - Orientation: PointSpace newRot = qMul(orgRot, R); ObjectSpace newRot = qMul(R, orgRot).
//  - Strength lerps old->new for BOTH position (lerp) and rotation (slerp), TiXL lines 96-97.
//
// Baked / deferred (flagged, same discipline as the generators): Shearing, StrengthFactor
// (F1/F2 weighting -> Strength always full), ScaleW/OffsetW/WIsWeight, UpdateRotation. These
// are extra TiXL inputs not load-bearing for the core transform; add them the same way later.
#include <metal_stdlib>
#include "tixl_point.h"               // SwPoint (64B)
#include "transformpoints_params.h"   // TransformParams, TransformBinding
#include "shared/quat.metal.h"        // qFromAngleAxis, qMul, qRotateVec3, qSlerp
using namespace metal;

kernel void transformpoints(const device SwPoint*       src [[buffer(TRANSFORM_SourcePoints)]],
                            device SwPoint*              dst [[buffer(TRANSFORM_ResultPoints)]],
                            constant TransformParams&     P   [[buffer(TRANSFORM_Params)]],
                            uint                          tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  float3 rad = float3(P.RotationX, P.RotationY, P.RotationZ) * (M_PI_F / 180.0f);
  float4 R = qMul(qFromAngleAxis(rad.z, float3(0, 0, 1)),
                  qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                       qFromAngleAxis(rad.x, float3(1, 0, 0))));  // X, then Y, then Z
  float3 scale = float3(P.StretchX, P.StretchY, P.StretchZ) * P.Scale;
  float3 pivot = float3(P.PivotX, P.PivotY, P.PivotZ);
  float3 trans = float3(P.TranslationX, P.TranslationY, P.TranslationZ);

  // transform(v) = qRotate((v - Pivot) * scale, R) + Pivot + Translation
  float3 orgPos = p.Position;
  float4 orgRot = p.Rotation;
  float3 newPos;
  float4 newRot;
  if (P.Space < 1) {                                    // PointSpace
    float3 offset = qRotateVec3((float3(0.0f) - pivot) * scale, R) + pivot + trans;
    newPos = qRotateVec3(offset, orgRot) + orgPos;      // offset in the point's own frame
    newRot = qMul(orgRot, R);
  } else {                                              // ObjectSpace
    newPos = qRotateVec3((orgPos - pivot) * scale, R) + pivot + trans;
    newRot = qMul(R, orgRot);
  }

  p.Position = mix(orgPos, newPos, P.Strength);
  p.Rotation = qSlerp(orgRot, newRot, P.Strength);
  dst[tid] = p;
}
