// pointtrail_params.h — host/GPU shared cbuffer layout for PointTrail (3-pass Clear/Collect/Copy).
// Reference: external/tixl .../Assets/shaders/points/sim/PointTrail-{Clear,Collect,Copy}.hlsl
//   Clear   : no params (NaNs the whole output ring)
//   Collect : cbuffer Params2 b0 { int CycleIndex; int TrailLength; int PointCount; int BufferLength;
//                                   int WriteOrderTo; int WriteLineSeperators; }
//   Copy    : cbuffer Params  b0 { int CycleIndex; int TrailLength; int PointCount; int BufferLength;
//                                   int WriteOrderTo; int WriteLineSeperators; }
//
// PointTrail keeps a PERSISTENT CyclePoints ring (cross-frame state) and emits a per-frame TrailPoints
// output: Collect writes the source bag into the ring at the cross-frame head; Copy reads the ring
// NEWEST-FIRST into the output (targetIndex = BufferLength - i - 1), writes the fade f into the
// WriteOrderTo channel, and stamps NaN line separators. TrailLength here == ringPerPoint (userTrailLength
// +1, the .t3 AddInts(+1)). BufferLength = PointCount * TrailLength.
//
// metal-cpp-discipline: scalar ints only (4-byte each), no float3 → no packed trap. static_assert pins size.
#pragma once
#ifndef __METAL_VERSION__
  #include <cstdint>
#endif

// Buffer binding indices (shared by all three passes; each pass binds the subset it uses).
enum {
  POINTTRAIL_SourcePoints = 0,  // const device SwPoint* (Collect input / source bag)
  POINTTRAIL_CyclePoints  = 1,  // device/const SwPoint* (persistent ring — Collect writes, Copy reads)
  POINTTRAIL_TrailPoints  = 2,  // device SwPoint* (per-frame output — Clear/Copy write)
  POINTTRAIL_Params       = 3,  // PointTrailParams
};

struct PointTrailParams {
  int CycleIndex;           // ring write head (== FrameCount, +1 per enabled frame)
  int TrailLength;          // ring slots per point (== ringPerPoint = userTrailLength+1)
  int PointCount;           // source point count
  int BufferLength;         // PointCount * TrailLength
  int WriteOrderTo;         // fade target: 0 None / 1 FX1 / 2 FX2 / 3 Scale
  int WriteLineSeperators;  // 1 = write NaN line separators (Copy)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PointTrailParams) == 24, "PointTrailParams: 6 x 4-byte ints");
#endif
