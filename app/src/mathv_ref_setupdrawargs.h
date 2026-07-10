#pragma once
// mathv_ref_setupdrawargs — CPU scalar oracle for TiXL points/draw-sorted/sort-6-SetupDrawArgs
// (points island; owner: external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3, the LAST of six
// numbered sort-N passes that compound dispatches — only this pass is ported here).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/draw-sorted/sort-6-SetupDrawArgs.hlsl — NOT derived from sw's
// MSL kernel (app/shaders/setupdrawargs.metal intentionally never opened while writing this file).
//   cbuffer Params (BucketCount/ParticleCount) :5-9
//   SetupDrawArgs() body                        :14-22
//
// numthreads(1,1,1): this kernel runs as a SINGLE invocation (no per-thread index, no dispatch
// guard) — the CPU ref below models exactly that: one call, five fixed output slots.
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <array>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// setupDrawArgsOnce — HLSL SetupDrawArgs():17-22.
//   totalCount = BucketPrefixSum[BucketCount-1] + BucketCounter[BucketCount-1];
//   DrawArgsBuffer = {totalCount*6, 1, 0, 0, 0}
// uint32_t arithmetic wraps mod 2^32 identically to HLSL's uint (matches AddNoise/UpdateChunkSizes
// precedent for uint overflow semantics -- both sides use the SAME two's-complement wraparound).
inline void setupDrawArgsOnce(uint32_t bucketPrefixSumLast, uint32_t bucketCounterLast,
                               std::array<uint32_t, 5>& out) {
  uint32_t totalCount = bucketPrefixSumLast + bucketCounterLast;  // :17 (uint wraps on overflow)
  out[0] = totalCount * 6u;  // :18 Vertex count (6 per quad), also wraps
  out[1] = 1u;               // :19 Instance count
  out[2] = 0u;               // :20
  out[3] = 0u;               // :21
  out[4] = 0u;               // :22
}

}  // namespace mathv_ref
}  // namespace sw
