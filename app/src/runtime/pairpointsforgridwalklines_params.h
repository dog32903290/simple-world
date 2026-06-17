// Shared host<->shader params for the TiXL-ported PairPointsForGridWalkLines COMBINE op.
// Mirrors external/tixl .../point/combine/PairPointsForGridWalkLines.cs (slots) +
//         external/tixl .../Assets/shaders/points/combine/PairPointsForGridWalkLine.hlsl (math).
//
// PairPointsForGridWalkLines connects each StartPoints[i] to TargetPoints[i] (both cycled via
// modulo) with an 11-step "grid walk" polyline that hops along grid axes. Each pair (line) emits
// exactly 11 output points (stepsPerPair = numthreads(11)). The 11th step (lineStepIndex==10) is
// the NaN divider that separates lines for DrawLines.
//
// Count policy: ResultCount(lines) = max(StartCount, TargetCount); output = ResultCount * 11.
//   (TiXL: MaxInt(StartCount,TargetCount) -> MultiplyInt(B=11) -> buffer length + dispatch count.)
//
// cbuffer routing (.t3 BACKWARD-TRACE, Cut-58 lesson): the compound feeds the shader cbuffer via a
// FloatsToBuffer whose connection order is 1:1 with the cbuffer struct below — Vector3Components
// splits GridSize/GridOffset/RandomizeGrid into .x/.y/.z, each vector followed by one padding Value,
// then the three scalars StrokeLength/Speed/PhaseOffset. NO intermediate math nodes on these params
// (the only math nodes — MaxInt/MultiplyInt — drive COUNT, not the cbuffer). So the mapping is
// direct: op param -> cbuffer field, verbatim defaults from the .t3.
//
// TiXL cbuffer (PairPointsForGridWalkLine.hlsl register b0), HLSL float3 = 16-byte slot + pad:
//   float3 GridSize;       float _padding1;   // .t3 default (0.25, 0.25, 0.25)
//   float3 GridOffset;     float _padding3;   // (0,0,0)
//   float3 RandomizeGrid;  float _padding4;   // (0,0,0)
//   float  StrokeLength;   float Speed;   float PhaseOffset;  // 2.0 / 0.5 / 0.0
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// 16-byte aligned to match the HLSL cbuffer's float3+pad slots exactly (host setBytes -> constant&).
// 3 vec4 slots (48) + 3 trailing scalars (12) + 1 tail pad (4) = 64 bytes.
struct PairPointsForGridWalkLinesParams {
  float GridSizeX, GridSizeY, GridSizeZ;     // GridSize.xyz
  float _padding1;                            // HLSL _padding1 (float3 -> 16-byte slot)
  float GridOffsetX, GridOffsetY, GridOffsetZ;
  float _padding3;
  float RandomizeGridX, RandomizeGridY, RandomizeGridZ;
  float _padding4;
  float StrokeLength;
  float Speed;
  float PhaseOffset;
  float _padTail;                             // pad struct to 64 bytes (16-byte multiple)
};

enum PairPointsForGridWalkLinesBinding {
  PAIRGRIDWALK_StartPoints  = 0,  // const device SwPoint* (StartPoints input -> t0)
  PAIRGRIDWALK_TargetPoints = 1,  // const device SwPoint* (TargetPoints input -> t1)
  PAIRGRIDWALK_Result       = 2,  // device SwPoint*       (output -> u0)
  PAIRGRIDWALK_Params       = 3,  // constant PairPointsForGridWalkLinesParams&
  PAIRGRIDWALK_Counts       = 4,  // constant uint3 (totalCount, countA, countB) — replaces
                                  // HLSL GetDimensions() (MSL has no StructuredBuffer dims).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PairPointsForGridWalkLinesParams) == 64,
              "PairPointsForGridWalkLinesParams must be 64 bytes (matches HLSL cbuffer slots)");
#endif
