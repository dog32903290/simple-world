// Host<->shader params for TiXL points/sim/simulate-points — mathv transpiler-batch kernel INVENTORY
// (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-4). NOT wired to node_registry / t3_import — a
// verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/points/sim/simulate-points.hlsl. Owner:
// external/tixl/Operators/Lib/point/sim/_legacy/_LegacySimForwardMovement.t3 (a legacy standalone
// forward-movement integrator — NOT the same op as sw's existing `particle_sim` kernel, which ports
// the DIFFERENT, more complete external/tixl .../particles/ParticleSystem.hlsl: that kernel's drag
// is `velocity *= pow(1-Drag, Speed)` (frame-rate-aligned), this kernel's is the simpler
// `velocity *= (1-Drag)` per-dispatch with no `pow` — genuinely different math, not a duplicate).
//
// Entry name is `main` (confirmed via `grep numthreads -A2`).
//
// GetDimensions substitution (§10.5①): the HLSL calls `Particles.GetDimensions(numStructs, stride)`
// to bound the dispatch (`if (i.x >= numStructs) return;`). The transpiler emits a
// `spvBufferSizeConstants[[buffer(25)]]` byte-size lookup for this; the adapter replaces it with a
// host-ABI `Count` field (element count, NOT byte size — the host already knows how many Particles
// it allocated, so no stride division is needed on the GPU side).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimulatePointsParams {
#ifdef __METAL_VERSION__
  int   Count;  // host-ABI substitution for GetDimensions (§10.5①) -- NOT an HLSL cbuffer field
  float Drag;
  float Speed;
  float _pad0;  // -> 16 bytes
#else
  int32_t Count;
  float   Drag;
  float   Speed;
  float   _pad0;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — declaration order):
// Particles(u0)->buffer(0), Params(b0)->buffer(1). The transpiler's spvBufferSizeConstants
// [[buffer(25)]] slot (§10.5①) is DROPPED entirely -- the adapter substitutes P.Count instead of
// binding a size-constants buffer, so buffer(25) is never used by this kernel.
enum SimulatePointsBinding {
  SIMULATEPOINTS_Particles = 0,  // device Particle*                 (u0)
  SIMULATEPOINTS_Params    = 1,  // constant SimulatePointsParams&   (b0, host-ABI extended)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SimulatePointsParams) == 16, "SimulatePointsParams must be 16 bytes");
#endif
