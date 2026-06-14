// Shared host<->shader params for the TiXL-ported OffsetPoints MODIFIER (batch 24).
// Mirrors external/tixl/Operators/Lib/point/_internal/_OffsetPoints.cs (.cs ports, lines 10-17) +
// .../Assets/shaders/points/modify/OffsetPoints.hlsl (.hlsl math, lines 30-45).
//
// OffsetPoints is the cleanest count-preserving modifier — no simplification:
//   each point: Position += qRotateVec3(Direction * Distance, Point.Rotation)  (.hlsl line 40)
//   Rotation / Color / Selected(W via FX) / W preserved verbatim (.hlsl lines 41-44).
// The offset is rotated by the POINT's own rotation (the .hlsl uses the point's existing
// Rotation quaternion — NO new rotation is constructed), so a "forward" Direction offsets each
// point along its own facing direction.
//
// TiXL .cs ports (line + GUID in comment): Points(buffer) / Direction(Vector3) / Distance(float).
// No defaults are encoded in the .cs InputSlots (TiXL InputSlot<T> defaults to T's zero); the
// .t3 default values are Direction=(0,0,1)? — NOT trusted blind. We use the safe zero/neutral
// defaults that make the node a no-op until the user dials it (Direction=(0,0,1), Distance=0):
//   Distance=0 -> offset magnitude 0 -> identity passthrough (matches "no-op until dialed").
//
// All Vector3 inputs become X/Y/Z scalars; no packed_float3 trap. 16-byte aligned.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct OffsetPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;   // inherited from input bag (modifier: count from upstream Points)
  float _pad0;
  float _pad1;
  float _pad2;   // -> 16 bytes
#else
  uint32_t Count;
  float    _pad0;
  float    _pad1;
  float    _pad2;
#endif
  float DirectionX, DirectionY, DirectionZ;  // .cs Direction (Vector3)
  float Distance;                            // .cs Distance (float) — scales the offset; 0 = no-op
};                                           // -> 16 bytes (Direction12 + Distance4)

enum OffsetPointsBinding {
  OFFSETPOINTS_SourcePoints = 0,  // const device SwPoint* (t0)
  OFFSETPOINTS_ResultPoints = 1,  // device SwPoint*       (u0)
  OFFSETPOINTS_Params       = 2,  // constant OffsetPointsParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+3xpad(12)=16 | Direction(12)+Distance(4)=16 = 32 bytes
static_assert(sizeof(OffsetPointsParams) == 32, "OffsetPointsParams must be 32 bytes (2x16)");
#endif
