// RadialPoints emitter (TiXL Point layout). Lays `Count` points on a ring of
// radius `Radius` in XY, identity rotation, white, unit scale. Feeds ParticleSystem
// as EmitPoints. (TiXL's RadialPoints lives in Lib.point.generate; this is the
// minimal faithful generator for the first slice.)
#include <metal_stdlib>
#include "tixl_point.h"        // Point
#include "particle_params.h"   // EmitParams, EmitBinding
using namespace metal;

kernel void radial_emit(device SwPoint*      pts [[buffer(EMIT_Points)]],
                        constant EmitParams& P   [[buffer(EMIT_Params)]],
                        uint3                tid [[thread_position_in_grid]]) {
  if (tid.x >= P.Count) return;
  float a = (float(tid.x) / float(P.Count)) * (2.0f * M_PI_F);
  pts[tid.x].Position = float3(P.Radius * cos(a), P.Radius * sin(a), 0.0f);
  pts[tid.x].FX1 = 0.0f;
  pts[tid.x].Rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);  // identity quaternion
  pts[tid.x].Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
  pts[tid.x].Scale = float3(1.0f, 1.0f, 1.0f);
  pts[tid.x].FX2 = 0.0f;
}
