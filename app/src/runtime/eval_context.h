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
  // LocalFxTime seam (additive): repurposes the former reserved `_pad` slot at offset 12.
  // BARS (= TiXL EvaluationContext.LocalFxTime = Playback.FxTimeInBars), NOT seconds — `time` above
  // is the seconds clock. CPU value ops that need TiXL's LocalFxTime read this (e.g. PerlinNoise2's
  // OverrideTime-unwired path). The struct stays 16 bytes (offset 12, was `_pad`) so BI_EvalContext's
  // constant-buffer layout is byte-identical: no GPU upload reads offset 12 (the shaders only read
  // frameIndex/time/deltaTime at offsets 0/4/8), so this rename is GPU-side a no-op.
  float localFxTime;
};

#ifndef __METAL_VERSION__
// Single shared header (host + .metal) so the layout is identical on both sides by
// construction; this assert is a drift tripwire (see metal-cpp-discipline). Shaders read
// frameIndex/time/deltaTime (offsets 0/4/8); localFxTime (offset 12, formerly _pad) keeps the
// constant buffer 16-byte sized AND is a host-side CPU field — no shader reads it.
// (Audio reaches the graph via the SpectrumSnapshot cook -> Node::outCache, not through ctx.)
static_assert(sizeof(EvaluationContext) == 16, "EvaluationContext layout changed — sync shaders/upload");
#endif
