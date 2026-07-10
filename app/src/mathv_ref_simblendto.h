#pragma once
// mathv_ref_simblendto — CPU scalar oracle for TiXL SimBlendTo (points/sim/experimental).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/combine/SimBlendTo.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/simblendto.metal intentionally never opened while writing this file).
//   cbuffer Params (BlendFactor,PairingMethod,CountA,CountB) :5-11
//   main() body                                              :23-35
//
// KNOWN TiXL SHIPPED BUG, FAITHFULLY REPRODUCED (MATH_VERIFY_WORKFLOW.md §10.3 — this is sw's
// transcription TARGET, not a fork to guard against): SimBlendTo.hlsl:30-31 —
//   float wA = ResultPoints[i.x].W;      // :30
//   float wB = ResultPoints[i.x].W;      // :31 -- NOT PointsB[i.x].W (hand-slip; PointsB.W is dead)
// wA and wB are therefore bit-identical on every call, so W_out = lerp(wA,wB,t) == wA for every
// BlendFactor, independent of PointsB's actual W. This oracle reproduces that literally — posB's W
// is intentionally not even part of SimBlendToIn below (the kernel never reads it).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cmath>

namespace sw {
namespace mathv_ref {

struct SimBlendToParams {
  float blendFactor;
  // PairingMethod/CountA/CountB: declared in the TiXL cbuffer but NEVER READ by main() — dead
  // fields, not modeled (mirrors runtime/simblendto_params.h's ABI-completeness note).
};

// One point's inputs: A-side comes from ResultPoints (read AND write target, in-place), B-side from
// PointsB (SRV). HLSL:28-29 read Position from both buffers; :30-31 read W from ResultPoints TWICE
// (the bug above) — so posB's W is deliberately absent from this struct, matching the kernel exactly.
struct SimBlendToIn {
  float posA[3];
  float wA;
  float posB[3];
};
struct SimBlendToOut {
  float pos[3];
  float w;
};

// simBlendToOne — HLSL main():28-34 (Position/W only; Rotation/Color/Stretch/Selected are untouched
// passthrough — not modeled here, see the fuzz TU's SCOPE note for why that's in-bounds).
inline void simBlendToOne(const SimBlendToIn& in, SimBlendToOut& out, const SimBlendToParams& p) {
  const float t = p.blendFactor;
  // :33 ResultPoints[i.x].Position = lerp(posA, posB, BlendFactor); HLSL lerp(a,b,t) == a+(b-a)*t.
  for (int k = 0; k < 3; ++k) out.pos[k] = in.posA[k] + (in.posB[k] - in.posA[k]) * t;
  // :34 lerp(wA, wB, BlendFactor) with wB==wA bit-identical (the bug) — collapses to wA regardless of t.
  out.w = in.wA;
}

}  // namespace mathv_ref
}  // namespace sw
