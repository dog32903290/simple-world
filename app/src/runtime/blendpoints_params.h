// Shared host<->shader params for the TiXL-ported BlendPoints COMBINE op (point_combine family).
// Mirrors external/tixl .../point/combine/BlendPoints.cs (slots) +
//         external/tixl .../Assets/shaders/points/combine/BlendPoints.hlsl (math) +
//         external/tixl .../point/combine/BlendPoints.t3 (FloatsToBuffer routing).
//
// BlendPoints lerps PointsA[i] toward PointsB[i] (index-paired, NOT modulo-wrapped) by a
// per-point blend factor f, lerping Position/Rotation(qSlerp)/Color/Scale/FX1/FX2.  The
// output count = countA (the FIRST input), NOT the sum (unlike CombineBuffers).
//
// TiXL cbuffer (BlendPoints.hlsl register b0, EXACT order):
//   float BlendFactor;   // base blend amount                       (default 0.5)
//   float BlendMode;     // 0 Mix, 1 UseA_F1, 2 UseB_F1, 3 Ranged, 4 RangedSmooth (default 0)
//   float PairingMode;   // 0 WrapAround, 1 Adjust (count-mismatch thinning)        (default 0)
//   float Width;         // <- RangeWidth slot; range falloff width                 (default 0.5)
//   float Scatter;       // random jitter on f, scaled by center falloff           (default 0.0)
//
// .t3 ROUTING (BACKWARD-TRACED per Cut-58 lesson): all 5 cbuffer fields reach the kernel via
// the FloatsToBuffer multi-input slot 49556d12 in this CONNECTION order, which is EXACTLY the
// cbuffer order — direct 1:1, no intermediate math nodes (the two IntToFloat nodes are pure
// enum->float casts for BlendMode/Pairing, value-preserving):
//   1) BlendFactor (op input, direct)
//   2) BlendMode   (op input -> IntToFloat 2eb9e20b)
//   3) Pairing     (op input -> IntToFloat ec56cf28)  -> PairingMode
//   4) RangeWidth  (op input, direct)                 -> Width
//   5) Scatter     (op input, direct)
//
// COUNT POLICY (.t3): the output StructuredBufferWithViews and CalcDispatchCount are both sized
// from GetSRVProperties of PointsA (be16bd14 <- PointsA). => output count = countA. Locked via
// the driver's countFromFirstPointsInput=true (SnapToPoints proves this seam).
//
// NAMED FORKS:
//   fork[b-count-guard]: TiXL .hlsl reads PointsB[i.x] with no bounds check (HLSL OOB
//     StructuredBuffer reads return 0). To avoid Metal OOB we clamp the B index to
//     (CountB-1) and, when CountB==0, treat B as a zero point (matches HLSL's zero-read).
//   fork[t-singular]: TiXL computes t = i.x/(resultCount-1); when resultCount==1 this divides
//     by zero (HLSL -> +inf/NaN, saturated away in the Ranged modes). We reproduce the exact
//     float division (no special-casing) so byte-parity holds.
//
// 16-byte aligned: 5 floats + 3 pad = 32 bytes.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct BlendPointsParams {
  // --- TiXL cbuffer fields (BlendPoints.hlsl b0, EXACT order) ---
  float BlendFactor;   // base blend amount
  float BlendMode;     // 0..4 (Mix / UseA_F1 / UseB_F1 / Ranged / RangedSmooth)
  float PairingMode;   // 0 WrapAround, 1 Adjust
  float Width;         // RangeWidth (range falloff width)
  float Scatter;       // random jitter scaled by center falloff
  // --- host-supplied buffer dimensions (HLSL derives these from GetDimensions; we pass them
  //     explicitly because Metal buffer length is not queryable inside the kernel like HLSL's
  //     StructuredBuffer.GetDimensions). Values come from c.inputCounts in the cook fn. ---
  uint  CountA;        // PointsA length == ResultCount (output count)
  uint  CountB;        // PointsB length (may differ; drives Adjust thinning + B clamp)
  float _pad0;         // -> 32 bytes total (2x16)
};

enum BlendPointsBinding {
  BLENDPOINTS_PointsA = 0,  // const device SwPoint* (PointsA input — drives output count)
  BLENDPOINTS_PointsB = 1,  // const device SwPoint* (PointsB input — blend target)
  BLENDPOINTS_Result  = 2,  // device SwPoint*       (output)
  BLENDPOINTS_Params  = 3,  // constant BlendPointsParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BlendPointsParams) == 32, "BlendPointsParams must be 32 bytes");
#endif
