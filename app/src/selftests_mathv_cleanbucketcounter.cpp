// selftests_mathv_cleanbucketcounter.cpp — --selftest-mathv-cleanbucketcounter (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-4 transpiler量產批: fuzz the TRANSPILED GPU "cleanbucketcounter"
// kernel (app/shaders/cleanbucketcounter.metal — glslang+spirv-cross output of TiXL sort-1-
// CleanBucketCounter.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_cleanbucketcounter.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (a single guarded constant-write; no floats at all).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (internal `_` kernel of the DrawPointsDOF compound, no user-facing node of its own).
// BucketCount is a count computed by the compound's cook loop, always >=0 in realistic use; the
// dispatch always rounds up to a multiple of 64 threadgroup-size (matching HLSL numthreads(64,1,1)),
// so threads with index >= BucketCount legitimately exist and must leave their slot untouched — the
// buffer is pre-filled with a sentinel pattern so "untouched" is directly observable, not just
// "happens to already be zero".
//
// injectBug: this kernel has NO input data buffer to corrupt (it only ever writes a hardcoded 0) — a
// float-param nudge (§1.5's usual lever) is also structurally unavailable (BucketCount/ParticleCount
// are ints, and ParticleCount is unread). The real dispatch-path lever here is BucketCount itself:
// bumping the value sent to the GPU changes WHICH indices get cleared, corrupting the real dispatch
// while the CPU ref keeps computing against the original (unperturbed) BucketCount — this diverges at
// the boundary index deterministically whenever BucketCount < dispatchedThreads (guaranteed by the
// scenario table below, which always leaves headroom past BucketCount).
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header (no SwPoint/SwVertex —
// this op is pure uint buffers).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_cleanbucketcounter.h"
#include "parity_golden_harness.h"
#include "runtime/cleanbucketcounter_params.h"
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

struct CbcDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  CbcDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("cleanbucketcounter", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~CbcDispatch() { if (pso) pso->release(); }
  CbcDispatch(const CbcDispatch&) = delete;

  // `dispatchedThreads` = the buffer's element count = ceil(bucketCountForDispatch/64)*64, matching
  // the real numthreads(64,1,1) dispatch's threadgroup rounding.
  bool dispatch(const std::vector<uint32_t>& in, int32_t bucketCount, size_t dispatchedThreads,
                std::vector<uint32_t>& out) const {
    if (!ok) return false;
    if (dispatchedThreads == 0 || in.size() != dispatchedThreads) return false;
    out = in;
    MTL::Buffer* buf = dev->newBuffer(out.data(), (NS::UInteger)(out.size() * sizeof(uint32_t)),
                                       MTL::ResourceStorageModeShared);
    CleanBucketCounterParams prm{bucketCount, /*ParticleCount(unread)=*/0};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&prm, sizeof(prm), CLEANBUCKETCOUNTER_Params);
    enc->setBuffer(buf, 0, CLEANBUCKETCOUNTER_BucketCounter);
    const uint32_t tg = 64;  // matches numthreads(64,1,1) in the HLSL
    enc->dispatchThreadgroups(MTL::Size::Make((uint32_t)(dispatchedThreads / tg), 1, 1),
                               MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(out.data(), buf->contents(), out.size() * sizeof(uint32_t));
    buf->release();
    return true;
  }
};

}  // namespace

int runMathvCleanBucketCounterSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-cleanbucketcounter] FAIL: no metallib\n"); return 1; }
  CbcDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-cleanbucketcounter] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-cleanbucketcounter", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  // Every scenario dispatches strictly MORE threads than bucketCount, so the tail threads
  // (bucketCount..dispatchedThreads) are a direct probe of the guard's "leave untouched" branch.
  struct Scenario { int32_t bucketCount; size_t dispatchedThreads; const char* tag; };
  const Scenario scenarios[] = {
      {0, 64, "n0"},               // bucketCount==0 -> every thread guarded off, buffer untouched
      {1, 64, "n1"},
      {63, 64, "n63-of-64"},       // classic tail-of-one-threadgroup case
      {64, 128, "n64-exact-tg"},   // bucketCount exactly one full threadgroup, padding beyond
      {65, 128, "n65"},
      {4096, 4160, "n4096"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<uint32_t> sentinel(s.dispatchedThreads);
    for (auto& v : sentinel) v = 0xCAFEBABEu ^ (uint32_t)rng.uniform(0.0f, 1e6f);

    // injectBug: send a bumped BucketCount to the REAL dispatch while the CPU ref keeps the original
    // -- corrupts which indices actually get cleared (real dispatch-path lever, see header note).
    int32_t bucketCountGpu = injectBug ? s.bucketCount + 1 : s.bucketCount;

    std::vector<uint32_t> gpuOut;
    if (!disp.dispatch(sentinel, bucketCountGpu, s.dispatchedThreads, gpuOut)) {
      dispatchOk = false;
      continue;
    }
    std::vector<uint32_t> refOut(s.dispatchedThreads);
    mathv_ref::mathvRefCleanBucketCounter(sentinel.data(), refOut.data(), s.dispatchedThreads,
                                           s.bucketCount);  // ref always uses the UNPERTURBED count
    for (size_t i = 0; i < s.dispatchedThreads; ++i) {
      float inVec[1] = {(float)i};
      cmpMain.add((float)gpuOut[i], (float)refOut[i], inVec, 1, 0, -1.0f, s.tag);
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "cleanbucketcounter");

  ParityReport rep("selftest-mathv-cleanbucketcounter");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  return rep.finish();
}

// order 1086: transpiler-batch wave-4 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1086, {"mathv-cleanbucketcounter", runMathvCleanBucketCounterSelfTest});

}  // namespace sw
