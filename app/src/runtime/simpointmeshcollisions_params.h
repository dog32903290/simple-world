// Host<->shader params for TiXL points/sim/SimPointMeshCollisions.hlsl (owner op:
// SimPointMeshCollisions) — mathv transpiler-batch kernel INVENTORY (2026-07-10, MATH_VERIFY_
// WORKFLOW.md §10 wave-3, replacement candidate 6/6 — mesh-CollapseVertices.hlsl was RETIRED from this
// slot, see §10.5 "limitation" note below). NOT wired to node_registry / t3_import — a verified-
// kernel-in-stock entry; connecting it to a stage is a separate future lane. Owner: external/tixl/
// Operators/Lib/point/sim/SimPointMeshCollisions.t3 (a genuinely deterministic per-particle kernel —
// unlike ScatterMeshFaces (documented "DANGER" shared-vertex race) and IkChain (iterative FABRIK
// solver, every thread redundantly recomputes ALL chains against a stateful buffer being written by
// other threads concurrently — both screened OUT as unsuitable for direct-kernel-dispatch fuzzing,
// which assumes one thread -> one deterministic output from READ-ONLY inputs).
//
// ★NEW LIMITATION FOUND THIS WAVE (candidate swap, not in §10.5 yet — record for the SSOT next pass):
// mesh-CollapseVertices.hlsl's `switch (VolumeShape)` (a bare `float`, no `(int)` cast — contrast
// mesh-Deform.hlsl's `switch((int)TwistAxis)`) makes glslang emit "case duplicated value" + a cascading
// parse error. This is a genuine glslang HLSL-frontend limitation with switch-on-float (not a bug in
// the ref or a kernel-authoring mistake) — recorded here as the wave-3 in-flight discovery; promote to
// MATH_VERIFY_WORKFLOW.md §10.5 ⑧ in a follow-up doc pass.
//
// Mirrors external/tixl .../Assets/shaders/points/sim/SimPointMeshCollisions.hlsl's cbuffer Params
// (:6-13, only Bounciness/Damping — NOTE: Damping is declared but NEVER READ by main(), a dead field,
// same class as selectvertices_params.h's UseVertexSelection).
//
// TWO live GetDimensions calls need host-ABI replacement (§10.2③/§10.5①) — this op has TWO count
// fields, unlike every prior op in this batch which had at most one:
//   - Indices.GetDimensions -> FaceCount (the triangle-index buffer's element count)
//   - Particles.GetDimensions -> PointCount (the dispatch guard's element count)
//   Vertices.GetDimensions IS called in the original HLSL (`vertexCount`) but the result is NEVER
//   READ afterward — confirmed dead by the raw transpile output (no spvBufferSizeConstants reference
//   for Vertices' own buffer index) — correctly absent from this host-ABI, nothing to replace.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimPointMeshCollisionsParams {
  float Bounciness;
  float Damping;  // dead field -- never read by main(), see header
#ifdef __METAL_VERSION__
  uint FaceCount;   // host-ABI (replaces Indices.GetDimensions, §10.5①)
  uint PointCount;  // host-ABI (replaces Particles.GetDimensions, §10.5①)
#else
  uint32_t FaceCount;
  uint32_t PointCount;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — NOT declaration order:
// HLSL declares Particles(u0) first, then Vertices(t0), then Indices(t1), but the compacted
// [[buffer(N)]] numbering came out Indices=0/Vertices=1/Particles=2/Params=3).
enum SimPointMeshCollisionsBinding {
  SPMC_Indices   = 0,  // const device SwTriIndex*      (t1)
  SPMC_Vertices  = 1,  // const device SwVertex*        (t0)
  SPMC_Particles = 2,  // device Particle*               (u0)
  SPMC_Params    = 3,  // constant SimPointMeshCollisionsParams& (b0, extended with host-ABI counts)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SimPointMeshCollisionsParams) == 16,
              "SimPointMeshCollisionsParams must be 16 bytes (2 float + 2 uint32)");
#endif
