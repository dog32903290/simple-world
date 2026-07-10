#pragma once
// mathv_ref_appendpoints — CPU scalar oracle for TiXL _AppendPoints (points/_internal glue op).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/combine/AppendPoints.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/appendpoints.metal intentionally never opened while writing this file).
//   cbuffer Params (CountA,CountB) :3-7
//   main() body                    :18-41
//
// SEMANTICS (faithfully transcribed, including the NaN-boundary-marker convention already documented
// at app/shaders/draw_lines.metal:12): concatenates Points1[0..countA) + Points2[0..CountB) into
// ResultPoints, where countA = (uint)(CountA+1.5) (AppendPoints.hlsl:21 — NOT a plain round(); this
// literal truncating-add-1.5 form is transcribed exactly, not "corrected" to round-half-up). A NaN is
// stamped into .W at the LAST index of each source bag (the "point-bag break" marker), and any thread
// past the combined bag length (i.x > CountA+CountB) writes ONLY .W=NaN and returns — leaving every
// other field at whatever was already in the (in-place UAV) buffer before dispatch, which is why
// `before` below is a real ref input, not a computed default.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cmath>
#include <cstdint>

namespace sw {
namespace mathv_ref {

struct AppendPointsParams {
  float countA;  // AppendPoints.hlsl :5 CountA (float; internally-derived element count)
  float countB;  // AppendPoints.hlsl :6 CountB (ditto)
};

// One point's fields (Position+W is all this batch asserts — see fuzz TU SCOPE note for why the
// remaining fields, which the kernel struct-copies verbatim from Points1/Points2, are out of scope).
struct AppendPointsPoint {
  float pos[3];
  float w;
};

// appendPointsOne — HLSL main():18-41 for ONE dispatch index `idx`.
//   before      = ResultPoints[idx] BEFORE this dispatch (in-place UAV — needed for the OOB branch,
//                 which only ever touches .W; see file header).
//   points1/points1N, points2/points2N = the two SRV source arrays + their lengths (host-side bound
//                 check only — this ref never does an OOB read; a real GPU dispatch relies on the
//                 caller sizing buffers generously, see the fuzz TU's SAFETY note).
inline void appendPointsOne(uint32_t idx, const AppendPointsPoint& before,
                            const AppendPointsPoint* points1, uint32_t points1N,
                            const AppendPointsPoint* points2, uint32_t points2N,
                            const AppendPointsParams& p, AppendPointsPoint& out) {
  const uint32_t countA = (uint32_t)(p.countA + 1.5f);  // :21 uint(CountA + 1.5) -- literal, not round()
  out = before;                                          // in-place UAV default: untouched unless written
  // :24 if (i.x > CountA + CountB) { W = NaN; return; } -- uint->float compare, HLSL implicit conversion.
  if ((float)idx > (p.countA + p.countB)) {
    out.w = std::nanf("");
    return;
  }
  if (idx < countA) {                                    // :29 useFirst branch
    if (idx < points1N) { out.pos[0] = points1[idx].pos[0]; out.pos[1] = points1[idx].pos[1];
                          out.pos[2] = points1[idx].pos[2]; out.w = points1[idx].w; }
    if (idx == countA - 1u) out.w = std::nanf("");        // :31-33 bag-end marker (overrides the copy)
  } else {                                                 // :35 Points2 branch
    const uint32_t j = idx - countA;
    if (j < points2N) { out.pos[0] = points2[j].pos[0]; out.pos[1] = points2[j].pos[1];
                        out.pos[2] = points2[j].pos[2]; out.w = points2[j].w; }
    // :37-39 the SECOND bag-end marker: idx == countA + uint(CountB + 0.5).
    if (idx == countA + (uint32_t)(p.countB + 0.5f)) out.w = std::nanf("");
  }
}

}  // namespace mathv_ref
}  // namespace sw
