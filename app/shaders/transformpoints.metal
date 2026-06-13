// transformpoints — faithful port of external/tixl
// .../Assets/shaders/points/modify/TransformPoints.hlsl. A MODIFIER op: reads a bag of
// SwPoints (SourcePoints), writes a transformed bag (ResultPoints). The first modifier in
// lane A — the template the batch-2 fan-out copies (in->out bag, count from input).
//
// TiXL parity (TransformPoints.hlsl):
//  - Builds a TRS transform from Translation / Rotation(Euler) / Stretch·Scale about Pivot,
//    applied to position. The .hlsl receives a prebuilt float4x4 + extracts the rotation quat
//    via qFromMatrix3Precise(transpose(rotationMatrix)) (TransformPoints.hlsl:70); we compose the
//    rotation quat directly from the Euler angles (qMul of qFromAngleAxis), which is
//    algebraically identical for a pure rotation and skips passing a packed float4x4 host param.
//  - NAMED FORK — Euler order. The host builds TransformMatrix via the TransformMatrix child op
//    (render/_/TransformMatrix.cs:30-39): yaw=Rotation.Y, pitch=Rotation.X, roll=Rotation.Z, then
//    Quaternion.CreateFromYawPitchRoll(yaw,pitch,roll). .NET CreateFromYawPitchRoll = the Hamilton
//    product Y·X·Z (yaw first, then pitch, then roll), so R = qMul(Ry, qMul(Rx, Rz)) below —
//    mirrors the (batch-16-corrected) polartransformpoints.metal:53-55. The PREVIOUS code built
//    qMul(Rz, qMul(Ry, Rx)) (Z·Y·X) with a comment claiming "X, then Y, then Z" — WRONG; the GPU
//    parity probe (runTransformPointsParityProbe) falsified Z·Y·X and verifies Y·X·Z < 1e-3.
//    (Aside: the CPU-only TransformCpuPoint.cs:67 uses CreateFromYawPitchRoll(Rotation.X,Y,Z) =
//    yaw=X — a DIFFERENT operator with a different binding; not the path TransformPoints.hlsl uses.)
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
  float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                  qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                       qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z); see header fork. probe-verified
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
