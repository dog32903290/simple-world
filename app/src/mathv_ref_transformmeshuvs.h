#pragma once
// mathv_ref_transformmeshuvs — CPU scalar oracle for TiXL mesh/modify/mesh-TransformUVs.hlsl (owner
// op: TransformMeshUVs; owner: external/tixl/Operators/Lib/mesh/modify/TransformMeshUVs.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-TransformUVs.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/transformmeshuvs.metal intentionally never opened while writing this file).
//   cbuffer Params (TransformMatrix/UseVertexSelection/ToTexCoord2) :6-10
//   main() body                                                      :16-33
//
// EXACT class (affine matrix-vector transform + lerp/select, no transcendentals).
//
// MATRIX CONVENTION: this ref implements `mul(v,M).xyz` per HLSL semantics EXACTLY — out[j] =
// sum_i v[i]*M[i][j] where M[i][j] is row i, column j of the SAME matrix the kernel receives.
// mulRowMajorPoint below takes the row-major-flattened m[0..15] (m[r*4+c] = row r, col c) and
// computes the mathematically EQUIVALENT `M·v` (column-vector) form -- for a row-major M this equals
// HLSL's `mul(v,M)` exactly (see transformmeshuvs_params.h header ALGEBRA note for the full derivation
// of why the GPU adapter's raw m[] upload reproduces this same result through spirv-cross's `v *
// float4x4` operator). Translation lives in column 3 (m[3]/m[7]/m[11]).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
namespace sw {
namespace mathv_ref {

// mulRowMajorPoint — out[r] = sum_c m[r*4+c]*v[c], v=(x,y,z,w). See header MATRIX CONVENTION note.
inline void mulRowMajorPoint(const float m[16], float x, float y, float z, float w, float& outX,
                              float& outY, float& outZ) {
  outX = m[0] * x + m[1] * y + m[2] * z + m[3] * w;
  outY = m[4] * x + m[5] * y + m[6] * z + m[7] * w;
  outZ = m[8] * x + m[9] * y + m[10] * z + m[11] * w;
}

inline float lerp1(float a, float b, float f) { return a + f * (b - a); }

struct TransformMeshUvsIn {
  float texU, texV, tex2U, tex2V, selected;
};
struct TransformMeshUvsOut {
  float texU, texV, tex2U, tex2V;  // whichever pair the branch didn't touch is unchanged passthrough
};

// transformMeshUvsOne — HLSL main():21-31.
inline void transformMeshUvsOne(const TransformMeshUvsIn& in, const float m[16], float useVertexSelection,
                                 float toTexCoord2, TransformMeshUvsOut& out) {
  const float s = useVertexSelection > 0.5f ? in.selected : 1.0f;
  out.texU = in.texU;
  out.texV = in.texV;
  out.tex2U = in.tex2U;
  out.tex2V = in.tex2V;
  if (toTexCoord2 != 0.0f) {
    float tx, ty, tz;
    mulRowMajorPoint(m, in.tex2U, in.tex2V, 0.0f, 1.0f, tx, ty, tz);
    out.tex2U = lerp1(in.tex2U, tx, s);
    out.tex2V = lerp1(in.tex2V, ty, s);
  } else {
    float tx, ty, tz;
    mulRowMajorPoint(m, in.texU, in.texV, 0.0f, 1.0f, tx, ty, tz);
    out.texU = lerp1(in.texU, tx, s);
    out.texV = lerp1(in.texV, ty, s);
  }
}

}  // namespace mathv_ref
}  // namespace sw
