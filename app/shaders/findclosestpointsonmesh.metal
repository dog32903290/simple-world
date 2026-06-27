// findclosestpointsonmesh — faithful 1:1 port of external/tixl
//   .../Assets/shaders/points/onmesh/FindClosestPointOnMesh.hlsl
// A point-MODIFY kernel: for each input Point, brute-force loop over EVERY mesh triangle, find the
// closest point on that triangle (closestPointOnTriangle), keep the global minimum, and snap the
// Point's Position onto it. NO BVH, no acceleration — exactly TiXL's O(points × faces) loop.
//
// ★MSL packing (metal-cpp-discipline): SwVertex/SwTriIndex come from sw_mesh.h's __METAL_VERSION__
// branch (packed_float3 12-byte members / int3 indices) so the 80-byte PbrVertex stride and 12-byte
// Int3 stride match the host byte-exactly. SwPoint (tixl_point.h) is the 64-byte LegacyPoint in/out.
//
// NAMED FORKS vs the .hlsl:
//   • PARITY (dead helper): the .hlsl also defines `udTriangle` (an unsigned triangle-distance fn) but
//     NEVER calls it — `closestPointOnTriangle` is the live one. We do NOT port `udTriangle` (porting
//     dead code adds an unused function; the live `closestPointOnTriangle` IS the algorithm). The .hlsl
//     `dot2` helper exists only for `udTriangle`, so it is also omitted. NO behavioural change.
//   • Threadgroup size: TiXL uses [numthreads(64,1,1)]; we use the project's standard 64 — identical.
//   • Out-of-range threads (i.x >= pointCount): the .hlsl sets ResultPoints[i.x].W = 0 and returns. Our
//     SwPoint's `.W` is FX2 (offset 60, the LegacyPoint.W slot — tixl_point.h). We set FX2 = 0 to match.
#include <metal_stdlib>
#include "tixl_point.h"   // SwPoint (64B LegacyPoint, in & out)
#include "sw_mesh.h"      // SwVertex (80B PbrVertex) + SwTriIndex (12B Int3) — MSL-shareable (packed_float3)
#include "findclosestpointsonmesh_params.h"  // FcpomParams + FCPOM_* bindings
using namespace metal;

// closestPointOnTriangle — bit-faithful port of FindClosestPointOnMesh.hlsl:46-148. The Ericson
// "closest point on triangle" decision tree (region a/b/c/d/e/f), VERBATIM (same branches, same clamps).
static inline float3 closestPointOnTriangle(float3 p0, float3 p1, float3 p2, float3 sourcePosition) {
  float3 edge0 = p1 - p0;
  float3 edge1 = p2 - p0;
  float3 v0    = p0 - sourcePosition;

  float a = dot(edge0, edge0);
  float b = dot(edge0, edge1);
  float c = dot(edge1, edge1);
  float d = dot(edge0, v0);
  float e = dot(edge1, v0);

  float det = a * c - b * b;
  float s   = b * e - c * d;
  float t   = b * d - a * e;

  if (s + t < det) {
    if (s < 0.0f) {
      if (t < 0.0f) {
        if (d < 0.0f) {
          s = clamp(-d / a, 0.0f, 1.0f);
          t = 0.0f;
        } else {
          s = 0.0f;
          t = clamp(-e / c, 0.0f, 1.0f);
        }
      } else {
        s = 0.0f;
        t = clamp(-e / c, 0.0f, 1.0f);
      }
    } else if (t < 0.0f) {
      s = clamp(-d / a, 0.0f, 1.0f);
      t = 0.0f;
    } else {
      float invDet = 1.0f / det;
      s *= invDet;
      t *= invDet;
    }
  } else {
    if (s < 0.0f) {
      float tmp0 = b + d;
      float tmp1 = c + e;
      if (tmp1 > tmp0) {
        float numer = tmp1 - tmp0;
        float denom = a - 2.0f * b + c;
        s = clamp(numer / denom, 0.0f, 1.0f);
        t = 1.0f - s;
      } else {
        t = clamp(-e / c, 0.0f, 1.0f);
        s = 0.0f;
      }
    } else if (t < 0.0f) {
      if (a + d > b + e) {
        float numer = c + e - b - d;
        float denom = a - 2.0f * b + c;
        s = clamp(numer / denom, 0.0f, 1.0f);
        t = 1.0f - s;
      } else {
        s = clamp(-e / c, 0.0f, 1.0f);
        t = 0.0f;
      }
    } else {
      float numer = c + e - b - d;
      float denom = a - 2.0f * b + c;
      s = clamp(numer / denom, 0.0f, 1.0f);
      t = 1.0f - s;
    }
  }

  return p0 + s * edge0 + t * edge1;
}

// main — FindClosestPointOnMesh.hlsl:152-198 VERBATIM. Per input point: loop every face, keep the
// nearest closestPointOnTriangle; if any face was found, snap Position to it. Result written 1:1.
kernel void findclosestpointsonmesh(device const SwPoint*    points    [[buffer(FCPOM_Points)]],
                                    device const SwVertex*   verts     [[buffer(FCPOM_Vertices)]],
                                    device const SwTriIndex* indices   [[buffer(FCPOM_Indices)]],
                                    device SwPoint*          result    [[buffer(FCPOM_ResultPoints)]],
                                    constant FcpomParams&    P         [[buffer(FCPOM_Params)]],
                                    constant uint&           faceCount [[buffer(FCPOM_FaceCount)]],
                                    uint3                    i         [[thread_position_in_grid]]) {
  // .hlsl:155-160 — out-of-range thread: ResultPoints[i.x].W = 0; return. SwPoint.W == FX2 (@60).
  if (i.x >= P.Count) {
    result[i.x].FX2 = 0.0f;
    return;
  }

  SwPoint p = points[i.x];
  int   closestIndex    = -1;        // .hlsl:169
  float closestDistance = 99999.0f;  // .hlsl:170
  float3 pos = float3(p.Position.x, p.Position.y, p.Position.z);  // .hlsl:171 (packed_float3 -> float3)
  float3 closestPoint = float3(0.0f);

  for (uint faceIndex = 0; faceIndex < faceCount; faceIndex++) {  // .hlsl:174
    SwTriIndex f = indices[faceIndex];
    float3 v0 = float3(verts[f.X].Position.x, verts[f.X].Position.y, verts[f.X].Position.z);
    float3 v1 = float3(verts[f.Y].Position.x, verts[f.Y].Position.y, verts[f.Y].Position.z);
    float3 v2 = float3(verts[f.Z].Position.x, verts[f.Z].Position.y, verts[f.Z].Position.z);
    float3 pointOnFace = closestPointOnTriangle(v0, v1, v2, pos);

    float distance2 = length(pointOnFace - pos);  // .hlsl:184 (length, NOT squared, despite the name)
    if (distance2 < closestDistance) {
      closestDistance = distance2;
      closestIndex    = (int)faceIndex;
      closestPoint    = pointOnFace;
    }
  }

  if (closestIndex >= 0) {           // .hlsl:192
    p.Position = packed_float3(closestPoint);
  }

  result[i.x] = p;                   // .hlsl:197
}
