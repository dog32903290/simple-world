// Shared host<->shader params for the TiXL-ported PairPointsForSplines COMBINE op (point_combine).
// Mirrors external/tixl .../point/combine/PairPointsForSplines.cs (slots) +
//         external/tixl .../Assets/shaders/points/combine/PairPointsForSplines.hlsl (math).
//
// PairPointsForSplines pairs each GPoints[i] with GTargets[i] (both cycled via modulo) and, for
// each pair, emits a Hermite spline strip of `SegmentCount` output points: indexInLine in
// [0, SegmentCount-2] interpolate the cubic, the LAST point (indexInLine==SegmentCount-1) is a
// NaN divider (Scale=NaN). ResultCount = max(CountA,CountB) * SegmentCount.
//
// TiXL cbuffer (PairPointsForSplines.hlsl register b0) — EXACT field order (traced via the .t3
// FloatsToBuffer connection order, which is 1:1 with the cbuffer; only intermediate math is the
// Segments routing captured in SegmentCount below):
//   float3 TangentDirection;   // default (0,0,1)
//   float  InitWTo01;          // SetWTo01 bool -> 0/1; >0.5 => FX1 = f (spline param)
//   float  SegmentCount;       // = clamp(Segments,3,16385) + 1  (points-per-pair incl. NaN divider)
//   float  TangentA;           // default 1
//   float  TangentA_WFactor;   // default 0
//   float  TangentB;           // default 1
//   float  TangentB_WFactor;   // default 0
//   float  Debug;              // default 0 (unused by the math)
//
// COUNT POLICY (.t3 MaxInt -> MultiplyInt(B = ClampInt(Segments,3,16385)+1) -> CalcDispatchCount):
//   ResultCount = max(CountA, CountB) * (clamp(Segments,3,16385) + 1)
//   We size SegmentCount = clamp(Segments,3,16385)+1 host-side and pass it; the buffer length is
//   computed in the cook fn and surfaced via the PointCountFn static (same pattern as
//   PairPointsForLines).
//
// 9 floats; padded to 16-byte multiple (48 bytes) so MSL constant alignment is exact.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PairPointsForSplinesParams {
#ifdef __METAL_VERSION__
  packed_float3 TangentDirection;  // @0  (12B)
  float InitWTo01;                 // @12
  float SegmentCount;              // @16
  float TangentA;                  // @20
  float TangentA_WFactor;          // @24
  float TangentB;                  // @28
  float TangentB_WFactor;          // @32
  float Debug;                     // @36
  // HLSL reads CountA/CountB/ResultCount via StructuredBuffer.GetDimensions; Metal has no buffer
  // length query, so the host passes them explicitly (mirrors PairPointsForLines).
  float CountA;                    // @40
  float CountB;                    // @44
  float ResultCount;               // @48  (max(A,B)*SegmentCount = output buffer length)
  float _pad0;                     // @52
  float _pad1;                     // @56
  float _pad2;                     // @60
#else
  float TangentDirectionX;
  float TangentDirectionY;
  float TangentDirectionZ;
  float InitWTo01;
  float SegmentCount;
  float TangentA;
  float TangentA_WFactor;
  float TangentB;
  float TangentB_WFactor;
  float Debug;
  float CountA;
  float CountB;
  float ResultCount;
  float _pad0;
  float _pad1;
  float _pad2;
#endif
};  // 64 bytes (16 floats), naturally 16-byte aligned

enum PairPointsForSplinesBinding {
  PAIRPOINTSFORSPLINES_GPoints  = 0,  // const device SwPoint* (GPoints / PointsA input)
  PAIRPOINTSFORSPLINES_GTargets = 1,  // const device SwPoint* (GTargets / PointsB input)
  PAIRPOINTSFORSPLINES_Result   = 2,  // device SwPoint*       (output)
  PAIRPOINTSFORSPLINES_Params   = 3,  // constant PairPointsForSplinesParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PairPointsForSplinesParams) == 64,
              "PairPointsForSplinesParams must be 64 bytes (16 floats)");
#endif
