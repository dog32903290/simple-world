// selftests_mathv_combineindexbuffers.cpp — --selftest-mathv-combineindexbuffers (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-2 transpiler量產批: fuzz the TRANSPILED GPU "combineindexbuffers"
// kernel (app/shaders/combineindexbuffers.metal — glslang+spirv-cross output of TiXL mesh-
// CombineIndexBuffers.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_combineindexbuffers.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (pure integer add + index rebase, no floats, no branches beyond the dispatch guard).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ─────────────────────────────────────────────
// mesh-CombineIndexBuffers.hlsl has no .t3ui (it's an internal `_` kernel, no user-facing node of its
// own — dispatched by CombineMeshes.t3's cook loop). StartIndex/StartVertex are non-negative rebase
// offsets computed by that cook loop (running totals of prior meshes' face/vertex counts); this TU
// exercises them over [0, 4096) which comfortably covers realistic combine-N-meshes scenarios.
//
// ── SCOPE: the op WRITES INTO AN OFFSET SLICE of a larger ResultIndices buffer (targetIndex = i.x +
// StartIndex), not index-for-index like reversefacevertexindexorder — this TU allocates ResultIndices
// with headroom (StartIndex + n + tail) and checks BOTH the touched slice (against ref) AND that the
// untouched tail is never written (a bonus bounds-safety check, not required by §2/§3 but free here).
//
// ZONE: shell tier; crosses runtime only for SwTriIndex (int3's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_combineindexbuffers.h"
#include "parity_golden_harness.h"
#include "runtime/combineindexbuffers_params.h"
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

const int32_t kSentinel = -777777;

struct CibDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  CibDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("combineindexbuffers", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~CibDispatch() { if (pso) pso->release(); }
  CibDispatch(const CibDispatch&) = delete;

  // resultCap must be >= startIndex + (int)src.size(); untouched slots keep whatever `result` held
  // going in (caller pre-fills with kSentinel to probe out-of-slice writes).
  bool dispatch(const std::vector<SwTriIndex>& src, int32_t startIndex, int32_t startVertex,
                std::vector<SwTriIndex>& result) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0) return false;
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwTriIndex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufRes = dev->newBuffer(result.data(), (NS::UInteger)(result.size() * sizeof(SwTriIndex)),
                                         MTL::ResourceStorageModeShared);
    CombineIndexBuffersParams prm{startIndex, startVertex, n};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, CIB_Indices);
    enc->setBytes(&prm, sizeof(prm), CIB_Params);
    enc->setBuffer(bufRes, 0, CIB_ResultIndices);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(result.data(), bufRes->contents(), result.size() * sizeof(SwTriIndex));
    bufSrc->release();
    bufRes->release();
    return true;
  }
};

SwTriIndex randomTri(Rng& rng) {
  auto ri = [&]() { return (int32_t)rng.uniform(-5000.0f, 5000.0f); };
  return SwTriIndex{ri(), ri(), ri()};
}

// Bonus bounds-safety check (not eps-gated, exact sentinel check): slots before startIndex and after
// startIndex+n must remain kSentinel — proves the kernel only ever touches [startIndex, startIndex+n).
bool checkUntouchedSlots(const std::vector<SwTriIndex>& result, int32_t startIndex, size_t n) {
  for (int32_t i = 0; i < startIndex; ++i)
    if (result[(size_t)i].X != kSentinel) return false;
  for (size_t i = (size_t)startIndex + n; i < result.size(); ++i)
    if (result[i].X != kSentinel) return false;
  return true;
}

}  // namespace

int runMathvCombineIndexBuffersSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-combineindexbuffers] FAIL: no metallib\n"); return 1; }
  CibDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-combineindexbuffers] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-combineindexbuffers", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { size_t n; int32_t startIndex; int32_t startVertex; const char* tag; };
  const Scenario scenarios[] = {
      {1, 0, 0, "n1-start0"},
      {63, 0, 17, "n63-start0"},
      {64, 5, -3, "n64-start5"},
      {65, 100, 4096, "n65-start100"},
      {256, 1000, -777, "n256-start1000"},
      {4096, 37, 999999, "n4096-start37"},
  };
  bool untouchedOk = true;
  for (const auto& s : scenarios) {
    int32_t gpuSv = injectBug ? s.startVertex + 1 : s.startVertex;
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<SwTriIndex> src(s.n);
    for (auto& t : src) t = randomTri(rng);
    std::vector<SwTriIndex> result((size_t)s.startIndex + s.n + 8, SwTriIndex{kSentinel, kSentinel, kSentinel});
    if (!disp.dispatch(src, s.startIndex, gpuSv, result)) { dispatchOk = false; continue; }
    mathv_ref::CombineIndexBuffersParams rp{0, s.startVertex};
    for (size_t i = 0; i < s.n; ++i) {
      mathv_ref::CombineIndexBuffersIn in{src[i].X, src[i].Y, src[i].Z};
      mathv_ref::CombineIndexBuffersOut refOut{};
      mathv_ref::combineIndexBuffersOne(in, refOut, rp);
      const SwTriIndex& g = result[(size_t)s.startIndex + i];
      float inVec[3] = {(float)in.x, (float)in.y, (float)in.z};
      float gv[3] = {(float)g.X, (float)g.Y, (float)g.Z};
      float rv[3] = {(float)refOut.x, (float)refOut.y, (float)refOut.z};
      for (int k = 0; k < 3; ++k) cmpMain.add(gv[k], rv[k], inVec, 3, k, -1.0f, s.tag);
    }
    if (!injectBug && !checkUntouchedSlots(result, s.startIndex, s.n)) untouchedOk = false;
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "combineindexbuffers");

  ParityReport rep("selftest-mathv-combineindexbuffers");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("boundsSafety(untouched slots preserved)", untouchedOk, untouchedOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1071: transpiler-batch wave-2 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1071, {"mathv-combineindexbuffers", runMathvCombineIndexBuffersSelfTest});

}  // namespace sw
