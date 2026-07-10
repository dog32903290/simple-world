#pragma once
// mathv_ref_depthtolinear — CPU scalar oracle for the depth-to-linear kernel (depth-buffer linearization).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/img/post-fx/depth-to-linear.hlsl — NOT derived from sw's
// app/shaders/computeshaderstage_depthtolinear.metal (intentionally never opened while writing this header).
//   cbuffer ParamConstants { Near,Far,OutrangeMin,OutrangeMax,ClampRange,Mode }  :4-12  -> DepthParams below
//   main(uint3 i)                                                                :13-40  -> depthToLinearTexel()
//     InputTexture.GetDimensions + bound-return                                  :15-18  (caller queries valid texels)
//     depth = InputTexture[i.xy].r                                               :22
//     depth<0 : OutputTexture = (i.x+i.y)%16 > 0 ? 0 : 1                          :24-28  -> the checker special case
//     c = Mode<0.5 ? (-f*n)/(depth*(f-n)-f) : (2n)/(f+n-depth*(f-n))             :30-32
//       (:32 the .hlsl false-branch is `(c = EXPR)` — a redundant self-assign to the very `c` being
//        declared; the ternary result is assigned to c regardless, so it is a NO-OP. Transcribed as EXPR.)
//     if (OutrangeMin!=0 || OutrangeMax!=0) c = (c-OutrangeMin)/(OutrangeMax-OutrangeMin)  :34-37
//     OutputTexture = ClampRange>0.5 ? saturate(c) : c                           :39
// Every statement below carries its own :NN back-reference.
//
// PROVENANCE (GOLDEN_STANDARD.md "P5-safe oracle 判準" / MATH_VERIFY_WORKFLOW.md §1.3): transcribed
// straight from the TiXL HLSL text above, never from any sw `app/shaders/*.metal` port. Zero metal
// include, zero app/shaders/ reference, zero sw math helper. The MSL kernel is a transpiler artifact of
// the SAME HLSL, so ref and kernel share ONE authority (the HLSL) yet neither derives from the other —
// exactly the P5-safe separation the mathv oracles rely on.
//
// FLOAT (not double) on purpose: every op in this kernel runs in float32 on the GPU (the depth divide,
// the optional remap divide, saturate). A double ref would drift from the GPU's fast-math divide at a
// tight ±eps. Constants at their HLSL spelling (2.0, 0.5, 16).
#include <cmath>
#include <cstdint>

namespace mathv_ref {

// :4-12 — the b0 cbuffer, 6 tightly-packed floats (Mode carried as float; <0.5 selects Linear).
struct DepthParams {
  float Near = 0.1f;
  float Far = 1000.0f;
  float OutrangeMin = 0.0f;
  float OutrangeMax = 0.0f;
  float ClampRange = 0.0f;  // >0.5 → saturate
  float Mode = 0.0f;        // <0.5 → Linear, else LegacyDOF
};

// :13-40 — the linearized depth for the texel at (gx,gy) reading `depth` from InputTexture[gx,gy].r.
inline float depthToLinearTexel(float depth, uint32_t gx, uint32_t gy, const DepthParams& p) {
  const float n = p.Near;  // :20
  const float f = p.Far;   // :21

  if (depth < 0.0f) {  // :24
    // :26 — per-texel checker (deterministic function of the thread coords).
    return ((gx + gy) % 16u > 0u) ? 0.0f : 1.0f;
  }

  // :30-32 — Mode<0.5 Linear, else LegacyDOF (the false branch's redundant self-assign is a no-op).
  float c = (p.Mode < 0.5f)
                ? ((-f * n) / (depth * (f - n) - f))
                : ((2.0f * n) / (f + n - depth * (f - n)));

  if (p.OutrangeMin != 0.0f || p.OutrangeMax != 0.0f) {  // :34
    c = (c - p.OutrangeMin) / (p.OutrangeMax - p.OutrangeMin);  // :36
  }

  // :39 — ClampRange>0.5 → saturate (clamp to [0,1]).
  if (p.ClampRange > 0.5f) {
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
  }
  return c;
}

}  // namespace mathv_ref
