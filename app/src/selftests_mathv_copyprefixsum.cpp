// selftests_mathv_copyprefixsum.cpp — --selftest-mathv-copyprefixsum (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-4 transpiler量產批: fuzz the TRANSPILED GPU "copyprefixsum" kernel
// (app/shaders/copyprefixsum.metal — glslang+spirv-cross output of TiXL sort-4-CopyPrefixSum.hlsl,
// §10.1 recipe) against the R-authored CPU oracle (app/src/mathv_ref_copyprefixsum.h) via
// direct-kernel dispatch (§1.3 — NOT buildEvalGraph). EXACT-class op (a single guarded uint copy).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (internal `_` kernel of the DrawPointsDOF compound). BucketCount always >=0 in realistic
// use; dispatch rounds up to a multiple of 64, so tail threads (>=BucketCount) must leave
// BucketOffsetSum untouched — probed via a distinct sentinel pre-fill on BucketOffsetSum (separate
// from BucketPrefixSum's random source data, so a copy-when-should-not-copy bug is directly visible).
//
// injectBug: corrupt the REAL GPU-side BucketPrefixSum[0] (the real source data flowing to the real
// output) -- a genuine "corrupt input" lever (§1.5), unlike sort-1-CleanBucketCounter which had no
// data-carrying input at all. Uses a full XOR bit-flip, not a +1 nudge: EpsSpec::exact()'s rtol=1e-5
// relative gate would silently swallow a +-1 nudge once the uint32-cast-to-float magnitude exceeds
// ~1e5 (rtol*scale > 1) -- the SAME reason this TU also runs a dedicated bit-exact buffer compare
// (below) instead of trusting the float Comparator alone for pass/fail on a pure-copy kernel.
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header (no SwPoint/SwVertex —
// this op is pure uint buffers).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_copyprefixsum.h"
#include "parity_golden_harness.h"
#include "runtime/copyprefixsum_params.h"
#include "runtime/selftest_registry.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::Rng;

struct CpsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  CpsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("copyprefixsum", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~CpsDispatch() { if (pso) pso->release(); }
  CpsDispatch(const CpsDispatch&) = delete;

  bool dispatch(const std::vector<uint32_t>& prefixSum, const std::vector<uint32_t>& offsetSumIn,
                int32_t bucketCount, size_t dispatchedThreads, std::vector<uint32_t>& offsetSumOut) const {
    if (!ok) return false;
    if (dispatchedThreads == 0 || prefixSum.size() != dispatchedThreads ||
        offsetSumIn.size() != dispatchedThreads)
      return false;
    offsetSumOut = offsetSumIn;
    MTL::Buffer* bufPrefix = dev->newBuffer(prefixSum.data(),
                                             (NS::UInteger)(prefixSum.size() * sizeof(uint32_t)),
                                             MTL::ResourceStorageModeShared);
    MTL::Buffer* bufOffset = dev->newBuffer(offsetSumOut.data(),
                                             (NS::UInteger)(offsetSumOut.size() * sizeof(uint32_t)),
                                             MTL::ResourceStorageModeShared);
    CopyPrefixSumParams prm{bucketCount, /*ParticleCount(unread)=*/0};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&prm, sizeof(prm), COPYPREFIXSUM_Params);
    enc->setBuffer(bufOffset, 0, COPYPREFIXSUM_BucketOffsetSum);
    enc->setBuffer(bufPrefix, 0, COPYPREFIXSUM_BucketPrefixSum);
    const uint32_t tg = 64;  // matches numthreads(64,1,1) in the HLSL
    enc->dispatchThreadgroups(MTL::Size::Make((uint32_t)(dispatchedThreads / tg), 1, 1),
                               MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(offsetSumOut.data(), bufOffset->contents(), offsetSumOut.size() * sizeof(uint32_t));
    bufPrefix->release();
    bufOffset->release();
    return true;
  }
};

}  // namespace

int runMathvCopyPrefixSumSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-copyprefixsum] FAIL: no metallib\n"); return 1; }
  CpsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-copyprefixsum] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-copyprefixsum", EpsSpec::exact(), 5);
  bool dispatchOk = true;
  // Dedicated bit-exact buffer compare, ALONGSIDE cmpMain: this kernel is a pure uint32 pass-through
  // (no arithmetic), so a real off-by-one MSL bug should fail the tooth even though it would sit well
  // inside EpsSpec::exact()'s rtol=1e-5 relative gate at these magnitudes (see header note).
  bool exactOk = true;

  struct Scenario { int32_t bucketCount; size_t dispatchedThreads; const char* tag; };
  const Scenario scenarios[] = {
      {0, 64, "n0"},
      {1, 64, "n1"},
      {63, 64, "n63-of-64"},
      {64, 128, "n64-exact-tg"},
      {65, 128, "n65"},
      {4096, 4160, "n4096"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<uint32_t> prefixSum(s.dispatchedThreads);
    for (auto& v : prefixSum) v = (uint32_t)rng.uniform(0.0f, 1e6f);
    std::vector<uint32_t> offsetSumIn(s.dispatchedThreads);
    for (auto& v : offsetSumIn) v = 0xCAFEBABEu ^ (uint32_t)rng.uniform(0.0f, 1e6f);

    // injectBug: corrupt the real dispatch's source data (BucketPrefixSum[0]) while the CPU ref
    // keeps the original -- a real "corrupt input" lever.
    // injectBug: bit-flip (not +1) -- EpsSpec::exact()'s rtol=1e-5 relative gate would swallow a
    // +-1 nudge on these large uint32-cast-to-float magnitudes (rtol*scale >> 1 once scale exceeds
    // ~1e5); a full XOR flip produces a magnitude jump no relative tolerance can absorb, matching
    // how updatechunksizes' index-perturbation lever also produces an uncorrelated (not merely
    // off-by-one) divergence.
    std::vector<uint32_t> prefixSumGpu = prefixSum;
    if (injectBug && !prefixSumGpu.empty()) prefixSumGpu[0] ^= 0xFFFFFFFFu;

    std::vector<uint32_t> gpuOut;
    if (!disp.dispatch(prefixSumGpu, offsetSumIn, s.bucketCount, s.dispatchedThreads, gpuOut)) {
      dispatchOk = false;
      continue;
    }
    std::vector<uint32_t> refOut(s.dispatchedThreads);
    mathv_ref::mathvRefCopyPrefixSum(prefixSum.data(), offsetSumIn.data(), refOut.data(),
                                      s.dispatchedThreads, s.bucketCount);  // ref uses UNPERTURBED source
    for (size_t i = 0; i < s.dispatchedThreads; ++i) {
      float inVec[1] = {(float)i};
      cmpMain.add((float)gpuOut[i], (float)refOut[i], inVec, 1, 0, -1.0f, s.tag);
      if (gpuOut[i] != refOut[i]) {
        exactOk = false;
        printf("[mathv-copyprefixsum] EXACT MISMATCH tag=%s idx=%zu gpu=%u ref=%u\n", s.tag, i,
               gpuOut[i], refOut[i]);
      }
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict() && exactOk;
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "copyprefixsum");

  ParityReport rep("selftest-mathv-copyprefixsum");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("bitExact(uint32 pass-through, no rtol swallow)", exactOk, exactOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1087: transpiler-batch wave-4 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1087, {"mathv-copyprefixsum", runMathvCopyPrefixSumSelfTest});

}  // namespace sw
