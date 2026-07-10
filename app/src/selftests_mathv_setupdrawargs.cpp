// selftests_mathv_setupdrawargs.cpp — --selftest-mathv-setupdrawargs (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-4 transpiler量產批: fuzz the TRANSPILED GPU "setupdrawargs" kernel
// (app/shaders/setupdrawargs.metal — glslang+spirv-cross output of TiXL sort-6-SetupDrawArgs.hlsl,
// §10.1 recipe) against the R-authored CPU oracle (app/src/mathv_ref_setupdrawargs.h) via
// direct-kernel dispatch (§1.3 — NOT buildEvalGraph). EXACT-class op (uint add/mul, no floats).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (internal `_` kernel of the DrawPointsDOF compound). numthreads(1,1,1) — dispatched with
// EXACTLY one threadgroup of size (1,1,1), no per-element sweep. BucketCount>=1 in every scenario
// (BucketCount-1 indexes the last bucket; BucketCount==0 would read index -1, a genuine HLSL
// out-of-declared-range access that the source itself never guards against — out of scope here, not
// this kernel's math, same class of AMBIGUITY the workflow leaves unprobed elsewhere).
//
// injectBug: corrupt the REAL GPU-side BucketPrefixSum[BucketCount-1] (the exact element this
// single-thread kernel reads) while the CPU ref keeps the original — real "corrupt input" lever.
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header (pure uint buffers).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_setupdrawargs.h"
#include "parity_golden_harness.h"
#include "runtime/selftest_registry.h"
#include "runtime/setupdrawargs_params.h"

#include <array>
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

struct SdaDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  SdaDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("setupdrawargs", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SdaDispatch() { if (pso) pso->release(); }
  SdaDispatch(const SdaDispatch&) = delete;

  bool dispatch(const std::vector<uint32_t>& bucketPrefixSum, const std::vector<uint32_t>& bucketCounter,
                int32_t bucketCount, std::array<uint32_t, 5>& drawArgsOut) const {
    if (!ok) return false;
    if (bucketCount <= 0 || (size_t)bucketCount > bucketPrefixSum.size() ||
        (size_t)bucketCount > bucketCounter.size())
      return false;
    drawArgsOut = {0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu};
    MTL::Buffer* bufPrefix = dev->newBuffer(bucketPrefixSum.data(),
                                             (NS::UInteger)(bucketPrefixSum.size() * sizeof(uint32_t)),
                                             MTL::ResourceStorageModeShared);
    MTL::Buffer* bufCounter = dev->newBuffer(bucketCounter.data(),
                                              (NS::UInteger)(bucketCounter.size() * sizeof(uint32_t)),
                                              MTL::ResourceStorageModeShared);
    MTL::Buffer* bufArgs = dev->newBuffer(drawArgsOut.data(), (NS::UInteger)(drawArgsOut.size() * sizeof(uint32_t)),
                                           MTL::ResourceStorageModeShared);
    SetupDrawArgsParams prm{bucketCount, /*ParticleCount(unread)=*/0};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufPrefix, 0, SETUPDRAWARGS_BucketPrefixSum);
    enc->setBytes(&prm, sizeof(prm), SETUPDRAWARGS_Params);
    enc->setBuffer(bufCounter, 0, SETUPDRAWARGS_BucketCounter);
    enc->setBuffer(bufArgs, 0, SETUPDRAWARGS_DrawArgsBuffer);
    enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(1, 1, 1));  // numthreads(1,1,1)
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(drawArgsOut.data(), bufArgs->contents(), drawArgsOut.size() * sizeof(uint32_t));
    bufPrefix->release();
    bufCounter->release();
    bufArgs->release();
    return true;
  }
};

}  // namespace

int runMathvSetupDrawArgsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-setupdrawargs] FAIL: no metallib\n"); return 1; }
  SdaDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-setupdrawargs] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-setupdrawargs", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  // Values kept comfortably inside float32's exact-integer range (2^24) so the Comparator's float
  // cast below never loses precision (updatechunksizes precedent) -- the near-2^32 overflow domain
  // is covered separately by the dedicated exact-uint32 boundary check after this loop.
  struct Scenario { int32_t bucketCount; uint32_t prefixLast; uint32_t counterLast; const char* tag; };
  const Scenario scenarios[] = {
      {1, 0u, 0u, "n1-zero"},
      {1, 100u, 7u, "n1"},
      {64, 4096u, 128u, "n64"},
      {4096, 1000000u, 42u, "n4096"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<uint32_t> prefixSum((size_t)s.bucketCount);
    for (auto& v : prefixSum) v = (uint32_t)rng.uniform(0.0f, 1e6f);
    prefixSum.back() = s.prefixLast;
    std::vector<uint32_t> counter((size_t)s.bucketCount);
    for (auto& v : counter) v = (uint32_t)rng.uniform(0.0f, 1e6f);
    counter.back() = s.counterLast;

    // injectBug: corrupt the exact element the kernel reads (real dispatch-path lever).
    std::vector<uint32_t> prefixSumGpu = prefixSum;
    if (injectBug) prefixSumGpu.back() += 1u;

    std::array<uint32_t, 5> gpuOut{};
    if (!disp.dispatch(prefixSumGpu, counter, s.bucketCount, gpuOut)) {
      dispatchOk = false;
      continue;
    }
    std::array<uint32_t, 5> refOut{};
    mathv_ref::setupDrawArgsOnce(s.prefixLast, s.counterLast, refOut);  // ref uses UNPERTURBED source
    for (int lane = 0; lane < 5; ++lane) {
      float inVec[2] = {(float)s.prefixLast, (float)s.counterLast};
      cmpMain.add((float)gpuOut[(size_t)lane], (float)refOut[(size_t)lane], inVec, 2, lane, -1.0f, s.tag);
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "setupdrawargs");

  // Dedicated exact-uint32 boundary check (NOT float-based -- near-2^32 values would silently lose
  // precision through Comparator's float cast, same reasoning as updatechunksizes' FaceCount
  // boundary check): (prefixLast+counterLast)*6 must wrap mod 2^32 bit-identically on both sides.
  bool boundaryOk = true;
  {
    struct OverflowCase { uint32_t prefixLast, counterLast; const char* tag; };
    const OverflowCase cases[] = {
        {0xFFFFFFFFu, 5u, "overflow-add"},                // sum wraps past UINT32_MAX
        {0x2AAAAAAAu, 0x2AAAAAAAu, "overflow-mul6"},       // sum fits, but sum*6 overflows
    };
    for (const auto& oc : cases) {
      const int32_t bucketCount = 8;
      std::vector<uint32_t> prefixSum((size_t)bucketCount, 0u);
      prefixSum.back() = oc.prefixLast;
      std::vector<uint32_t> counter((size_t)bucketCount, 0u);
      counter.back() = oc.counterLast;
      std::array<uint32_t, 5> gpuOut{};
      if (!disp.dispatch(prefixSum, counter, bucketCount, gpuOut)) { boundaryOk = false; continue; }
      std::array<uint32_t, 5> refOut{};
      mathv_ref::setupDrawArgsOnce(oc.prefixLast, oc.counterLast, refOut);
      if (gpuOut != refOut) {
        boundaryOk = false;
        printf("[mathv-setupdrawargs] BOUNDARY MISMATCH tag=%s gpu=(%u,%u,%u,%u,%u) ref=(%u,%u,%u,%u,%u)\n",
               oc.tag, gpuOut[0], gpuOut[1], gpuOut[2], gpuOut[3], gpuOut[4], refOut[0], refOut[1],
               refOut[2], refOut[3], refOut[4]);
      }
    }
  }

  ParityReport rep("selftest-mathv-setupdrawargs");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("uint32Wrap(boundary: overflow-add/overflow-mul6)", boundaryOk, boundaryOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1088: transpiler-batch wave-4 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1088, {"mathv-setupdrawargs", runMathvSetupDrawArgsSelfTest});

}  // namespace sw
