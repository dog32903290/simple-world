// Shared host<->shader params for the TiXL-ported SamplePointsByCameraDistance — the SECOND camera-matrix-
// into-points seam consumer (PointCookCtx::objectToCamera) AND a rider of the bake-into-point seam (the
// WForDistance Curve baked to a 256×1 R32 scratch @t1). Mirrors the cbuffers of external/tixl
// .../Assets/shaders/points/modify/SamplePointsByCameraDistance.hlsl:
//
//   cbuffer Transforms: register(b0) { float4x4 ... }   // 10 matrices; the kernel reads ONLY ObjectToCamera
//   cbuffer Params    : register(b1) { float NearRange; float FarRange; }
//   Texture2D inputTexture : t1; sampler texSampler : s0;   // the baked WForDistance curve
//
// fork-camera-one-matrix-per-op (named): this kernel touches ObjectToCamera ONLY (the depth .z). So our
// cbuffer carries that ONE matrix (the other 9 are dead) + NearRange/FarRange + Count (OUR addition — TiXL
// reads it via the buffer dims; we pass it + guard tid>=Count). ObjectToCamera is ROW-MAJOR float[16]; the
// MSL kernel rebuilds its float4x4 via mul4row so `mul(rowVec,M)` == `v·M_rowmajor` (the draw_quad_xf /
// field_raymarch convention). float[16] is 4-byte aligned and ends the struct (no packed_float3 after) →
// the 16-byte float4x4 binding is naturally aligned, metal-cpp-discipline safe.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SpcdParams {
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float NearRange, FarRange, _pad0;  // -> 16. depth window + pad to align ObjectToCamera.
  float ObjectToCamera[16];          // row-major (m[r*4+c]); the ONLY matrix this kernel reads (.z depth).
};

enum SpcdBinding {
  SPCD_SourcePoints = 0,  // const device SwPoint* SourcePoints (t0 in HLSL; buffer(0) in MSL)
  SPCD_ResultPoints = 1,  // device SwPoint* ResultPoints       (u0 in HLSL; buffer(1) in MSL)
  SPCD_Params       = 2,  // constant SpcdParams&               (b0/b1 folded; buffer(2) in MSL)
};
// Texture + sampler bind slots (separate spaces from buffers).
enum SpcdTexBinding {
  SPCD_CurveTex = 0,  // Texture2D<float4> inputTexture (the baked WForDistance curve; t1 → texture(0))
};
enum SpcdSamplerBinding {
  SPCD_TexSampler = 0,  // sampler texSampler (s0) — Clamp + Linear
};

#ifndef __METAL_VERSION__
// 16 (Count+NearRange+FarRange+pad) + 64 (float[16]) = 80 bytes.
static_assert(sizeof(SpcdParams) == 80, "SpcdParams must be 80 bytes (params row + 4x16 matrix)");
#endif
