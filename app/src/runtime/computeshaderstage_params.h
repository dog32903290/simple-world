// Shared host<->shader binding indices for the GENERIC ComputeShaderStage seam (compute-stage
// keystone). Unlike transformpoints_params.h (a fused scalar TransformParams struct), this seam is
// GENERIC: the ComputeShaderStage atom binds arbitrary wired buffers to a named MSL kernel exactly
// as TiXL's ComputeShaderStage.Update() binds SRVs (t0..), UAVs (u0..) and constant buffers (b0..)
// then Dispatches (external/tixl/Operators/TypeOperators/Gfx/ComputeShaderStage.cs:58-107).
//
// The Metal binding-slot layout mirrors the HLSL register model the ported .hlsl kernels already use:
//   b0.. = constant buffers  (ComputeShaderStage.ConstantBuffers, in wire order)
//   t0.. = shader resources  (ComputeShaderStage.ShaderResources / SRVs, in wire order)
//   u0.. = unordered access   (ComputeShaderStage.Uavs, in wire order)
// A proving kernel (computeshaderstage_transformpoints.metal) declares matching [[buffer(n)]] slots.
// These are BYTE offsets into ONE flat Metal argument table; the ranges below keep SRV/UAV/CB from
// colliding while staying faithful to the b#/t#/u# grouping (Metal has a single buffer index space,
// so we partition it — fork `computestage-metal-flat-buffer-index-partition`).
#pragma once

#ifndef __METAL_VERSION__
  #include <cstdint>
#endif

// Flat Metal buffer-index partition for a generic compute stage. Each range is [base, base+span).
// A kernel binds ConstantBuffer i at CS_CB_BASE+i, SRV i at CS_SRV_BASE+i, UAV i at CS_UAV_BASE+i.
enum ComputeStageBinding {
  CS_CB_BASE  = 0,   // constant buffers b0.. (up to 4)
  CS_SRV_BASE = 4,   // shader resources  t0.. (up to 8)
  CS_UAV_BASE = 12,  // unordered access   u0.. (up to 4)
  CS_MAX_CB   = 4,
  CS_MAX_SRV  = 8,
  CS_MAX_UAV  = 4,
};
