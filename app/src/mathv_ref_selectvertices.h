#pragma once
// mathv_ref_selectvertices — CPU scalar oracle for TiXL mesh/modify/mesh-SelectVertices.hlsl (owner
// op: SelectVertices; owner: external/tixl/Operators/Lib/mesh/modify/SelectVertices.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-SelectVertices.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/selectvertices.metal intentionally never opened while writing this file), EXCEPT the
// `snoise` sub-computation which REUSES app/src/tixl_noise_oracle.h (the established shared P5-safe
// oracle, §1.4 — same one AddNoise's ref uses; TRANSCRIBED independently there from
// shared/noise-functions.hlsl, not from any sw kernel).
//   cbuffer Params            :8-18
//   main() body               :37-101
//
// TRANSCENDENTAL+BRANCHY class (length/smoothstep/snoise + 10 threshold branches across two
// independent 5-way chains). MATRIX CONVENTION: same row-major/M·v as transformmeshuvs_params.h.
// `mod(x,y)` floored per selectvertices_params.h header note.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper OTHER than the shared tixl_noise_oracle.h (itself P5-safe, §1.4
// sanctioned reuse) — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <algorithm>
#include <cmath>

#include "tixl_noise_oracle.h"

namespace sw {
namespace mathv_ref {

inline float smoothstepHlsl(float edge0, float edge1, float x) {
  float t = (x - edge0) / (edge1 - edge0);
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return t * t * (3.0f - 2.0f * t);
}
inline float modFloored(float x, float y) { return x - y * std::floor(x / y); }
inline float saturateF(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

struct SelectVerticesIn {
  float posX, posY, posZ, selected;
};
struct SelectVerticesParamsR {
  float m[16];
  float fallOff, volumeShape, selectMode, clampResult, strength, phase, threshold;
};

// Shared row-major point-transform helper (SAME convention as transformmeshuvs_params.h's
// mulRowMajorPoint; duplicated here in mathv_ref namespace scope rather than #include-shared since
// mathv_ref_transformmeshuvs.h is its own standalone R-authored TU, not a shared oracle header).
inline void mulRowMajorPoint(const float m[16], float x, float y, float z, float w, float& outX,
                              float& outY, float& outZ) {
  outX = m[0] * x + m[1] * y + m[2] * z + m[3] * w;
  outY = m[4] * x + m[5] * y + m[6] * z + m[7] * w;
  outZ = m[8] * x + m[9] * y + m[10] * z + m[11] * w;
}

// selectVerticesOne — HLSL main():39-99. Returns the new Selected value; ALL OTHER PbrVertex fields
// are exact passthrough (ResultVertices[i] = SourceVertices[i] wholesale, only .Selected overwritten).
inline float selectVerticesOne(const SelectVerticesIn& in, const SelectVerticesParamsR& P) {
  float px, py, pz;
  mulRowMajorPoint(P.m, in.posX, in.posY, in.posZ, 1.0f, px, py, pz);  // reuses transformmeshuvs' helper shape

  float s;
  if (P.volumeShape < 0.5f) {
    float distance = std::sqrt(px * px + py * py + pz * pz);
    s = smoothstepHlsl(1.0f + P.fallOff, 1.0f, distance);
  } else if (P.volumeShape < 1.5f) {
    float tx = std::fabs(px), ty = std::fabs(py), tz = std::fabs(pz);
    float distance = std::max(std::max(tx, ty), tz) + P.phase;
    s = smoothstepHlsl(1.0f + P.fallOff, 1.0f, distance);
  } else if (P.volumeShape < 2.5f) {
    float distance = py;
    s = smoothstepHlsl(P.fallOff, 0.0f, distance);
  } else if (P.volumeShape < 3.5f) {
    float distance = 1.0f - std::fabs(modFloored(py * 1.0f + P.phase, 2.0f) - 1.0f);
    s = smoothstepHlsl(P.threshold + 0.5f + P.fallOff, P.threshold + 0.5f, distance);
  } else if (P.volumeShape < 4.5f) {
    tixl_noise::V3 noiseLookup{px * 0.91f + P.phase, py * 0.91f + P.phase, pz * 0.91f + P.phase};
    float noise = tixl_noise::snoise(noiseLookup);
    s = smoothstepHlsl(P.threshold + P.fallOff, P.threshold, noise);
  } else {
    s = 1.0f;  // no branch taken -- `s` keeps its HLSL:47 initializer value
  }

  float result;
  if (P.selectMode < 0.5f) {
    result = s * P.strength;
  } else if (P.selectMode < 1.5f) {
    result = s + in.selected * P.strength;
  } else if (P.selectMode < 2.5f) {
    result = in.selected - s * P.strength;
  } else if (P.selectMode < 3.5f) {
    result = in.selected + P.strength * (in.selected * s - in.selected);  // lerp(sel, sel*s, strength)
  } else if (P.selectMode < 4.5f) {
    result = s * (1.0f - in.selected);
  } else {
    result = s;  // no branch taken -- `s` unchanged
  }

  return P.clampResult < 0.5f ? result : saturateF(result);
}

}  // namespace mathv_ref
}  // namespace sw
