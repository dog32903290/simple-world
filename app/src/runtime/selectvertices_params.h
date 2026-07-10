// Host<->shader params for TiXL mesh/modify/mesh-SelectVertices.hlsl (owner op: SelectVertices) —
// mathv transpiler-batch kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-3). NOT wired
// to node_registry / t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a
// separate future lane. Owner: external/tixl/Operators/Lib/mesh/modify/SelectVertices.t3.
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/mesh-SelectVertices.hlsl's cbuffer Params (:8-18)
// field for field. No packed_float3 padding traps here (unlike DeformMesh's Pivot/TwistPivot): the 8
// trailing scalars after TransformVolume pack into exactly 2 full 16-byte cbuffer slots (32 bytes, no
// straddle) — confirmed by the raw transpile output having zero `_padding` fields for this struct.
//
// MATRIX CONVENTION: same as transformmeshuvs_params.h (row-major m[0..15], translation in column
// 3/7/11, `M·v` semantics) — see that header's full ALGEBRA derivation, reused verbatim here since
// both kernels use the identical `v * float4x4` raw-transpile shape.
//
// `mod(x,y)` in the HLSL (VolumeZebra branch) is NOT an HLSL builtin — glslang's HLSL frontend
// resolves it via a GLSL-heritage extension to FLOORED modulo (`x - y*floor(x/y)`), confirmed by
// reading the raw transpile output (`(_716.y+Phase) - 2*floor((_716.y+Phase)*0.5)`, exactly matching
// this codebase's own `shared/point-legacy.hlsl:32` `#define mod(x,y) ((x)-(y)*floor((x)/(y)))` macro
// — no controversy, both agree on floored semantics).
//
// `snoise` (VolumeNoise branch) is TiXL's Ashima 3-D simplex noise (shared/noise-functions.hlsl) —
// this op's CPU ref REUSES app/src/tixl_noise_oracle.h::snoise (the established shared P5-safe oracle,
// same one AddNoise's ref uses, §1.4 "大依賴閉包...獨立成共享 oracle header 一份多 op 引用"), not a
// fresh re-transcription.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SelectVerticesParams {
#ifdef __METAL_VERSION__
  float4x4 TransformVolume;  // row-major m[0..15] memcpy'd verbatim -- see header MATRIX CONVENTION
#else
  float m[16];
#endif
  float FallOff;
  float VolumeShape;   // thresholds: <0.5 Sphere, <1.5 Box, <2.5 Plane, <3.5 Zebra, <4.5 Noise, else 1.0
  float SelectMode;    // thresholds: <0.5 Override, <1.5 Add, <2.5 Sub, <3.5 Multiply(lerp), <4.5 Invert, else passthrough
  float ClampResult;   // <0.5 -> raw s; else saturate(s)
  float Strength;
  float Phase;
  float Threshold;
  float UseVertexSelection;  // NOTE: declared in the HLSL cbuffer but NEVER READ by main() -- dead field,
                              // see selectvertices.metal header (kernel always uses SourceVertices[i].Selected
                              // directly, never gates on UseVertexSelection like DeformMesh/TransformMeshUVs do)
#ifdef __METAL_VERSION__
  uint Count;   // host-ABI (replaces SourceVertices.GetDimensions, §10.5①)
#else
  uint32_t Count;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③).
enum SelectVerticesBinding {
  SELECTV_SourceVertices = 0,  // const device SwVertex* (t0)
  SELECTV_ResultVertices = 1,  // device SwVertex*       (u0)
  SELECTV_Params         = 2,  // constant SelectVerticesParams& (b0, extended with host-ABI Count)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SelectVerticesParams) == 100,
              "SelectVerticesParams must be 100 bytes (64 matrix + 8*4 scalars + 4 Count)");
#endif
