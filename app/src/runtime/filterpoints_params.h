// Shared host<->shader params for the TiXL-ported FilterPoints op (lane A, batch 15).
// Mirrors external/tixl .../Assets/shaders/points/generate/FilterPoints.hlsl.
//
// FilterPoints is NOT a count-preserving modifier — it changes the output count to `Count`.
// TiXL cbuffer layout (two registers b0/b1):
//   b0 (floats): Scatter, StepSize
//   b1 (ints):   SourceCount, ResultCount, StartIndex, Seed
//
// The output buffer is pre-sized to ResultCount by PointGraph (it sees the "Count" Float port).
// Shader does: ResultPoints[i] = SourcePoints[imod2(StartIndex + (i*StepSize) + scatterOffset,
//              SourceCount)] where scatterOffset = Scatter>0.001 ?
//              SourceCount*Scatter*hash11u(i+Seed*SourceCount+StartIndex) : 0
//
// All flattened to avoid packed_float3 / alignment traps. 16-byte multiple, static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct FilterPointsParams {
  // b0 floats
  float Scatter;    // ScatterSelect port, default 0.0
  float StepSize;   // Step port, default 1.0
  float _pad0;
  float _pad1;
  // b1 ints
#ifdef __METAL_VERSION__
  int SourceCount;  // upstream input bag count
  int ResultCount;  // output buffer count (the Count port)
  int StartIndex;   // StartIndex port, default 0
  int Seed;         // Seed port, default 0
#else
  int32_t SourceCount;
  int32_t ResultCount;
  int32_t StartIndex;
  int32_t Seed;
#endif
};

enum FilterPointsBinding {
  FILTERPOINTS_SourcePoints = 0,  // const device SwPoint* (t0)
  FILTERPOINTS_ResultPoints = 1,  // device SwPoint*       (u0)
  FILTERPOINTS_Params       = 2,  // constant FilterPointsParams& (b0)
};

#ifndef __METAL_VERSION__
// floats(8) + ints(16) = 32 bytes = 2 x 16
static_assert(sizeof(FilterPointsParams) == 32, "FilterPointsParams must be 32 bytes");
#endif
