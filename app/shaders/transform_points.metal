#include <metal_stdlib>
#include "Particle.h"  // shared layout + BufferIndex enum
using namespace metal;

// TransformPoints: Euler-integrate position by velocity over one frame's dt.
// Every cooked node reads the same per-frame EvaluationContext (old
// FRAME_SCHEDULER_CONTRACT: one deltaTime per frame, not a per-node clock).
kernel void transform_points(device Particle*           pts   [[buffer(BI_Particles)]],
                             constant EvaluationContext& ctx   [[buffer(BI_EvalContext)]],
                             constant uint&              count [[buffer(BI_GenParams)]],
                             uint                        tid   [[thread_position_in_grid]]) {
  if (tid >= count) return;
  pts[tid].position += pts[tid].velocity * ctx.deltaTime;
}
