#pragma once
// mathv_ref_flipnormals — CPU scalar oracle for TiXL mesh-FlipNormals (3d/mesh, vertex-buffer utility;
// owner: external/tixl/Operators/Lib/mesh/modify/FlipNormals.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-FlipNormals.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/flipnormals.metal intentionally never opened while writing this file).
//   cbuffer Params (EMPTY)          :3-6
//   main() body                     :13-28
//
// NOTED-QUIRK, FAITHFULLY REPRODUCED (MATH_VERIFY_WORKFLOW.md §10.3 — transcription TARGET, not a
// fork): HLSL:21-27 assigns 7 of 8 PbrVertex fields — TexCoord2 is never written. This ref therefore
// only models Position/Normal (the two fields the fuzz TU asserts: Position passthrough, Normal
// negated) — Tangent/Bitangent/TexCoord/Selected/ColorRGB are straight passthrough (not separately
// modeled; asserting Normal's negation + Position's passthrough already exercises the kernel's only
// non-trivial arithmetic) and TexCoord2 is asserted UNTOUCHED by the fuzz TU directly (bug-invariant
// tooth, not part of this ref).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
namespace sw {
namespace mathv_ref {

struct FlipNormalsIn {
  float posX, posY, posZ;
  float normX, normY, normZ;
};
struct FlipNormalsOut {
  float posX, posY, posZ;
  float normX, normY, normZ;
};

// flipNormalsOne — HLSL main():21 (Position passthrough) + :22 (Normal negated).
inline void flipNormalsOne(const FlipNormalsIn& in, FlipNormalsOut& out) {
  out.posX = in.posX;
  out.posY = in.posY;
  out.posZ = in.posZ;
  out.normX = -in.normX;
  out.normY = -in.normY;
  out.normZ = -in.normZ;
}

}  // namespace mathv_ref
}  // namespace sw
