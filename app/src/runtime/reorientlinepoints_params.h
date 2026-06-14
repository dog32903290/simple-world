// Shared host<->shader params for the TiXL-ported ReorientLinePoints MODIFIER (lane point_modify).
// Mirrors external/tixl .../point/transform/ReorientLinePoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/ReorientLinePoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: re-orients each point's Rotation so its +Z forward follows the
// local LINE TANGENT (direction from the previous live neighbour to the next live neighbour),
// blended by Amount via qSlerp. Count is INHERITED from upstream.
//
// TiXL inputs (ReorientLinePoints.cs — exactly 5 scalar inputs + 1 bag):
//   Amount    (float, default 1.0) — slerp blend toward the aligned rotation
//   Center    (Vector3)            — DECLARED but UNUSED by the .hlsl kernel (no read).
//   UpVector  (Vector3)            — DECLARED but UNUSED by the .hlsl kernel (no read).
//   WIsWeight (bool, → UseWAsWeight cbuffer field) — DECLARED but UNUSED by the .hlsl kernel.
//   Flip      (bool)               — DECLARED but UNUSED by the .hlsl kernel.
// The .hlsl cbuffer carries Center/UpVector/UseWAsWeight/Flip but main() reads ONLY Amount.
// We keep Amount as the single live param and DROP the dead ports (see NAMED FORK note in .cpp):
//   porting unused inputs as knobs would be inventing operable controls TiXL itself ignores.
//
// TiXL cbuffer (ReorientLinePoints.hlsl:6-13): { float3 Center; float Amount; float3 UpVector;
//   float UseWAsWeight; float Flip; }  — only Amount affects output.
//
// Flattened to single struct (16-byte rows; static_assert).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ReorientLineParams {
#ifdef __METAL_VERSION__
  uint  Count;   // inherited from input bag (count-preserving modifier)
  float Amount;  // slerp blend toward tangent-aligned rotation (TiXL Amount)
  float _pad0;
  float _pad1;   // -> 16 bytes
#else
  uint32_t Count;
  float    Amount;
  float    _pad0;
  float    _pad1;
#endif
};

enum ReorientLineBinding {
  REORIENTLINE_SourcePoints = 0,  // const device SwPoint* (t0)
  REORIENTLINE_ResultPoints = 1,  // device SwPoint*       (u0)
  REORIENTLINE_Params       = 2,  // constant ReorientLineParams& (b0)
};

#ifndef __METAL_VERSION__
// Count+Amount+2xpad = 16 bytes
static_assert(sizeof(ReorientLineParams) == 16, "ReorientLineParams must be 16 bytes");
#endif
