// Shared host<->shader params for the TiXL-ported SnapToPoints COMBINE op (batch 21).
// Mirrors external/tixl .../point/transform/SnapToPoints.cs (slots) +
//         external/tixl .../Assets/shaders/points/modify/SnapToPoints.hlsl (math).
//
// SnapToPoints pairs each point in Points1 with the corresponding (same-index) point in
// Points2 and lerps toward it: blendFactor = smoothstep(BlendFactor+Distance, Distance,
// distance) * MaxAmount;  Position = lerp(A.Pos, SnapPos, blendFactor).
// W channel: lerp(A.W, SnapPoint.W, BlendFactor).  All other SwPoint fields carry from A.
//
// TiXL cbuffer (SnapToPoints.hlsl register b0):
//   float BlendFactor;   // smoothstep edge threshold  (default 0.0)
//   float Distance;      // snap radius                (default 1.0)
//   float MaxAmount;     // scale the blend factor     (default 1.0)
//
// NAMED FORKS:
//   count-guard: TiXL .hlsl assumes Points1 and Points2 are equal length (no OOB guard).
//     We clamp the Points2 index to (Points2Count-1) when i >= Points2Count.  When Points2
//     is empty (Points2Count==0) the point passes through unchanged (blendFactor=0).
//     See shader: // fork[count-guard]: TiXL .hlsl assumes equal length...
//
// 16-byte aligned; Count/Points2Count scalar pair + 3 floats + 1 pad = 32 bytes.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SnapToPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;         // Points1 count (output count)
  uint  Points2Count;  // Points2 count (may be < Count — triggers count-guard clamp)
  float BlendFactor;   // smoothstep lower edge
  float Distance;      // smoothstep upper edge
#else
  uint32_t Count;
  uint32_t Points2Count;
  float    BlendFactor;
  float    Distance;
#endif
  float MaxAmount;     // scale on blendFactor for Position snap
  float _pad0;
  float _pad1;
  float _pad2;         // -> 32 bytes total (2x16)
};

enum SnapToPointsBinding {
  SNAPTOPOINTS_Points1  = 0,  // const device SwPoint* (Points1 input)
  SNAPTOPOINTS_Points2  = 1,  // const device SwPoint* (Points2 input)
  SNAPTOPOINTS_Result   = 2,  // device SwPoint*       (output)
  SNAPTOPOINTS_Params   = 3,  // constant SnapToPointsParams&
};

#ifndef __METAL_VERSION__
// Count(4)+Points2Count(4)+BlendFactor(4)+Distance(4)=16 | MaxAmount(4)+3xpad(12)=16 = 32 bytes
static_assert(sizeof(SnapToPointsParams) == 32, "SnapToPointsParams must be 32 bytes");
#endif
