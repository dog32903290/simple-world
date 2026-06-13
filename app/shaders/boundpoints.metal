// boundpoints.metal — faithful Metal port of TiXL's BoundPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/BoundPoints.hlsl
// A count-preserving MODIFIER: each point's Position is CLAMPED into an axis-aligned box of
// (Size * UniformScale) centered at Position. (The commented-out jitter block in the .hlsl is dead
// in TiXL too — we port only the live tail.)
//
// TiXL parity (BoundPoints.hlsl lines 58-69):
//   float3 size      = Size * UniformScale;
//   float3 halfSize  = size * 0.5;
//   float3 minBounds = Position - halfSize;
//   float3 maxBounds = Position + halfSize;
//   ResultPoints[i]  = SourcePoints[i];
//   ResultPoints[i].Position = clamp(SourcePoints[i].Position, minBounds, maxBounds);
//
// No fork: clamp() is identical in MSL and HLSL. All-scalar params (no packed_float3 trap).
#include <metal_stdlib>
#include "tixl_point.h"              // SwPoint (64B layout)
#include "boundpoints_params.h"      // BoundPointsParams, BoundPointsBinding
using namespace metal;

kernel void boundpoints(
    device const SwPoint* SourcePoints [[buffer(BOUNDPOINTS_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(BOUNDPOINTS_ResultPoints)]],
    constant BoundPointsParams& P      [[buffer(BOUNDPOINTS_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    float3 Position = float3(P.PositionX, P.PositionY, P.PositionZ);
    float3 Size     = float3(P.SizeX, P.SizeY, P.SizeZ);

    float3 size      = Size * P.UniformScale;
    float3 halfSize  = size * 0.5f;
    float3 minBounds = Position - halfSize;
    float3 maxBounds = Position + halfSize;

    SwPoint p = SourcePoints[idx];          // carry all attributes (ResultPoints[i]=SourcePoints[i])
    p.Position = clamp(p.Position, minBounds, maxBounds);
    ResultPoints[idx] = p;
}
