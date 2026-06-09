// spherepoints — faithful port of external/tixl
// .../Assets/shaders/points/generate/SpherePoints.hlsl (the REAL math). A generator op
// for the lane-A point-graph: writes a bag of SwPoints (no input bag).
//
// TiXL parity (SpherePoints.hlsl lines 34-49 are the source of truth for position):
//   t = i / (count-1); y = 1 - 2t  (y goes from 1 to -1)
//   radius = sqrt(1 - y*y)         (ring radius at height y)
//   theta  = highprecision( i*phi mod 2PI ) + StartAngle*(PI/180) + (hash11(i/123.71)-0.5)*Scatter
//            with phi = PI*(3 - sqrt(5))  (golden angle) — the Fibonacci spiral.
//   x = cos(theta)*radius;  z = sin(theta)*radius
//   position = float3(x,y,z) * Radius + Center
// Because x*x + y*y + z*z = (1-y*y) + y*y = 1, the pre-scale point is unit length, so
// every output point sits exactly `Radius` from `Center`. That invariant is what the
// golden asserts (--selftest-spherepoints).
//
// Baked to TiXL defaults (NOT load-bearing for a point cloud; flagged in parityNotes):
//   - Color = white (TiXL sets Color = 1)
//   - Rotation = identity. TiXL computes a faithful orientation quaternion (rot/rot2/rot5
//     via qFromAngleAxis/qMul) so z aligns with the sphere normal — only matters for
//     oriented meshes/lines AT the points, not for the position-faithful point cloud /
//     DrawPoints billboards. The quat helpers aren't in this world; baking to identity
//     keeps the leaf clean and avoids an untested quat path. Position + Radius are faithful.
#include <metal_stdlib>
#include "tixl_point.h"          // SwPoint (64B)
#include "spherepoints_params.h" // SphereParams, SphereBinding
using namespace metal;

// hash11 — ported 1:1 from external/tixl .../shared/hash-functions.hlsl (so Scatter is
// the real TiXL jitter, not an approximation).
static float hash11(float p) {
  p = fract(p * 0.1031f);
  p *= p + 33.33f;
  p *= p + p;
  return fract(p);
}

kernel void spherepoints(device SwPoint*        pts [[buffer(SPHERE_Points)]],
                         constant SphereParams&  P   [[buffer(SPHERE_Params)]],
                         uint3                   tid [[thread_position_in_grid]]) {
  if (tid.x >= P.Count) return;
  uint i = tid.x;

  // golden-angle spiral constants (SpherePoints.hlsl lines 16-19)
  const float precision = 0.0001f;
  const float phi = M_PI_F * (3.0f - sqrt(5.0f));  // golden angle in radians
  const float modPi = 2.0f * M_PI_F * precision;
  const float toRad = M_PI_F / 180.0f;

  // t = i / (count-1); y from 1 to -1; radius at y
  float denom = P.Count > 1u ? float(P.Count - 1u) : 1.0f;
  float t = float(i) / denom;
  float y = 1.0f - t * 2.0f;
  float ringRadius = sqrt(max(0.0f, 1.0f - y * y));

  // theta: high-precision (i*phi) mod 2PI, then StartAngle + Scatter jitter
  float theta = float(i) * precision;
  theta *= phi;
  theta = fmod(theta, modPi);
  theta /= precision;
  theta += P.StartAngle * toRad;
  theta += (hash11(float(i) / 123.71f) - 0.5f) * P.Scatter;

  float x = cos(theta) * ringRadius;
  float z = sin(theta) * ringRadius;

  float3 unitPos = float3(x, y, z);             // |unitPos| == 1
  float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);

  SwPoint p;
  p.Position = unitPos * P.Radius + center;     // every point at distance Radius from Center
  p.FX1 = 1.0f;                                 // TiXL sets FX1 = 1
  p.Rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);  // identity — orientation quat baked (see header)
  p.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);     // TiXL sets Color = 1
  p.Scale = float3(1.0f, 1.0f, 1.0f);           // TiXL sets Scale = 1
  p.FX2 = 1.0f;                                 // TiXL sets FX2 = 1
  pts[i] = p;
}
