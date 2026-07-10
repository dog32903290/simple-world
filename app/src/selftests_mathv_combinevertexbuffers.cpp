// selftests_mathv_combinevertexbuffers.cpp — --selftest-mathv-combinevertexbuffers (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-2 transpiler量產批: fuzz the TRANSPILED GPU "combinevertexbuffers"
// kernel (app/shaders/combinevertexbuffers.metal — glslang+spirv-cross output of TiXL mesh-
// CombineVertexBuffers.hlsl, §10.1 recipe, with the wave-2 packed_float3 struct fix + binding-
// collision fix documented in that .metal's header) against the R-authored CPU oracle
// (app/src/mathv_ref_combinevertexbuffers.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (struct copy + one scalar add, no branches beyond the dispatch guard).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ─────────────────────────────────────────────
// Internal `_` kernel, no .t3ui — StartVertexIndex is a non-negative rebase offset (cook-loop running
// total, same role as CombineIndexBuffers' StartIndex); DebugValue is an unbounded debug nudge (no
// authored range in the .hlsl or any .t3ui — this TU exercises it over a generous [-1e3,1e3]).
//
// ── SCOPE: writes into an OFFSET SLICE of a larger ResultVertices buffer (targetIndex = i.x +
// StartVertexIndex), same offset-write shape as CombineIndexBuffers — this TU allocates headroom and
// checks the touched slice's Position (mutated) + Normal (copy-fidelity witness) against ref.
//
// ZONE: shell tier; crosses runtime only for SwVertex (PbrVertex's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_combinevertexbuffers.h"
#include "parity_golden_harness.h"
#include "runtime/combinevertexbuffers_params.h"
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

const float kSentinelY = -777777.0f;

struct CvbDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  CvbDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("combinevertexbuffers", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~CvbDispatch() { if (pso) pso->release(); }
  CvbDispatch(const CvbDispatch&) = delete;

  bool dispatch(const std::vector<SwVertex>& src, int32_t startVertexIndex, float debugValue,
                std::vector<SwVertex>& result) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0) return false;
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufRes = dev->newBuffer(result.data(), (NS::UInteger)(result.size() * sizeof(SwVertex)),
                                         MTL::ResourceStorageModeShared);
    CombineVertexBuffersParams prm{startVertexIndex, debugValue, n};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, CVB_Vertices);
    enc->setBuffer(bufRes, 0, CVB_ResultVertices);
    enc->setBytes(&prm, sizeof(prm), CVB_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(result.data(), bufRes->contents(), result.size() * sizeof(SwVertex));
    bufSrc->release();
    bufRes->release();
    return true;
  }
};

SwVertex randomVertex(Rng& rng) {
  SwVertex v{};
  v.Position = {rng.uniform(-100.0f, 100.0f), rng.uniform(-100.0f, 100.0f), rng.uniform(-100.0f, 100.0f)};
  v.Normal = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Tangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Bitangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Texcoord = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Texcoord2 = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Selection = rng.uniform(0.0f, 1.0f);
  v.ColorRgb = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  return v;
}

}  // namespace

int runMathvCombineVertexBuffersSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-combinevertexbuffers] FAIL: no metallib\n"); return 1; }
  CvbDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-combinevertexbuffers] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-combinevertexbuffers", EpsSpec::exact(), 5);
  bool dispatchOk = true, untouchedOk = true;

  struct Scenario { size_t n; int32_t startVertexIndex; float debugValue; const char* tag; };
  const Scenario scenarios[] = {
      {1, 0, 0.0f, "n1-start0"},
      {63, 0, 3.5f, "n63-start0"},
      {64, 5, -2.25f, "n64-start5"},
      {65, 100, 1e3f, "n65-start100"},
      {256, 1000, -1e3f, "n256-start1000"},
      {4096, 37, 0.001f, "n4096-start37"},
  };
  for (const auto& s : scenarios) {
    float gpuDv = injectBug ? s.debugValue + 1.0f : s.debugValue;
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<SwVertex> src(s.n);
    for (auto& v : src) v = randomVertex(rng);
    std::vector<SwVertex> result((size_t)s.startVertexIndex + s.n + 8);
    for (auto& v : result) v.Position.y = kSentinelY;
    if (!disp.dispatch(src, s.startVertexIndex, gpuDv, result)) { dispatchOk = false; continue; }
    mathv_ref::CombineVertexBuffersParams rp{s.debugValue};
    for (size_t i = 0; i < s.n; ++i) {
      mathv_ref::CombineVertexBuffersIn in{src[i].Position.x, src[i].Position.y, src[i].Position.z,
                                           src[i].Normal.x, src[i].Normal.y, src[i].Normal.z};
      mathv_ref::CombineVertexBuffersOut refOut{};
      mathv_ref::combineVertexBuffersOne(in, refOut, rp);
      const SwVertex& g = result[(size_t)s.startVertexIndex + i];
      float inVec[6] = {in.posX, in.posY, in.posZ, in.normX, in.normY, in.normZ};
      float gv[6] = {g.Position.x, g.Position.y, g.Position.z, g.Normal.x, g.Normal.y, g.Normal.z};
      float rv[6] = {refOut.posX, refOut.posY, refOut.posZ, refOut.normX, refOut.normY, refOut.normZ};
      for (int k = 0; k < 6; ++k) cmpMain.add(gv[k], rv[k], inVec, 6, k, -1.0f, s.tag);
    }
    if (!injectBug) {
      for (int32_t j = 0; j < s.startVertexIndex; ++j)
        if (result[(size_t)j].Position.y != kSentinelY) untouchedOk = false;
      for (size_t j = (size_t)s.startVertexIndex + s.n; j < result.size(); ++j)
        if (result[j].Position.y != kSentinelY) untouchedOk = false;
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "combinevertexbuffers");

  ParityReport rep("selftest-mathv-combinevertexbuffers");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("boundsSafety(untouched slots preserved)", untouchedOk, untouchedOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1072: transpiler-batch wave-2 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1072, {"mathv-combinevertexbuffers", runMathvCombineVertexBuffersSelfTest});

}  // namespace sw
