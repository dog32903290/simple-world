// Shared host<->shader layout. Included by BOTH the .metal and the .cpp so the
// compiler proves the layout matches (metal-cpp-discipline Iron Rule 1). Never
// redefine these structs anywhere else; never hand-pad away from this file.
//
// This is the contract spine the gemini-research "SharedTypes.h" pattern + the
// old MY_WORLD_RUNTIME_CONTRACT both point at: struct layout + binding-slot enum
// in ONE place, so parallel work later can't drift the layout.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <simd/simd.h>
  #include <cstdint>
  using simd::float2;
  using simd::float3;
#endif

#include "eval_context.h"  // EvaluationContext (extracted; uploaded at BI_EvalContext)

// Buffer binding slots — single source of truth for CPU setBuffer/setBytes AND
// MSL [[buffer(N)]]. Convention: particles always at slot 0.
enum BufferIndex {
  BI_Particles   = 0,  // device Particle*
  BI_GenParams   = 1,  // RadialParams (generators)
  BI_EvalContext = 2,  // EvaluationContext (per-frame: time/deltaTime)
};

// One point/particle. float3 occupies 16 bytes on BOTH sides (not 12).
struct Particle {
  float3 position;  // 16
  float3 velocity;  // 16  -> stride 32, 16-aligned
};

// Parameters for the RadialPoints generator (uploaded via setBytes at BI_GenParams).
struct RadialParams {
#ifdef __METAL_VERSION__
  uint count;
#else
  uint32_t count;
#endif
  float radius;
  float speed;  // tangential speed written into velocity
};

// EvaluationContext moved to eval_context.h (included above) so callers that need
// only the time/frame context don't drag in `struct Particle`.

#ifndef __METAL_VERSION__
static_assert(sizeof(Particle) == 32, "Particle must be 32 bytes (two 16-byte float3)");
static_assert(sizeof(Particle) % 16 == 0, "Particle stride must be 16-aligned");
#endif
