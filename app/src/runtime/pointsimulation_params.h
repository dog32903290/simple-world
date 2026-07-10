// Host<->shader params for TiXL PointSimulation (points/sim, mathv transpiler-batch kernel
// INVENTORY, 2026-07-10, MATH_VERIFY_WORKFLOW.md §10). NOT wired to node_registry / kernelNameFor /
// t3_import — kernel-inventory entry only; connecting to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/points/combine/PointSimulation.hlsl cbuffer Params : b0.
// A cross-frame "restore toward source" blend: ResultPoints (IN-PLACE UAV, the running simulation
// state, also read as the "current" side) is mixed toward SourcePoints (SRV, the "original" side) by
// MixOriginal — EXCEPT where either side carries a NaN sentinel (isnan trap, PointSimulation.hlsl:34),
// in which case the point is reset from SourcePoints instead of blended (see mathv_ref header note).
//
// Count = SourcePoints' element count (as a uint). PointSimulation.hlsl:25-26 gets this via
// `SourcePoints.GetDimensions(sourcePointcount, stride)` (§10.5 限制① GetDimensions 魔法緩衝) — the
// transpiled kernel (app/shaders/pointsimulation.metal) substitutes the transpiler's
// spvBufferSizeConstants[[buffer(25)]] array read with `Count * 64u` (SwPoint's 64-byte stride),
// matching AddNoise's precedent (MATH_VERIFY_WORKFLOW.md §10.5①).
#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PointSimulationParams {
#ifdef __METAL_VERSION__
  uint  Count;          // SourcePoints element count (host-fed, replaces GetDimensions — see above)
#else
  uint32_t Count;
#endif
  float MixOriginal;     // .cs MixOriginal, PointSimulation.t3 Default=0.005, no t3ui Min/Max
  float Reset;           // .cs Reset (bool authored as float), PointSimulation.t3 Default=false(0.0)
};

enum PointSimulationBinding {
  POINTSIMULATION_Params       = 0,  // constant PointSimulationParams& (b0)
  POINTSIMULATION_ResultPoints = 1,  // device LegacyPoint* (u0) -- IN-PLACE, also read as "current"
  POINTSIMULATION_SourcePoints = 2,  // const device LegacyPoint* (t0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PointSimulationParams) == 12, "PointSimulationParams must be 12 bytes");
#endif
