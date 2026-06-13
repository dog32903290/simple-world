// hexgridpoints.metal — faithful Metal port of TiXL HexGridPoints.hlsl (Pattern=2 Hexa)
// Source: external/tixl/Operators/Lib/Assets/shaders/points/generate/HexGridPoints.hlsl
//
// TiXL parity (HexGridPoints.hlsl Pattern=2 branch, lines 93-114):
//   // Pattern=2 (Hexa-pattern)
//   float3 pos = SizeMode > 0.5 ? zeroAdjustedSize * (cell / clampedCount) - zeroAdjustedSize * (Pivot + 0.5)
//                               : zeroAdjustedSize * cell - zeroAdjustedSize * clampedCount * (Pivot + 0.5);
//   int hexAttrIndex = cell.x % 2 + ((cell.y + 3) % 6) * 2;
//   float2 offsetAndAngles = HexOffsetsAndAngles[hexAttrIndex];
//   pos.x += offsetAndAngles.x * zeroAdjustedSize.x * 0.3333;
//   const float HexScale = 0.578f;
//   pos.x *= HexScale * 3;
//   float rotDelta = (180 + offsetAndAngles.y) * ToRad;
//   pos += Center;
//   ResultPoints[index].Position = pos;
//   ResultPoints[index].W = W;
//   ResultPoints[index].Rotation = qFromAngleAxis(OrientationAngle*PI/180 + rotDelta, normalize(OrientationAxis));
//   ResultPoints[index].Color = 1;
//   ResultPoints[index].Selected = 1;
//   ResultPoints[index].Stretch = 1;
//
// HexOffsetsAndAngles table (TiXL lines 26-34, 12 float2 entries):
//   float2(-1,90), float2(0,30),  // 0
//   float2(0,150), float2(-1,-30), // 1
//   float2(-1,-150), float2(0,-90), // 2
//   float2(0,30), float2(-1,90),  // 3
//   float2(-1,-30), float2(0,150), // 4
//   float2(0,-90), float2(-1,-150) // 5
//
// NAMED FORKS:
//   Pattern baked to 2 (Hexa); Triangular and default (3) branches deferred.
//   Color baked to white (1,1,1,1) as TiXL does (ResultPoints[index].Color = 1).
//   Rotation set via Y·X·Z order (CreateFromYawPitchRoll = batch16/17 rule; here only
//   axis-angle is used, same as TiXL's qFromAngleAxis, so no reorder issue).
//   Scale=1 (Stretch=1 in TiXL, baked).
#include <metal_stdlib>
#include "tixl_point.h"           // SwPoint (64B)
#include "hexgridpoints_params.h" // HexGridParams, HexGridBinding
#include "shared/quat.metal.h"    // qFromAngleAxis
using namespace metal;

// TiXL HexOffsetsAndAngles table (12 float2: x=column_offset, y=rotation_angle_degrees)
constant float2 HexOffsetsAndAngles[12] = {
    float2(-1.0f,  90.0f), float2( 0.0f,  30.0f),   // 0
    float2( 0.0f, 150.0f), float2(-1.0f, -30.0f),   // 1
    float2(-1.0f,-150.0f), float2( 0.0f, -90.0f),   // 2
    float2( 0.0f,  30.0f), float2(-1.0f,  90.0f),   // 3
    float2(-1.0f, -30.0f), float2( 0.0f, 150.0f),   // 4
    float2( 0.0f, -90.0f), float2(-1.0f,-150.0f),   // 5
};

kernel void hexgridpoints(
    device SwPoint*           pts [[buffer(HEXGRID_Points)]],
    constant HexGridParams&   P   [[buffer(HEXGRID_Params)]],
    uint3                     tid [[thread_position_in_grid]])
{
    uint cx = max(P.CountX, 1u);
    uint cy = max(P.CountY, 1u);
    uint cz = max(P.CountZ, 1u);
    uint total = cx * cy * cz;
    uint index = tid.x;
    if (index >= total) return;

    // TiXL: cell coordinate
    uint3 cell = uint3(index % cx,
                       (index / cx) % cy,
                       (index / (cx * cy)) % cz);
    float3 cellf = float3(cell);

    // TiXL: clampedCount (singleton axis -> 1 to avoid division)
    float3 clampedCount = float3(cx == 1u ? 1.0f : float(cx - 1u),
                                 cy == 1u ? 1.0f : float(cy - 1u),
                                 cz == 1u ? 1.0f : float(cz - 1u));

    float3 size = float3(P.SizeX, P.SizeY, P.SizeZ);
    // TiXL: zeroAdjustedSize (singleton axis -> 0)
    float3 zeroAdjustedSize = float3(cx == 1u ? 0.0f : size.x,
                                     cy == 1u ? 0.0f : size.y,
                                     cz == 1u ? 0.0f : size.z);

    float3 pivot  = float3(P.PivotX,  P.PivotY,  P.PivotZ);
    float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);

    // TiXL Pattern=2 position (SizeMode branch)
    float3 pos;
    if (P.SizeMode > 0u) {
        // Bounds mode
        pos = zeroAdjustedSize * (cellf / clampedCount) - zeroAdjustedSize * (pivot + 0.5f);
    } else {
        // Cell mode
        pos = zeroAdjustedSize * cellf - zeroAdjustedSize * clampedCount * (pivot + 0.5f);
    }

    // TiXL hex offset: hexAttrIndex = cell.x % 2 + ((cell.y + 3) % 6) * 2
    uint hexAttrIndex = (cell.x % 2u) + ((cell.y + 3u) % 6u) * 2u;
    // clamp to table size (12 entries): should always be 0..11 given the formula, but guard
    hexAttrIndex = min(hexAttrIndex, 11u);
    float2 offsetAndAngles = HexOffsetsAndAngles[hexAttrIndex];

    // TiXL: pos.x += offsetAndAngles.x * zeroAdjustedSize.x * 0.3333
    pos.x += offsetAndAngles.x * zeroAdjustedSize.x * 0.3333f;

    // TiXL: pos.x *= HexScale * 3  (HexScale = 0.578)
    const float HexScale = 0.578f;
    pos.x *= HexScale * 3.0f;

    // TiXL: rotDelta = (180 + offsetAndAngles.y) * ToRad
    const float ToRad = M_PI_F / 180.0f;
    float rotDelta = (180.0f + offsetAndAngles.y) * ToRad;

    pos += center;

    // TiXL: Rotation = qFromAngleAxis(OrientationAngle*PI/180 + rotDelta, normalize(OrientationAxis))
    float3 axis   = float3(P.OrientAxisX, P.OrientAxisY, P.OrientAxisZ);
    float  axLen  = length(axis);
    float3 normAxis = (axLen > 1e-6f) ? (axis / axLen) : float3(0.0f, 1.0f, 0.0f);
    float  angle  = P.OrientAngle * (M_PI_F / 180.0f) + rotDelta;
    float4 rot    = qFromAngleAxis(angle, normAxis);

    SwPoint p;
    p.Position = pos;
    p.FX1      = P.W;                          // TiXL W port -> SwPoint.FX1
    p.Rotation = rot;
    p.Color    = float4(1.0f, 1.0f, 1.0f, 1.0f);  // TiXL: Color = 1 (white)
    p.Scale    = float3(1.0f);                 // TiXL: Stretch = 1 (baked)
    p.FX2      = 0.0f;
    pts[index] = p;
}
