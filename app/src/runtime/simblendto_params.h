// Host<->shader params for TiXL SimBlendTo — mathv transpiler-batch kernel INVENTORY (2026-07-10,
// MATH_VERIFY_WORKFLOW.md §10). This is NOT wired to node_registry / kernelNameFor / t3_import — it is
// a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/points/combine/SimBlendTo.hlsl cbuffer Params : register(b0).
// SimBlendTo blends ResultPoints (IN-PLACE UAV, also read as the "A" side) toward PointsB (SRV, the
// "B" side) by BlendFactor. Count-preserving (same index i on both buffers, dispatch = min buffer size).
//
// KNOWN TiXL SHIPPED BUG, FAITHFULLY PRESERVED (transpiler-first discipline, MATH_VERIFY_WORKFLOW.md
// §10.3 — this is sw's transcription TARGET, not a fork to guard against): SimBlendTo.hlsl:30-31 reads
//   float wA = ResultPoints[i.x].W;
//   float wB = ResultPoints[i.x].W;   // NOT PointsB[i.x].W — hand-slip, PointsB.W is never consumed
// so W_out == W_in always (lerp(wA,wB,t) collapses to wA since wA==wB bit-for-bit). Both the transpiled
// kernel (app/shaders/simblendto.metal) and the CPU ref (mathv_ref_simblendto.h) reproduce this exactly.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimBlendToParams {
  float BlendFactor;     // .cs BlendFactor, SimBlendTo.t3ui Min=-5.0 Max=5.0, Default=0.0
  float PairingMethod;   // declared in the TiXL cbuffer but NEVER READ by main() — dead field, kept
                          // for ABI-mirroring completeness only (SimBlendTo.hlsl never touches it)
  float CountA;           // ditto — unused by the kernel body
  float CountB;           // ditto — unused by the kernel body
};

enum SimBlendToBinding {
  SIMBLENDTO_ResultPoints = 0,  // device LegacyPoint* (u0) — BOTH input ("A" side) and output, in-place
  SIMBLENDTO_PointsB      = 1,  // const device LegacyPoint* (t0) — "B" side, read-only
  SIMBLENDTO_Params       = 2,  // constant SimBlendToParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SimBlendToParams) == 16, "SimBlendToParams must be 16 bytes (4 floats)");
#endif
