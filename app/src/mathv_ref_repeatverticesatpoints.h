#pragma once
// mathv_ref_repeatverticesatpoints — CPU scalar oracle for TiXL mesh-RepeatVerticesAtPoints (3d/mesh,
// instancing utility; owner: external/tixl/Operators/Lib/mesh/generate/RepeatMeshAtPoints.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-RepeatVerticesAtPoints.hlsl — NOT derived from sw's MSL
// kernel (app/shaders/repeatverticesatpoints.metal intentionally never opened while writing this
// file).
//   cbuffer Params b0 (Stretch,Size,ApplyScale)              :7-11
//   cbuffer Params b1 (PointCount,ScaleFX,TexCoord2Factor)   :13-17
//   main() body                                              :24-70
//
// ROTATION IDENTITY (provenance note, R-role judgment call): HLSL:59 `mul(float4(v,1),
// transpose(qToMatrix(p.Rotation)))` — this ref uses mathv_ref_shared_quat.h::qRotateVec3(v, q)
// instead of re-deriving the matrix-transpose algebra: "rotate vector v by quaternion q via the
// transposed quaternion-to-matrix row-vector product" is the SAME rotation as qRotateVec3(v,q)'s
// direct cross-product formula (both compute the standard quaternion sandwich product v' = v +
// 2w(q_xyz×v) + 2(q_xyz×(q_xyz×v))) — qRotateVec3 is already an S-audited oracle (wave-1
// mathv_ref_pointsimulation.h). Confirmed independently: spirv-cross's OWN transpiled output for this
// exact kernel (see repeatverticesatpoints.metal) inlines Normal/Tangent/Bitangent's rotation as
// literally this cross-product formula (not a matrix multiply) — two structurally different transpiler
// code paths (Position via matrix, Normal/Tangent/Bitangent via cross-product) computing the same
// quaternion rotation is corroborating evidence, not just an assumption.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper (mathv_ref_shared_quat.h is itself a P5-safe TRANSCRIBED oracle, not
// an sw math helper) — pure host arithmetic transcribed from the HLSL text above.
//
// SCOPE: this TU asserts Position (the rotate+scale+translate path) + Normal (rotate-only path) +
// ColorRGB.x (simple multiply) + TexCoord2.y (enum-branch multiply) — the four structurally distinct
// arithmetic shapes in the kernel; Tangent/Bitangent reuse Normal's exact formula (not independently
// modeled) and TexCoord/Selected are straight passthrough (not modeled).
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "mathv_ref_shared_quat.h"

namespace sw {
namespace mathv_ref {

struct RepeatVerticesAtPointsParams {
  float stretchX, stretchY, stretchZ;
  float size;
  float applyScale;       // truthy float (HLSL `ApplyScale ?`)
  int32_t scaleFX;         // 0=1 / 1=p.FX1 / else=p.FX2
  int32_t texCoord2Factor; // 0=1 / 1=p.FX1 / else=p.FX2
};
struct RepeatVerticesAtPointsIn {
  float vPosX, vPosY, vPosZ;      // SourceVertices[vertexIndex].Position
  float vNormX, vNormY, vNormZ;   // SourceVertices[vertexIndex].Normal
  float vColorR;                  // SourceVertices[vertexIndex].ColorRGB.r
  float vTexCoord2Y;               // SourceVertices[vertexIndex].TexCoord2.y
  float pPosX, pPosY, pPosZ;      // Points[pointIndex].Position
  float pRotX, pRotY, pRotZ, pRotW;  // Points[pointIndex].Rotation
  float pColorR;                   // Points[pointIndex].Color.r
  float pScaleX, pScaleY, pScaleZ;  // Points[pointIndex].Scale (FX-a.k.a. Stretch role)
  float pFX1, pFX2;                 // Points[pointIndex].FX1/FX2 (ScaleFX/TexCoord2Factor enum reads)
};
struct RepeatVerticesAtPointsOut {
  float posX, posY, posZ;
  float normX, normY, normZ;
  float colorR;
  float texCoord2Y;
};

// repeatVerticesAtPointsOne — HLSL main():34-70.
inline void repeatVerticesAtPointsOne(const RepeatVerticesAtPointsIn& in, RepeatVerticesAtPointsOut& out,
                                      const RepeatVerticesAtPointsParams& p) {
  // :39-41 texCoord2Factor enum + TexCoord2.y multiply
  float texCoord2Factor = p.texCoord2Factor == 0 ? 1.0f : (p.texCoord2Factor == 1 ? in.pFX1 : in.pFX2);
  out.texCoord2Y = in.vTexCoord2Y * texCoord2Factor;

  // :49-51 sizeFxFactor enum
  float sizeFxFactor = p.scaleFX == 0 ? 1.0f : (p.scaleFX == 1 ? in.pFX1 : in.pFX2);
  // :53 resizeFromScale = ApplyScale ? p.Scale : 1
  float rsx = p.applyScale != 0.0f ? in.pScaleX : 1.0f;
  float rsy = p.applyScale != 0.0f ? in.pScaleY : 1.0f;
  float rsz = p.applyScale != 0.0f ? in.pScaleZ : 1.0f;
  // :55 posInObject.xyz *= max(0,resizeFromScale) * Stretch * Size * sizeFxFactor
  float sx = std::max(0.0f, rsx) * p.stretchX * p.size * sizeFxFactor;
  float sy = std::max(0.0f, rsy) * p.stretchY * p.size * sizeFxFactor;
  float sz = std::max(0.0f, rsz) * p.stretchZ * p.size * sizeFxFactor;
  quat::Vec3 scaled{in.vPosX * sx, in.vPosY * sy, in.vPosZ * sz};

  // :57 orientationMatrix = transpose(qToMatrix(p.Rotation)); :59 mul(v,orientationMatrix) == rotate
  quat::Quat q{in.pRotX, in.pRotY, in.pRotZ, in.pRotW};
  quat::Vec3 rotated = quat::qRotateVec3(scaled, q);
  // :61 posInObject += float4(p.Position, 0)
  out.posX = rotated.x + in.pPosX;
  out.posY = rotated.y + in.pPosY;
  out.posZ = rotated.z + in.pPosZ;

  // :64 v.Normal = qRotateVec3(v.Normal, p.Rotation) -- no scale/translate
  quat::Vec3 nIn{in.vNormX, in.vNormY, in.vNormZ};
  quat::Vec3 nOut = quat::qRotateVec3(nIn, q);
  out.normX = nOut.x;
  out.normY = nOut.y;
  out.normZ = nOut.z;

  // :67 v.ColorRGB *= p.Color.rgb
  out.colorR = in.vColorR * in.pColorR;
}

}  // namespace mathv_ref
}  // namespace sw
