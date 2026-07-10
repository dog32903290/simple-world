#pragma once
// mathv_ref_deformmesh — CPU scalar oracle for TiXL mesh/modify/mesh-Deform.hlsl (owner op:
// DeformMesh; owner: external/tixl/Operators/Lib/mesh/modify/DeformMesh.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-Deform.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/deformmesh.metal intentionally never opened while writing this file).
//   cbuffer Params           :5-20
//   NormalizeToSphere()      :26-31
//   TaperFunction()          :34-37
//   TwistFunction()          :40-63
//   main() body              :72-134
//
// TRANSCENDENTAL class (length()/cos()/sin() — sqrt under the hood).
//
// ★AMBIGUITY-PINNED (deformmesh_params.h header, §9 "HLSL 本體歧義"): TwistAxis outside {0,1,2} is a
// HLSL uninitialized-read UB in the ORIGINAL source; this ref pins the SAME resolution the transpiler
// (glslang+spirv-cross) deterministically chose (zero-fill `twisted` before the `+TwistPivot`, i.e.
// the post-twist local == TwistPivot exactly) — NOT a claim about true multi-backend HLSL semantics.
// TaperAxis outside {0,1,2} is well-defined in the ORIGINAL HLSL (falls through with `tapered`
// unchanged from its pre-switch value) — this ref models that directly, no UB/pinning involved.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cmath>

namespace sw {
namespace mathv_ref {

struct Vec3f { float x, y, z; };
inline Vec3f v3sub(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3f v3add(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3f v3scale(Vec3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3f v3lerp(Vec3f a, Vec3f b, float s) { return {a.x + s * (b.x - a.x), a.y + s * (b.y - a.y), a.z + s * (b.z - a.z)}; }
inline float v3len(Vec3f a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

struct DeformMeshIn {
  float posX, posY, posZ;
};
struct DeformMeshParamsR {
  float useVertexSelection, spherize, radius, taperAmount, twistAmount, taperAxis, twistAxis;
  float pivotX, pivotY, pivotZ, twistPivotX, twistPivotY, twistPivotZ, taper2X, taper2Y;
};

// NormalizeToSphere — HLSL:26-31. NaN/Inf when posWithPivot==0 (0/0), faithfully reproduced (no
// div-by-zero guard in the original HLSL either).
inline Vec3f normalizeToSphere(Vec3f position, float targetRadius, Vec3f pivot) {
  Vec3f posWithPivot = v3sub(position, pivot);
  float currentRadius = v3len(posWithPivot);
  return v3scale(posWithPivot, targetRadius / currentRadius);
}

// deformMeshOne — HLSL main():72-134 (per-vertex; Selected passed in as `selected`).
inline Vec3f deformMeshOne(Vec3f pos, float selected, const DeformMeshParamsR& P) {
  const float s = P.useVertexSelection > 0.5f ? selected : 1.0f;
  Vec3f pivot{P.pivotX, P.pivotY, P.pivotZ};
  Vec3f twistPivot{P.twistPivotX, P.twistPivotY, P.twistPivotZ};

  Vec3f spherePos = normalizeToSphere(pos, P.radius, pivot);
  Vec3f afterSpherize = v3lerp(pos, v3lerp(pos, spherePos, P.spherize), s);  // = pos when s cancels? no: see header
  // NOTE: matches HLSL exactly -- `pos = lerp(pos, lerp(pos, spherePos, Spherize), s)`.
  Vec3f pos1 = afterSpherize;

  Vec3f tapered = pos1;
  const int taperAxis = (int)P.taperAxis;  // HLSL (int) cast: truncate-toward-zero
  const float taper2x = P.taper2X * P.taperAmount, taper2y = P.taper2Y * P.taperAmount;
  if (taperAxis == 0) {
    float yy = pos1.y * (1.0f - taper2x * pos1.x);
    float zz = pos1.z * (1.0f - taper2y * pos1.x);
    tapered.y = yy;
    tapered.z = zz;
  } else if (taperAxis == 1) {
    float xx = pos1.x * (1.0f - taper2x * pos1.y);
    float zz = pos1.z * (1.0f - taper2y * pos1.y);
    tapered.x = xx;
    tapered.z = zz;
  } else if (taperAxis == 2) {
    float xx = pos1.x * (1.0f - taper2x * pos1.z);
    float yy = pos1.y * (1.0f - taper2y * pos1.z);
    tapered.x = xx;
    tapered.y = yy;
  }
  // default (any other int value): tapered stays == pos1 (well-defined HLSL fallthrough).

  Vec3f pos2 = v3lerp(pos1, tapered, s);

  const float angle = P.twistAmount * 0.01745329251f;  // radians()
  Vec3f posWithTwistPivot = v3sub(pos2, twistPivot);
  const int twistAxis = (int)P.twistAxis;
  Vec3f twisted;
  if (twistAxis == 0) {
    float c = std::cos(posWithTwistPivot.x * angle), sn = std::sin(posWithTwistPivot.x * angle);
    twisted = {posWithTwistPivot.x, posWithTwistPivot.y * c - posWithTwistPivot.z * sn,
               posWithTwistPivot.y * sn + posWithTwistPivot.z * c};
  } else if (twistAxis == 1) {
    float c = std::cos(posWithTwistPivot.y * angle), sn = std::sin(posWithTwistPivot.y * angle);
    twisted = {posWithTwistPivot.x * c - posWithTwistPivot.z * sn, posWithTwistPivot.y,
               posWithTwistPivot.x * sn + posWithTwistPivot.z * c};
  } else if (twistAxis == 2) {
    float c = std::cos(posWithTwistPivot.z * angle), sn = std::sin(posWithTwistPivot.z * angle);
    twisted = {posWithTwistPivot.x * c - posWithTwistPivot.y * sn,
               posWithTwistPivot.x * sn + posWithTwistPivot.y * c, posWithTwistPivot.z};
  } else {
    // ★AMBIGUITY-PINNED (see header): transpiler resolved the HLSL UB to zero-fill.
    twisted = {0.0f, 0.0f, 0.0f};
  }

  Vec3f twistedBack = v3add(twisted, twistPivot);
  return v3lerp(pos2, twistedBack, s);
}

}  // namespace mathv_ref
}  // namespace sw
