// combinebuffers — the PROVING kernel for the _ExecuteCombineBuffers replay atom (187 量產第一波).
//
// A faithful MSL port of external/tixl .../Assets/shaders/points/combine/CombineBuffersAsInt.hlsl —
// the kernel the CombineBuffers.t3 selects (ComputeShader.Source =
// "Lib:shaders/points/combine/CombineBuffersAsInt.hlsl"). It copies one input Point bag into the
// combined result bag at a running StartIndex offset:
//
//   HLSL (CombineBuffersAsInt.hlsl:3-18):
//     cbuffer Params : register(b0) { int StartIndex; int Length; }
//     StructuredBuffer<Point> Points : t0;
//     RWStructuredBuffer<Point> ResultPoints : u0;
//     [numthreads(256,1,1)] void main(uint3 i) {
//         ResultPoints[StartIndex + i.x] = Points[i.x];   // whole-Point copy
//     }
//
// The .t3's _ExecuteCombineBuffers dispatches this ONCE PER INPUT BUFFER (external/tixl
// .../point/combine/_ExecuteCombineBuffers.cs:101-113): StartIndex = running total of prior inputs'
// counts, Length = this input's count, SRV = this input's Point buffer, UAV = the shared result buffer.
// The sw _ExecuteCombineBuffers atom replays that per-pass loop (buffer_ops_executecombinebuffers.cpp),
// dispatching this kernel per input with the pass's StartIndex bound.
//
// The .hlsl comments out the `if (i.x >= Length) return;` bound, so a pass is dispatched with exactly
// Length threads (ceil(Length/256) groups). sw dispatches ceil(Length/tg) groups and DOES bound-check on
// Length here (the last group's tail threads must not clobber a neighbouring input's slot in the result).
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
using namespace metal;

kernel void combinebuffers(
    const device SwPoint* Points       [[buffer(CS_SRV_BASE + 0)]],   // t0: this input's Point bag
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0: the shared combined result
    constant int*         cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: {StartIndex, Length}
    uint3 i [[thread_position_in_grid]]) {
  int StartIndex = cb0[0];
  int Length = cb0[1];
  if ((int)i.x >= Length) return;                 // guard the shared result (see header)
  ResultPoints[StartIndex + (int)i.x] = Points[i.x];
}
