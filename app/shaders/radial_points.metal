#include <metal_stdlib>
#include "Particle.h"  // shared layout + BufferIndex enum — same the host uploads
using namespace metal;

// RadialPoints: lay `count` points evenly around a circle of radius `radius` in
// the XY plane, each with a tangential velocity of magnitude `speed` (so a later
// TransformPoints integrating position += velocity*dt moves them along the ring).
kernel void radial_points(device Particle*       pts    [[buffer(BI_Particles)]],
                          constant RadialParams& params [[buffer(BI_GenParams)]],
                          uint                   tid    [[thread_position_in_grid]]) {
  if (tid >= params.count) return;
  float a = (float(tid) / float(params.count)) * (2.0f * M_PI_F);
  float ca = cos(a), sa = sin(a);
  pts[tid].position = float3(params.radius * ca, params.radius * sa, 0.0f);
  pts[tid].velocity = float3(-sa, ca, 0.0f) * params.speed;  // tangential
}
