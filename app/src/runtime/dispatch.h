#pragma once
#include <cstdint>

namespace sw {
// Threadgroup count for a 1D compute dispatch, over-dispatch-by-one.
// Ported from the old COMMAND_STREAM_CONTRACT golden:
//   calc(64,16)=5, calc(63,16)=4, calc(64,0)=0, calc(960,16)=61, calc(540,16)=34.
// The "+1" intentionally over-covers; every kernel MUST guard `if (tid >= count) return`.
inline uint32_t calcDispatchCount(uint32_t total, uint32_t group) {
  return group == 0 ? 0u : total / group + 1u;
}

// Pure-arithmetic self-test of calcDispatchCount against the contract golden.
int runDispatchSelfTest();
}  // namespace sw
