// Shared host<->shader params for the TiXL-ported PairPointsForLines COMBINE op (batch 24).
// Mirrors external/tixl .../point/combine/PairPointsForLines.cs (slots) +
//         external/tixl .../Assets/shaders/points/combine/PairPointsForLines.hlsl (math).
//
// PairPointsForLines pairs each GPoints[i] with GTargets[i] (both cycled via modulo), emitting
// 3 output points per pair: [A, B, NaN divider]. ResultCount = PairCount = max(CountA, CountB).
// Output count = ResultCount * 3.
//
// TiXL cbuffer (PairPointsForLines.hlsl register b0):
//   float CountA;       // GPoints count
//   float CountB;       // GTargets count
//   float ResultCount;  // pair count = max(CountA, CountB); output = ResultCount * 3
//   float InitWTo01;    // 1.0 -> set FX1=0 on A-element, FX1=1 on B-element
//
// NAMED FORKS:
//   fork[resultcount]: TiXL's driver sets ResultCount = max(CountA, CountB) externally (C# side).
//     We compute ResultCount = max(CountA, CountB) in the cook fn and pass it to the shader.
//
// 16-byte aligned; 4 floats = 16 bytes.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PairPointsForLinesParams {
#ifdef __METAL_VERSION__
  float CountA;       // GPoints count
  float CountB;       // GTargets count
  float ResultCount;  // pair count = max(CountA, CountB)
  float InitWTo01;    // >0.5 -> set FX1 to 0/1 on A/B elements
#else
  float CountA;
  float CountB;
  float ResultCount;
  float InitWTo01;
#endif
};  // 16 bytes, naturally 16-byte aligned

enum PairPointsForLinesBinding {
  PAIRPOINTSFORLINES_GPoints    = 0,  // const device SwPoint* (GPoints input)
  PAIRPOINTSFORLINES_GTargets   = 1,  // const device SwPoint* (GTargets input)
  PAIRPOINTSFORLINES_Result     = 2,  // device SwPoint*       (output)
  PAIRPOINTSFORLINES_Params     = 3,  // constant PairPointsForLinesParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PairPointsForLinesParams) == 16, "PairPointsForLinesParams must be 16 bytes");
#endif
