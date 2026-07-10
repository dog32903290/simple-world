#pragma once
// mathv_ref_cleanbucketcounter — CPU scalar oracle for TiXL points/draw-sorted/sort-1-
// CleanBucketCounter (points island; owner: external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3,
// ONE of six numbered sort-N passes that compound dispatches — only this pass is ported here).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/draw-sorted/sort-1-CleanBucketCounter.hlsl — NOT derived from
// sw's MSL kernel (app/shaders/cleanbucketcounter.metal intentionally never opened while writing
// this file).
//   cbuffer Params (BucketCount/ParticleCount) :3-7
//   ClearBucketCounter() body                   :12-17
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cstdint>
#include <vector>

namespace sw {
namespace mathv_ref {

// cleanBucketCounterOne — HLSL ClearBucketCounter():15-16.
//   if (threadIdx < BucketCount) BucketCounter[threadIdx] = 0;
// `out` reflects the buffer contents AFTER this single thread's write (or unchanged, `in`, if the
// guard doesn't fire) — mirrors the GPU's per-thread branch exactly.
inline uint32_t cleanBucketCounterOne(uint32_t threadIdx, int32_t bucketCount, uint32_t in) {
  if (threadIdx < (uint32_t)bucketCount) return 0u;
  return in;
}

// mathvRefCleanBucketCounter — full-buffer CPU oracle over `dispatchedThreads` threads (typically
// padded up to a multiple of 64 by the caller, matching the real dispatch's threadgroup rounding).
// `in`/`out` may be the SAME array (in-place clear); each element is independent (no cross-thread
// dependency), so aliasing is always safe here.
inline void mathvRefCleanBucketCounter(const uint32_t* in, uint32_t* out, size_t dispatchedThreads,
                                        int32_t bucketCount) {
  for (size_t i = 0; i < dispatchedThreads; ++i) {
    out[i] = cleanBucketCounterOne((uint32_t)i, bucketCount, in[i]);
  }
}

}  // namespace mathv_ref
}  // namespace sw
