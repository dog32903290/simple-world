// polartransformpoints.metal — faithful Metal port of TiXL's PolarTransformPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/PolarTransformPoints.hlsl
// A count-preserving MODIFIER: applies a TRS transform to each point, then maps it through a
// cartesian->cylindrical (Mode 0) or cartesian->spherical (Mode 1) polar warp, composing the
// point's rotation with the polar-angle rotations.
//
// TiXL pipeline (PolarTransformPoints.hlsl):
//   pos = mul(float4(pos,1), TransformMatrix);              // TRS pre-transform
//   rotYAxis = qFromAngleAxis(pos.x, (0,1,0));              // angles from the POST-TRS pos
//   rotXAxis = qFromAngleAxis(-pos.y, (1,0,0));
//   if (Mode > 0.5) pos = float3(pos.x, pos.z*sin(pos.y), pos.z*cos(pos.y));  // spherical pre-step
//   pos = float3(pos.z*sin(pos.x), pos.y, pos.z*cos(pos.x));                  // cylindrical wrap
//   if (Mode > 0.5) rot = normalize(qMul(rotXAxis, rot));
//   rot = normalize(qMul(rotYAxis, rot));
//
// NAMED FORK — TRS matrix composed in-shader (not a host float4x4):
//   TiXL builds `TransformMatrix` host-side via the TransformMatrix child operator
//   (render/_/TransformMatrix.cs): M = CreateTransformationMatrix(scalingCenter=pivot,
//   scaling=Scale*UniformScale, rotationCenter=pivot, rotation=PitchYawRoll(Rotation),
//   translation=Translation), then transposed for HLSL's row-vector `mul(float4(pos,1), M)`.
//   PolarTransformPoints exposes NO Pivot / Shear / Invert ports, so pivot=0, shear=0, invert=false.
//   For pivot=0/shear=0, `mul(float4(pos,1), M)` == qRotate(pos * scale, R) + translation — the
//   SAME composition the existing transformpoints.metal uses. We reproduce it directly from the raw
//   TRS scalars, which avoids passing a packed float4x4 host param (the particle_params.h
//   discipline) and reuses the project's qFromAngleAxis Euler order (X then Y then Z).
//   Euler convention: TiXL uses Quaternion.CreateFromYawPitchRoll(yaw=Y, pitch=X, roll=Z).
//   .NET source (Quaternion.CreateFromYawPitchRoll) builds quaternions for yaw/pitch/roll separately
//   and returns cy*cp*cr+sy*sp*sr / cy*sp*cr+sy*cp*sr / cy*cp*sr-sy*sp*cr / cy*cp*cr+sy*sp*sr,
//   which is equivalent to the Hamilton product Y·X·Z (Yaw first, then Pitch, then Roll).
//   Previous comment claimed "equals qMul(Z,qMul(Y,X))" — WRONG; refuter-P falsified that.
//   Corrected to Y·X·Z below (verified by refuter-P GPU probe, batch 16).
#include <metal_stdlib>
#include "tixl_point.h"                    // SwPoint (64B layout)
#include "polartransformpoints_params.h"   // PolarTransformParams, PolarTransformBinding
#include "shared/quat.metal.h"             // qFromAngleAxis, qMul, qRotateVec3
using namespace metal;

kernel void polartransformpoints(
    device const SwPoint* SourcePoints [[buffer(POLARXF_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(POLARXF_ResultPoints)]],
    constant PolarTransformParams& P   [[buffer(POLARXF_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];
    float3 pos = p.Position;
    float4 rot = p.Rotation;

    // --- TRS pre-transform (== mul(float4(pos,1), TransformMatrix), pivot=0/shear=0) ---
    float3 rad = float3(P.RotationX, P.RotationY, P.RotationZ) * (M_PI_F / 180.0f);
    float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                    qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                         qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z); refuter-P verified
    float3 scale = float3(P.ScaleX, P.ScaleY, P.ScaleZ) * P.UniformScale;
    float3 trans = float3(P.TranslationX, P.TranslationY, P.TranslationZ);
    pos = qRotateVec3(pos * scale, R) + trans;

    // --- polar warp (verbatim from PolarTransformPoints.hlsl) ---
    float4 rotYAxis = qFromAngleAxis(pos.x, float3(0, 1, 0));
    float4 rotXAxis = qFromAngleAxis(-pos.y, float3(1, 0, 0));

    if (P.Mode > 0) {  // CartesianToSpherical pre-step
        pos = float3(pos.x,
                     pos.z * sin(pos.y),
                     pos.z * cos(pos.y));
    }

    pos = float3(pos.z * sin(pos.x),
                 pos.y,
                 pos.z * cos(pos.x));

    if (P.Mode > 0) {
        rot = normalize(qMul(rotXAxis, rot));
    }
    rot = normalize(qMul(rotYAxis, rot));

    p.Position = pos;
    p.Rotation = rot;
    ResultPoints[idx] = p;
}
