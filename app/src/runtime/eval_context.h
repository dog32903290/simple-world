// Per-frame evaluation context — the runtime owner of visual time supplies this
// to every cooked node (FRAME_SCHEDULER_CONTRACT: one frameIndex/time/deltaTime
// per frame, every node reads the same one). Uploaded at BI_EvalContext.
//
// Extracted from Particle.h so code that needs ONLY the time/frame context (graph
// eval, source registry, main's per-frame loop) can include it WITHOUT pulling in
// `struct Particle` — which collides with tixl_point.h's 64-byte Particle in the
// same TU. Both Particle.h and tixl_point.h include this, so either layout header
// transitively provides EvaluationContext.
//
// Dual-compiled (host .cpp + .metal): keep the __METAL_VERSION__ guards, and the
// includers must reference it as the bare "eval_context.h" (shaders only get
// -I src/runtime, no -I src).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct EvaluationContext {
#ifdef __METAL_VERSION__
  uint frameIndex;
#else
  uint32_t frameIndex;
#endif
  float time;        // seconds since start
  float deltaTime;   // seconds since previous frame
  float _pad;        // reserved; keeps the BI_EvalContext constant buffer at 16 bytes.
};

#ifndef __METAL_VERSION__
// Single shared header (host + .metal) so the layout is identical on both sides by
// construction; this assert is a drift tripwire (see metal-cpp-discipline). Shaders read
// frameIndex/time/deltaTime (offsets 0/4/8); _pad keeps the constant buffer 16-byte sized.
// (Audio reaches the graph via the SpectrumSnapshot cook -> Node::outCache, not through ctx.)
static_assert(sizeof(EvaluationContext) == 16, "EvaluationContext layout changed — sync shaders/upload");
#endif
