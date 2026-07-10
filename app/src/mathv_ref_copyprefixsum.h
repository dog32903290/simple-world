#pragma once
// mathv_ref_copyprefixsum — CPU scalar oracle for TiXL points/draw-sorted/sort-4-CopyPrefixSum
// (points island; owner: external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3, ONE of six
// numbered sort-N passes that compound dispatches — only this pass is ported here).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/draw-sorted/sort-4-CopyPrefixSum.hlsl — NOT derived from sw's
// MSL kernel (app/shaders/copyprefixsum.metal intentionally never opened while writing this file).
//   cbuffer Params (BucketCount/ParticleCount) :4-8
//   CopyPrefixSum() body                        :12-17
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cstdint>
#include <vector>

namespace sw {
namespace mathv_ref {

// copyPrefixSumOne — HLSL CopyPrefixSum():15-16.
//   if (threadIdx < BucketCount) BucketOffsetSum[threadIdx] = BucketPrefixSum[threadIdx];
inline uint32_t copyPrefixSumOne(uint32_t threadIdx, int32_t bucketCount, uint32_t prefixSumVal,
                                  uint32_t offsetSumIn) {
  if (threadIdx < (uint32_t)bucketCount) return prefixSumVal;
  return offsetSumIn;
}

// mathvRefCopyPrefixSum — full-buffer CPU oracle over `dispatchedThreads` threads (padded up to a
// multiple of 64, matching the real dispatch's threadgroup rounding). `prefixSum` is read-only here
// (mirrors the HLSL's "read-only during write pass" comment); `offsetSumOut` receives the copy (may
// alias a caller-provided sentinel-prefilled buffer to probe the untouched-tail branch).
inline void mathvRefCopyPrefixSum(const uint32_t* prefixSum, const uint32_t* offsetSumIn,
                                   uint32_t* offsetSumOut, size_t dispatchedThreads,
                                   int32_t bucketCount) {
  for (size_t i = 0; i < dispatchedThreads; ++i) {
    offsetSumOut[i] = copyPrefixSumOne((uint32_t)i, bucketCount, prefixSum[i], offsetSumIn[i]);
  }
}

}  // namespace mathv_ref
}  // namespace sw
