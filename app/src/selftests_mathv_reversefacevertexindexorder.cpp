// selftests_mathv_reversefacevertexindexorder.cpp — --selftest-mathv-reversefacevertexindexorder
// (hand-rolled TU). MATH_VERIFY_WORKFLOW.md §10 wave-2 transpiler量產批: fuzz the TRANSPILED GPU
// "reversefacevertexindexorder" kernel (app/shaders/reversefacevertexindexorder.metal — glslang+
// spirv-cross output of TiXL mesh-ReverseFaceVertexIndexOrder.hlsl, §10.1 recipe, with the wave-2
// packed_int3 stride fix documented in that .metal's header) against the R-authored CPU oracle
// (app/src/mathv_ref_reversefacevertexindexorder.h) via direct-kernel dispatch (§1.3 — NOT
// buildEvalGraph). EXACT-class op (pure integer swizzle, no floats, no branches beyond the dispatch
// bound guard).
//
// ── SCOPE ────────────────────────────────────────────────────────────────────────────────────────
// The op has ZERO real TiXL cbuffer params (HLSL's `cbuffer Params : register(b0)` is empty) — this
// TU therefore has no ParamDomain/identity-sentinel layer in the usual mathv sense (params.h's Count
// field is host-ABI-only, see that header's note). In its place: an INVOLUTION invariant tooth
// (reverse(reverse(x)) == x) — a stronger, op-specific pinned property than a param identity sentinel
// would be, since it exercises the dispatch path twice and catches any stride/index corruption that a
// single-pass compare could miss on a lucky element count.
//
// ZONE: shell tier; crosses runtime only for SwTriIndex (int3's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_reversefacevertexindexorder.h"
#include "parity_golden_harness.h"
#include "runtime/reversefacevertexindexorder_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/sw_mesh.h"

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

struct RfvioDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  RfvioDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("reversefacevertexindexorder", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~RfvioDispatch() { if (pso) pso->release(); }
  RfvioDispatch(const RfvioDispatch&) = delete;

  bool dispatch(const std::vector<SwTriIndex>& src, std::vector<SwTriIndex>& dstOut) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0) return false;
    dstOut.assign(n, SwTriIndex{0, 0, 0});
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwTriIndex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst =
        dev->newBuffer(dstOut.data(), (NS::UInteger)(n * sizeof(SwTriIndex)), MTL::ResourceStorageModeShared);
    ReverseFaceVertexIndexOrderParams prm{n};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, RFVIO_SourceIndices);
    enc->setBuffer(bufDst, 0, RFVIO_ResultIndices);
    enc->setBytes(&prm, sizeof(prm), RFVIO_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(dstOut.data(), bufDst->contents(), (size_t)n * sizeof(SwTriIndex));
    bufSrc->release();
    bufDst->release();
    return true;
  }
};

SwTriIndex randomTri(Rng& rng) {
  auto ri = [&]() { return (int32_t)rng.uniform(-5000.0f, 5000.0f); };
  return SwTriIndex{ri(), ri(), ri()};
}

// Compare one batch: dispatch(srcGpu) vs ref(srcRef) element-by-element (3 lanes/element). srcGpu may
// carry the injectBug perturbation on lane 0 of element 0 while srcRef keeps the original (§1.5
// analog for a param-less op — there's no cbuffer scalar to bump, so the perturbation lands on the
// GPU-bound INPUT DATA itself, which still corrupts the real dispatch path without being a want-flip).
bool compareBatch(const RfvioDispatch& disp, Comparator& cmp, const std::vector<SwTriIndex>& srcGpu,
                  const std::vector<SwTriIndex>& srcRef, const char* batchTag) {
  const uint32_t n = (uint32_t)srcGpu.size();
  std::vector<SwTriIndex> out;
  if (!disp.dispatch(srcGpu, out) || out.size() != n) return false;
  for (uint32_t i = 0; i < n; ++i) {
    mathv_ref::ReverseFaceVertexIndexOrderIn in{srcRef[i].X, srcRef[i].Y, srcRef[i].Z};
    mathv_ref::ReverseFaceVertexIndexOrderOut refOut{};
    mathv_ref::reverseFaceVertexIndexOrderOne(in, refOut);
    float inVec[3] = {(float)in.x, (float)in.y, (float)in.z};
    float g[3] = {(float)out[i].X, (float)out[i].Y, (float)out[i].Z};
    float r[3] = {(float)refOut.x, (float)refOut.y, (float)refOut.z};
    for (int k = 0; k < 3; ++k) cmp.add(g[k], r[k], inVec, 3, k, -1.0f, batchTag);
  }
  return true;
}

// ── TOOTH INVOLUTION (op-specific pinned property, substitutes the identity sentinel for this
// param-less op): reverse(reverse(x)) must equal x EXACTLY, for every element. ──
bool checkInvolutionTooth(const RfvioDispatch& disp) {
  Comparator cmp("mathv-reversefacevertexindexorder-involution", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("reversefacevertexindexorder-involution"));
  const size_t N = 512;
  std::vector<SwTriIndex> src(N);
  for (auto& t : src) t = randomTri(rng);
  std::vector<SwTriIndex> once, twice;
  bool ok = disp.dispatch(src, once) && disp.dispatch(once, twice);
  for (size_t i = 0; ok && i < N; ++i) {
    float inv[3] = {(float)src[i].X, (float)src[i].Y, (float)src[i].Z};
    cmp.add((float)twice[i].X, (float)src[i].X, inv, 3, 0, -1.0f, "involution");
    cmp.add((float)twice[i].Y, (float)src[i].Y, inv, 3, 1, -1.0f, "involution");
    cmp.add((float)twice[i].Z, (float)src[i].Z, inv, 3, 2, -1.0f, "involution");
  }
  cmp.print();
  return ok && cmp.verdict();
}

}  // namespace

int runMathvReverseFaceVertexIndexOrderSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-reversefacevertexindexorder] FAIL: no metallib\n"); return 1; }
  RfvioDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-reversefacevertexindexorder] FAIL: no kernel\n"); return 1; }

  Rng rng(mathv::mathvSeed("reversefacevertexindexorder"));
  Comparator cmpMain("mathv-reversefacevertexindexorder", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  // ── boundary-guard sweep: N straddling the 64-wide threadgroup (1, 63, 64, 65, 256, 4096) ──
  const size_t sizes[] = {1, 63, 64, 65, 256, 4096};
  for (size_t n : sizes) {
    std::vector<SwTriIndex> srcRef(n);
    for (auto& t : srcRef) t = randomTri(rng);
    std::vector<SwTriIndex> srcGpu = srcRef;
    if (injectBug) srcGpu[0].X += 1;  // corrupt the real dispatch path (no cbuffer scalar to bump)
    char tag[32];
    std::snprintf(tag, sizeof tag, "n=%zu", n);
    if (!compareBatch(disp, cmpMain, srcGpu, srcRef, tag)) dispatchOk = false;
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "reversefacevertexindexorder");

  bool passInvolution = checkInvolutionTooth(disp);

  ParityReport rep("selftest-mathv-reversefacevertexindexorder");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(boundary-sweep, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("involution(reverse(reverse(x))==x)", passInvolution, passInvolution ? 1.0 : 0.0);
  return rep.finish();
}

// order 1070: transpiler-batch wave-2 (MATH_VERIFY_WORKFLOW.md §10), appends after mathv-pointsimulation (1062).
REGISTER_SELFTESTS(/*orderBase=*/1070,
                   {"mathv-reversefacevertexindexorder", runMathvReverseFaceVertexIndexOrderSelfTest});

}  // namespace sw
