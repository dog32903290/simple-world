// wrappointposition.metal — faithful Metal port of TiXL WrapPointPosition.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/WrapPointPosition.hlsl
//
// WrapPointPosition is a CUBE FOLD (distinct from WrapPoints which is a floored-mod torus):
//   given p relative to Center (p_local = p.Position - Center):
//   compute Padding = Size.x * 0.1   (uniform padding taken from X component, as TiXL does)
//   padded = halfSize + Padding
//   for each axis: offsetFactor[a] = -1 if p_local[a] > padded[a], +1 if < -padded[a], else 0
//   wrappedP = p_local + Size * offsetFactor
//   output position = wrappedP + Center
//
// TiXL parity (WrapPointPosition.hlsl lines 54-74):
//   float3 p = ResultPoints[i.x].Position - center;
//   float3 Padding = Size.x * 0.1;
//   float3 halfSize = Size/2;
//   float3 padded = halfSize + Padding;
//   float3 offsetFactor = 0;
//   if(abs(p.x) > padded.x) { offsetFactor.x = p.x < 0 ? 1 : -1; }
//   if(abs(p.y) > padded.y) { offsetFactor.y = p.y < 0 ? 1 : -1; }
//   if(abs(p.z) > padded.z) { offsetFactor.z = p.z < 0 ? 1 : -1; }
//   float3 wrappedP = p + Size * offsetFactor;
//   ResultPoints[i.x].Position = wrappedP + center;
//
// NaN / dead-point handling (TiXL lines 49-50 + 56-59):
//   out-of-range index -> skip entirely.
//   NaN input position -> place at center - Size*0.2 with W=0.01 (TiXL recovery path, baked).
//
// W edge-fade (TiXL lines 82-86, the non-linebreak path, WriteLineBreaks=0 baked):
//   distToEdge = halfSize - abs(wrappedP)
//   minDist = saturate(distToEdge * 10)
//   W = minDist.x * minDist.y * minDist.z
//
// NAMED FORK:
//   UseCamera baked to 0 (no camera matrix in cook ctx; center = CenterXYZ from params).
//   WriteLineBreaks baked to 0 (W edge-fade path only; NaN line-break variant deferred).
#include <metal_stdlib>
#include "tixl_point.h"               // SwPoint (64B layout)
#include "wrappointposition_params.h" // WrapPointPositionParams, WrapPointPositionBinding
using namespace metal;

kernel void wrappointposition(
    device const SwPoint*                SourcePoints [[buffer(WRAPPOINTPOS_SourcePoints)]],
    device       SwPoint*                ResultPoints [[buffer(WRAPPOINTPOS_ResultPoints)]],
    constant WrapPointPositionParams&    P            [[buffer(WRAPPOINTPOS_Params)]],
    uint3 tid [[thread_position_in_grid]])
{
    uint idx = tid.x;
    if (idx >= P.Count) return;

    float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);
    float3 size   = float3(P.SizeX,   P.SizeY,   P.SizeZ);

    SwPoint pt = SourcePoints[idx];
    float3 p = pt.Position - center;

    // NaN recovery (TiXL lines 56-59): place at center - Size*0.2 with W=0.01
    if (isnan(p.x + p.y + p.z)) {
        pt.Position = center - size * 0.2f;
        pt.FX1 = 0.01f;
        ResultPoints[idx] = pt;
        return;
    }

    // TiXL: Padding = Size.x * 0.1 (scalar taken from X, broadcast to float3)
    float3 padding  = float3(size.x * 0.1f);
    float3 halfSize = size * 0.5f;
    float3 padded   = halfSize + padding;

    // TiXL lines 69-71: offsetFactor per-axis
    float3 offsetFactor = float3(0.0f);
    if (fabs(p.x) > padded.x) { offsetFactor.x = (p.x < 0.0f) ?  1.0f : -1.0f; }
    if (fabs(p.y) > padded.y) { offsetFactor.y = (p.y < 0.0f) ?  1.0f : -1.0f; }
    if (fabs(p.z) > padded.z) { offsetFactor.z = (p.z < 0.0f) ?  1.0f : -1.0f; }

    float3 wrappedP = p + size * offsetFactor;
    pt.Position = wrappedP + center;

    // W edge-fade (WriteLineBreaks=0 baked, TiXL lines 82-86):
    float3 distToEdge = halfSize - fabs(wrappedP);
    float3 minDist = saturate(distToEdge * 10.0f);
    pt.FX1 = minDist.x * minDist.y * minDist.z;  // W in TiXL LegacyPoint = FX1 in SwPoint

    ResultPoints[idx] = pt;
}
