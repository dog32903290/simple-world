#pragma once
// mathv_ref_uvsviewer — CPU scalar oracle for TiXL mesh/fx/mesh-UVs.hlsl (owner op: UVsViewer, a
// debug-visualization mesh op; owner: external/tixl/Operators/Lib/mesh/_/UVsViewer.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/fx/mesh-UVs.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/uvsviewer.metal intentionally never opened while writing this file).
//   cbuffer Params (BlendFactor/SwitchUV) :6-9
//   main() body                            :16-40
//
// NOTED-QUIRKS, FAITHFULLY REPRODUCED (MATH_VERIFY_WORKFLOW.md §10.3 — transcription TARGET, not a
// fork):
//   - ColorRGB is NEVER assigned by the HLSL body (only 6 of 8 PbrVertex fields are written: Position/
//     Normal/Tangent/Bitangent/TexCoord/Selected). This ref only models those 6; the fuzz TU asserts
//     ColorRGB stays whatever the destination held pre-dispatch (same shape as flipnormals'
//     TexCoord2-untouched tooth).
//   - TexCoord2 is likewise never assigned (only ever READ, when SwitchUV>0.5). Same untouched-tooth
//     treatment as ColorRGB.
//   - `float t = i.x / (float)resultCount;` (.hlsl:32) is a genuinely DEAD local in the HLSL itself —
//     computed, never read again. Correctly absent from this ref (and was dead-code-eliminated by
//     spirv-cross in the raw transpile too).
//   - The dispatch guard is `i.x > resultCount` (STRICT greater, not `>=`) — an off-by-one NAMED FORK
//     in the boundary-index handling, see uvsviewer_params.h header for the full analysis. This ref
//     function itself has NO guard (mirrors flipnormals_ref/updateChunkSizesOne: the guard is the
//     caller's job, this models the PER-ELEMENT math only) — the fuzz TU is responsible for deciding
//     which indices to feed it (0..Count inclusive when Count%64!=0, matching the kernel's actual
//     off-by-one processing of index==Count in that case).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
namespace sw {
namespace mathv_ref {

struct UvsViewerIn {
  float posX, posY, posZ;
  float normX, normY, normZ;
  float tanX, tanY, tanZ;
  float bitanX, bitanY, bitanZ;
  float texU, texV;
  float tex2U, tex2V;
};
struct UvsViewerOut {
  float posX, posY, posZ;
  float normX, normY, normZ;
  float tanX, tanY, tanZ;
  float bitanX, bitanY, bitanZ;
  float texU, texV;  // passthrough (== in.texU/texV)
};

inline float lerp1(float a, float b, float f) { return a + f * (b - a); }

// uvsViewerOne — HLSL main():34-46 (excluding the dead `t` local and the guard).
inline void uvsViewerOne(const UvsViewerIn& in, float blendFactor, float switchUv, UvsViewerOut& out) {
  const float f = blendFactor;
  if (switchUv > 0.5f) {
    out.posX = lerp1(in.posX, in.tex2U, f);
    out.posY = lerp1(in.posY, in.tex2V, f);
    out.posZ = lerp1(in.posZ, 0.0f, f);
  } else {
    out.posX = lerp1(in.posX, in.texU, f);
    out.posY = lerp1(in.posY, in.texV, f);
    out.posZ = lerp1(in.posZ, 0.0f, f);
  }
  out.normX = lerp1(in.normX, 0.0f, f);
  out.normY = lerp1(in.normY, 0.0f, f);
  out.normZ = lerp1(in.normZ, 1.0f, f);
  out.tanX = lerp1(in.tanX, 1.0f, f);
  out.tanY = lerp1(in.tanY, 0.0f, f);
  out.tanZ = lerp1(in.tanZ, 0.0f, f);
  out.bitanX = lerp1(in.bitanX, 0.0f, f);
  out.bitanY = lerp1(in.bitanY, 1.0f, f);
  out.bitanZ = lerp1(in.bitanZ, 0.0f, f);
  out.texU = in.texU;
  out.texV = in.texV;
}

}  // namespace mathv_ref
}  // namespace sw
