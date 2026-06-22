// Shared host<->shader params for the TiXL-ported TransformPointsFromClipspace — the FIRST Points op to
// consume the camera-matrix-into-points seam (PointCookCtx::cameraToWorld). Mirrors the cbuffers of
// external/tixl .../Assets/shaders/points/modify/TransformPointsFromClipspace.hlsl:
//
//   cbuffer Params   : register(b0) { }                  // EMPTY — the op has only the Points input
//   cbuffer Transforms: register(b1) { float4x4 ... }    // 10 matrices; the kernel reads ONLY CameraToWorld
//
// fork-camera-one-matrix-per-op (named): TiXL packs all 10 TransformBufferLayout matrices; this kernel
// touches CameraToWorld ONLY (unproject + the 3×3 → rotation). So our cbuffer carries that ONE matrix
// (the other 9 are dead for this op) plus Count (OUR addition — TiXL reads it via SourcePoints.GetDimensions;
// we pass it + guard tid>=Count, like every sibling point op). CameraToWorld is ROW-MAJOR float[16]; the
// MSL kernel rebuilds its float4x4 via mul4row so `mul(rowVec, M)` == `v·M_rowmajor` (the draw_quad_xf /
// field_raymarch convention). float[16] is 4-byte aligned and ends the struct (no packed_float3 after it),
// so the 16-byte float4x4 binding is naturally aligned — metal-cpp-discipline safe.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TpfcsParams {
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float _pad0, _pad1, _pad2;   // -> 16. align CameraToWorld to a 16-byte row boundary.
  float CameraToWorld[16];     // row-major (m[r*4+c]); the ONLY matrix this kernel reads.
};

enum TpfcsBinding {
  TPFCS_SourcePoints = 0,  // const device SwPoint* SourcePoints (t0 in HLSL; buffer(0) in MSL)
  TPFCS_ResultPoints = 1,  // device SwPoint* ResultPoints       (u0 in HLSL; buffer(1) in MSL)
  TPFCS_Params       = 2,  // constant TpfcsParams&              (b0/b1 folded; buffer(2) in MSL)
};

#ifndef __METAL_VERSION__
// 16 (Count+pad) + 64 (float[16]) = 80 bytes.
static_assert(sizeof(TpfcsParams) == 80, "TpfcsParams must be 80 bytes (Count+pad row + 4x16 matrix)");
#endif
