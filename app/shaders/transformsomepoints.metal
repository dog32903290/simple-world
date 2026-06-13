// transformsomepoints.metal — faithful Metal port of TiXL's TransformSomePoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/TransformSomePoints.hlsl
// A count-preserving MODIFIER: applies a TRS transform to each point, lerp-weighted by the
// point's W channel when WIsWeight>0.5, and composing the point's rotation with the new rotation.
//
// TiXL core pipeline (TransformSomePoints.hlsl:86-140, verbatim logic):
//   float w = p.W;                                           // selection/weight from W channel
//   float3 pos = [ObjectSpace: p.Position] [PointSpace: 0]; // coordinate frame selection
//   float4 rotation = [ObjectSpace: p.Rotation] [PointSpace: identity];
//   pos = mul(float4(pos, 1), TransformMatrix);              // TRS applied
//   if(UpdateRotation) { ... extract newRotation from TransformMatrix, compose with orgRot }
//   if(WIsWeight) { pos = lerp(pLocal, pos, w); newRot = slerp(orgRot, newRot, w); }
//   if(CoordinateSpace==PointSpace) { pos = qRotate(pos, orgRot) + pOrg; }   // back to world
//   p.W = w * ScaleW + OffsetW;                              // W-channel post-processing
//
// NAMED FORK — TRS matrix composed in-shader (not a host float4x4):
//   TiXL passes a prebuilt float4x4 TransformMatrix (built host-side via the TransformMatrix child
//   operator, render/_/TransformMatrix.cs:30-39). TransformSomePoints has no Pivot port (pivot=0,
//   shear=0, invert=false), so `mul(float4(pos,1), M)` == qRotate(pos*scale, R) + translation.
//   We compose the same result from raw TRS scalars — algebraically identical for pivot=0/shear=0,
//   avoids a packed float4x4 host param (the particle_params.h discipline).
//   Euler convention: Y·X·Z = CreateFromYawPitchRoll(yaw=Y, pitch=X, roll=Z) — same as
//   transformpoints.metal (batch-16/17 corrected) and polartransformpoints.metal.
//   The Y·X·Z order is proven correct by refuter-T (batch 17) and refuter-P (batch 16).
//
// BAKED / DEFERRED (flagged — not yet ported):
//   - Take/Skip/OnlyKeepTakes/RangeStart/LengthFactor/Scatter — selection range indexing
//     (TransformSomePoints.hlsl:50-84): all points are transformed in this port.
//   - UpdateRotation — baked to true (rotation always updated, same as TransformPoints.hlsl behavior).
//   - ScaleW/OffsetW — W-channel post-processing; baked to ScaleW=1/OffsetW=0 (W unchanged).
//   - Space=WorldSpace(2) — requires a camera/view transform not available in the cook ctx;
//     baked: treated as ObjectSpace (CoordinateSpace==ObjectSpace) per TiXL default.
//   - TestParam — debug input, no effect on output; baked to 0.
//
// SELECTION WEIGHT semantics (TiXL TransformSomePoints.hlsl:125-130, verbatim):
//   if(WIsWeight >= 0.5) {
//       float3 weightedOffset = (pos - pLocal) * w;  // scale offset by w
//       pos = pLocal + weightedOffset;               // lerp: pos = lerp(pLocal, newPos, w)
//       newRotation = qSlerp(orgRot, newRotation, w); // slerp by w
//   }
//   pLocal is the "pre-transform" position in the working frame:
//     PointSpace:  pLocal = (0,0,0)  (offset is the transform output in local frame)
//     ObjectSpace: pLocal = orgPos   (offset = movement from original position)
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "transformsomepoints_params.h"     // TransformSomeParams, TransformSomeBinding
#include "shared/quat.metal.h"              // qFromAngleAxis, qMul, qRotateVec3, qSlerp
using namespace metal;

kernel void transformsomepoints(
    device const SwPoint*          SourcePoints [[buffer(XFSOME_SourcePoints)]],
    device       SwPoint*          ResultPoints [[buffer(XFSOME_ResultPoints)]],
    constant TransformSomeParams&  P            [[buffer(XFSOME_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];
    float w = p.FX1;                        // selection weight (TiXL LegacyPoint.W == SwPoint.FX1 @12)
    float3 pOrg = p.Position;              // original world position
    float4 orgRot = p.Rotation;            // original rotation

    // --- Build R (Euler Y·X·Z = CreateFromYawPitchRoll(yaw=Y, pitch=X, roll=Z)) ---
    // Mirrors transformpoints.metal:44-46 and polartransformpoints.metal:53-55.
    // THE CORRECT ORDER (batch-16/17 fix): Y·X·Z, NOT the old Z·Y·X.
    float3 rad = float3(P.RotationX, P.RotationY, P.RotationZ) * (M_PI_F / 180.0f);
    float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                    qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                         qFromAngleAxis(rad.z, float3(0, 0, 1))));  // Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z); refuter-T/P verified
    float3 scale = float3(P.StretchX, P.StretchY, P.StretchZ) * P.UniformScale;
    float3 trans = float3(P.TranslationX, P.TranslationY, P.TranslationZ);

    // --- Coordinate frame selection (TransformSomePoints.hlsl:95-98) ---
    // PointSpace:  pos=0, rotation=identity (offset applied in point's own frame)
    // ObjectSpace: pos=orgPos, rotation=orgRot (world-space transform)
    float3 pos;
    float4 rotation;
    if (P.Space < 1) {  // PointSpace
        pos      = float3(0.0f, 0.0f, 0.0f);
        rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);  // identity
    } else {            // ObjectSpace (WorldSpace baked to ObjectSpace — see FORK note)
        pos      = pOrg;
        rotation = orgRot;
    }

    float3 pLocal = pos;  // pre-transform position in working frame (used in WIsWeight lerp)

    // --- Apply TRS (== mul(float4(pos,1), TransformMatrix) for pivot=0/shear=0) ---
    // TransformSomePoints.hlsl:101: pos = mul(float4(pos, 1), TransformMatrix)
    pos = qRotateVec3(pos * scale, R) + trans;

    // --- Rotation composition (UpdateRotation baked to true, same as HLSL:107-122) ---
    // PointSpace:  newRot = qMul(orgRot, R)   -> TiXL line 118: qMul(orgRot, newRotation)
    // ObjectSpace: newRot = qMul(R, orgRot)   -> TiXL line 121: qMul(newRotation, orgRot)
    float4 newRotation;
    if (P.Space < 1) {   // PointSpace
        newRotation = qMul(orgRot, R);
    } else {             // ObjectSpace
        newRotation = qMul(R, orgRot);
    }

    // --- WIsWeight: lerp by selection weight w (TransformSomePoints.hlsl:125-130) ---
    if (P.WIsWeight >= 0.5f) {
        float3 weightedOffset = (pos - pLocal) * w;
        pos         = pLocal + weightedOffset;       // lerp(pLocal, pos, w)
        newRotation = qSlerp(orgRot, newRotation, w);
    }

    // --- PointSpace: rotate offset back into world frame and add to original position ---
    // TransformSomePoints.hlsl:132-134: if(CoordinateSpace < 0.5) { pos=qRotate(pos,orgRot)+pOrg }
    if (P.Space < 1) {
        pos = qRotateVec3(pos, orgRot) + pOrg;
    }

    // Write results — TiXL TransformSomePoints.cs has no Strength port; weighting is solely
    // via WIsWeight × W channel (already applied above, HLSL:125-130).  Direct write matches
    // TiXL semantics exactly (p.Position = pos; p.Rotation = newRotation;).
    p.Position = pos;
    p.Rotation = newRotation;

    // W-channel (FX1): ScaleW/OffsetW baked to identity (ScaleW=1, OffsetW=0 -> FX1 unchanged).
    // BAKED FORK: TiXL line 139: p.W = w * ScaleW + OffsetW (W == FX1 in our layout).
    // p.FX1 unchanged.

    ResultPoints[idx] = p;
}
