// orientpoints — faithful port of external/tixl
// .../Assets/shaders/points/modify/OrientPoints.hlsl. A MODIFIER op: reads a bag of SwPoints
// (SourcePoints), writes the SAME points with each Rotation quaternion re-aimed (ResultPoints).
// Count-preserving — every other attribute (Position/Color/Scale/FX1/FX2) passes through
// untouched; only Rotation changes. A batch-2 modifier (copies transformpoints.metal's shape).
//
// TiXL parity (OrientPoints.hlsl):
//  - strength = Amount * (AmountFactor==0 ? 1 : AmountFactor==1 ? FX1 : FX2)  (FModes weighting).
//  - sign = Flip ? -1 : +1.
//  - Mode 0 (LookAtTarget): aim Z toward Target. TiXL:
//        newRot   = qLookAt(normalize(Target - Position) * sign, normalize(UpVector));
//        forward  = qRotateVec3((0,0,1), newRot);
//        align    = qFromAngleAxis(PI, forward);   // 180° spin about the new forward axis
//        newRot   = qMul(align, newRot);
//        Rotation = normalize(qSlerp(normalize(Rotation), normalize(newRot), strength));
//    Ported 1:1 (qLookAt / qRotateVec3 / qFromAngleAxis / qMul / qSlerp all in quat.metal.h).
//
// Baked / deferred (FLAGGED, same discipline as the generators): Modes 1 (Screen) and 2
// (LookAtCamera) need the camera matrices (WorldToCamera / CameraToWorld) and TiXL's
// qFromMatrix3 — neither exists in the headless lane-A path (no camera cbuffer, no
// qFromMatrix3 helper). They fall through to an identity passthrough (Rotation unchanged) so
// the op is a safe no-op in those modes until a camera transform cbuffer lands; the load-bearing,
// camera-free mode (0 = LookAtTarget, also TiXL's enum default) is the one ported faithfully.
#include <metal_stdlib>
#include "tixl_point.h"            // SwPoint (64B)
#include "orientpoints_params.h"   // OrientParams, OrientBinding
#include "shared/quat.metal.h"     // qLookAt, qRotateVec3, qFromAngleAxis, qMul, qSlerp
using namespace metal;

kernel void orientpoints(const device SwPoint*     src [[buffer(ORIENT_SourcePoints)]],
                         device SwPoint*            dst [[buffer(ORIENT_ResultPoints)]],
                         constant OrientParams&     P   [[buffer(ORIENT_Params)]],
                         uint                       tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  // strength = Amount * FModes weight (None=1, F1=FX1, F2=FX2)
  float weight = (P.AmountFactor == 0) ? 1.0f
               : (P.AmountFactor == 1) ? p.FX1
                                       : p.FX2;
  float strength = P.Amount * weight;

  float sign = (P.Flip > 0) ? -1.0f : 1.0f;
  float3 pos    = float3(p.Position.x, p.Position.y, p.Position.z);
  float3 target = float3(P.TargetX, P.TargetY, P.TargetZ);
  float3 up     = float3(P.UpVectorX, P.UpVectorY, P.UpVectorZ);

  if (P.OrientationMode == 0) {  // LookAtTarget — the camera-free, load-bearing mode
    float4 newRot    = qLookAt(normalize(target - pos) * sign, normalize(up));
    float3 forward   = qRotateVec3(float3(0, 0, 1), newRot);
    float4 alignment = qFromAngleAxis(PI, forward);  // TiXL uses 3.141578; PI is the same intent
    newRot           = qMul(alignment, newRot);
    p.Rotation = normalize(qSlerp(normalize(p.Rotation), normalize(newRot), strength));
  }
  // Modes 1 (Screen) / 2 (LookAtCamera): camera-dependent — baked to passthrough (see header).

  dst[tid] = p;
}
