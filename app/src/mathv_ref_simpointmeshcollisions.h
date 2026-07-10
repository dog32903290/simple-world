#pragma once
// mathv_ref_simpointmeshcollisions — CPU scalar oracle for TiXL points/sim/SimPointMeshCollisions.hlsl
// (owner op: SimPointMeshCollisions; owner: external/tixl/Operators/Lib/point/sim/
// SimPointMeshCollisions.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/sim/SimPointMeshCollisions.hlsl — NOT derived from sw's MSL
// kernel (app/shaders/simpointmeshcollisions.metal intentionally never opened while writing this file).
//   cbuffer Params                    :6-13
//   closestPointOnTriangle()          :21-119 (Ericson "Real-Time Collision Detection" closest-point-
//                                        on-triangle, region classification via barycentric s/t)
//   findClosestPointAndDistance()     :127-149
//   main() body                       :162-217
//
// TRANSCENDENTAL+BRANCHY class (length/normalize + closestPointOnTriangle's 7-way region branch).
//
// ★AMBIGUITY-PINNED (§9 "HLSL 本體歧義", same class as DeformMesh's TwistAxis default): if FaceCount==0
// the loop in findClosestPointAndDistance never executes, and `closestSurfacePoint` (an HLSL `out`
// param, never explicitly initialized before the loop) is read uninitialized -- UB. The transpiler
// resolved this deterministically to a ZERO-filled float3 (`constant float3 _862 = {}` in the raw
// output). This ref pins that SAME resolution for FaceCount==0 (not exercised by the main fuzz domain,
// which keeps FaceCount>=1 as the realistic case; recorded for completeness, no dedicated tooth --
// diminishing-returns tradeoff for the wave's 6th candidate).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cmath>
#include <cstdint>
#include <vector>

namespace sw {
namespace mathv_ref {

struct Vec3d { float x, y, z; };
inline Vec3d vsub(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d vadd(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d vscale(Vec3d a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float vdot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float vlen(Vec3d a) { return std::sqrt(vdot(a, a)); }
inline Vec3d vnorm(Vec3d a) { float l = vlen(a); return {a.x / l, a.y / l, a.z / l}; }
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// closestPointOnTriangle — HLSL:21-119 verbatim region logic (Ericson's algorithm).
inline Vec3d closestPointOnTriangle(Vec3d p0, Vec3d p1, Vec3d p2, Vec3d sourcePosition) {
  Vec3d edge0 = vsub(p1, p0), edge1 = vsub(p2, p0), v0 = vsub(p0, sourcePosition);
  float a = vdot(edge0, edge0), b = vdot(edge0, edge1), c = vdot(edge1, edge1);
  float d = vdot(edge0, v0), e = vdot(edge1, v0);
  float det = a * c - b * b;
  float s = b * e - c * d;
  float t = b * d - a * e;

  if (s + t < det) {
    if (s < 0.0f) {
      if (t < 0.0f) {
        if (d < 0.0f) { s = clamp01(-d / a); t = 0.0f; }
        else { s = 0.0f; t = clamp01(-e / c); }
      } else { s = 0.0f; t = clamp01(-e / c); }
    } else if (t < 0.0f) {
      s = clamp01(-d / a); t = 0.0f;
    } else {
      float invDet = 1.0f / det;
      s *= invDet; t *= invDet;
    }
  } else {
    if (s < 0.0f) {
      float tmp0 = b + d, tmp1 = c + e;
      if (tmp1 > tmp0) {
        float numer = tmp1 - tmp0, denom = a - 2.0f * b + c;
        s = clamp01(numer / denom); t = 1.0f - s;
      } else { t = clamp01(-e / c); s = 0.0f; }
    } else if (t < 0.0f) {
      if (a + d > b + e) {
        float numer = c + e - b - d, denom = a - 2.0f * b + c;
        s = clamp01(numer / denom); t = 1.0f - s;
      } else { s = clamp01(-e / c); t = 0.0f; }
    } else {
      float numer = c + e - b - d, denom = a - 2.0f * b + c;
      s = clamp01(numer / denom); t = 1.0f - s;
    }
  }
  return vadd(vadd(p0, vscale(edge0, s)), vscale(edge1, t));
}

struct SpmcFace { int32_t x, y, z; };

// findClosestPointAndDistance — HLSL:127-149. faceCount==0 -> AMBIGUITY-PINNED zero (see header).
inline Vec3d findClosestSurfacePoint(const std::vector<Vec3d>& vertexPositions,
                                      const std::vector<SpmcFace>& faces, Vec3d pos) {
  Vec3d best{0.0f, 0.0f, 0.0f};
  float bestDist = 99999.0f;
  for (const auto& f : faces) {
    Vec3d pointOnFace =
        closestPointOnTriangle(vertexPositions[(size_t)f.x], vertexPositions[(size_t)f.y],
                                vertexPositions[(size_t)f.z], pos);
    float dist = vlen(vsub(pointOnFace, pos));
    if (dist < bestDist) { bestDist = dist; best = pointOnFace; }
  }
  return best;
}

// simPointMeshCollisionsOne — HLSL main():188-215. Returns true iff Velocity was written (both early-
// return guards NOT taken); `outVelocity` is only meaningful when true.
inline bool simPointMeshCollisionsOne(Vec3d particlePos, float particleRadius, Vec3d particleVelocity,
                                       const std::vector<Vec3d>& vertexPositions,
                                       const std::vector<SpmcFace>& faces, float bounciness,
                                       Vec3d& outVelocity) {
  Vec3d closestSurfacePoint = findClosestSurfacePoint(vertexPositions, faces, particlePos);
  Vec3d vToSurface = vsub(particlePos, closestSurfacePoint);
  float distance = vlen(vToSurface);
  if (std::isnan(distance) || distance < 0.001f) return false;
  if (distance > particleRadius) return false;
  outVelocity = vadd(particleVelocity, vscale(vnorm(vToSurface), bounciness));
  return true;
}

}  // namespace mathv_ref
}  // namespace sw
