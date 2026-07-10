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
  // AUX (backward-compatible, computestage-per-srv-elementcount): a uint[CS_MAX_SRV] array of every
  // wired SRV's elementCount, in t# order. numStructs (CS_CB_BASE+3) still carries the FRONT SRV's count
  // for existing kernels; a kernel that needs a NON-front SRV's length (e.g. dual-SRV BlendPoints reading
  // countB = srvCounts[1] for its Adjust-thinning / B-zero-fill) binds this array here. Existing kernels
  // that never declare buffer(CS_SRVCOUNT_BASE) are unaffected (Metal ignores the extra bound buffer).
  CS_SRVCOUNT_BASE = 16,
};

// TEXTURE-SRV index partition (TEXTURE_COMPUTE_SEAM_SPEC.md stage 3 — buffer-out stage reads a Texture2D
// SRV). Metal textures ride their OWN [[texture(n)]] index space and samplers their OWN [[sampler(n)]]
// space, BOTH independent of the flat buffer-index space above (SPEC §2 "Metal 貼圖走獨立 texture 空間，與
// buffer-index 不相撞 — 天生留白"). DX11 packs buffer-SRV(t0) + texture-SRV(t1) into ONE t# register space;
// Metal splits them, so the transpiled MSL assigns the texture-SRVs their own texture(0).. indices while
// the buffer-SRVs keep CS_SRV_BASE.. — only each currency's INTERNAL order must be preserved (SPEC §6 risk
// 3). A texture-consuming buffer kernel binds ShaderResourceTexture i at CS_TEX_SRV_BASE+i and the default
// linear-clamp sampler at CS_SAMPLER_BASE (fork computestage-default-sampler).
enum ComputeStageTextureBinding {
  CS_TEX_SRV_BASE = 0,  // texture(0).. for ShaderResourceTextures, in wire order (Metal texture space)
  CS_MAX_TEX_SRV  = 4,
  CS_SAMPLER_BASE = 0,  // sampler(0) = the stage's default linear-clamp MTLSamplerState
};
