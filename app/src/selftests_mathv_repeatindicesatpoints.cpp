// selftests_mathv_repeatindicesatpoints.cpp — --selftest-mathv-repeatindicesatpoints (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-2 transpiler量產批: fuzz the TRANSPILED GPU
// "repeatindicesatpoints" kernel (app/shaders/repeatindicesatpoints.metal — glslang+spirv-cross output
// of TiXL mesh-RepeatIndicesAtPoints.hlsl, §10.1 recipe, with the wave-2 packed_int3 stride fix
// documented in that .metal's header) against the R-authored CPU oracle
// (app/src/mathv_ref_repeatindicesatpoints.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (pure integer index rebase, no floats, no branches beyond the dispatch guard).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────────
// Internal kernel, no .t3ui — VertexCount is the source mesh's real vertex count (a non-negative
// rebase stride); this TU exercises it over plausible mesh sizes [1,4096].
//
// ── DISPATCH SHAPE: 2D grid (x=source face index, y=point/instance index), targetFaceIndex =
// y*Count+x — sibling of repeatverticesatpoints' 2D shape. Every scenario below uses np>=2 so the
// VertexCount rebase term is actually exercised at pointIndex>0 (pointIndex=0 alone would make the
// injectBug perturbation on VertexCount a no-op — the rebase term is VertexCount*pointIndex).
//
// ZONE: shell tier; crosses runtime only for SwTriIndex (int3's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_repeatindicesatpoints.h"
#include "parity_golden_harness.h"
#include "runtime/repeatindicesatpoints_params.h"
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

struct RiatpDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  RiatpDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("repeatindicesatpoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~RiatpDispatch() { if (pso) pso->release(); }
  RiatpDispatch(const RiatpDispatch&) = delete;

  bool dispatch(const std::vector<SwTriIndex>& src, int32_t vertexCount, uint32_t pointCountDispatch,
                std::vector<SwTriIndex>& result) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0 || pointCountDispatch == 0) return false;
    result.assign((size_t)n * pointCountDispatch, SwTriIndex{0, 0, 0});
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwTriIndex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufRes = dev->newBuffer(result.data(), (NS::UInteger)(result.size() * sizeof(SwTriIndex)),
                                         MTL::ResourceStorageModeShared);
    RepeatIndicesAtPointsParams prm{vertexCount, (int32_t)pointCountDispatch, n};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, RIATP_SourceFaces);
    enc->setBuffer(bufRes, 0, RIATP_ResultFaces);
    enc->setBytes(&prm, sizeof(prm), RIATP_Params);
    const uint32_t tgx = 16, tgy = 16;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tgx - 1) / tgx, (pointCountDispatch + tgy - 1) / tgy, 1),
                              MTL::Size::Make(tgx, tgy, 1));
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
  auto ri = [&]() { return (int32_t)rng.uniform(-500.0f, 500.0f); };
  return SwTriIndex{ri(), ri(), ri()};
}

}  // namespace

int runMathvRepeatIndicesAtPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-repeatindicesatpoints] FAIL: no metallib\n"); return 1; }
  RiatpDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-repeatindicesatpoints] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-repeatindicesatpoints", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { size_t nFaces; int32_t vertexCount; uint32_t nPoints; const char* tag; };
  const Scenario scenarios[] = {
      {1, 3, 2, "nf1-np2"},
      {63, 24, 3, "nf63-np3"},
      {64, 100, 5, "nf64-np5"},
      {65, 0, 4, "nf65-np4-vc0"},   // VertexCount=0 -> rebase is always 0 regardless of pointIndex
      {256, 1000, 8, "nf256-np8"},
      {512, 37, 16, "nf512-np16"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<SwTriIndex> src(s.nFaces);
    for (auto& t : src) t = randomTri(rng);
    int32_t gpuVc = injectBug ? s.vertexCount + 1 : s.vertexCount;
    std::vector<SwTriIndex> result;
    if (!disp.dispatch(src, gpuVc, s.nPoints, result)) { dispatchOk = false; continue; }
    for (uint32_t py = 0; py < s.nPoints; ++py) {
      for (size_t fx = 0; fx < s.nFaces; ++fx) {
        mathv_ref::RepeatIndicesAtPointsIn in{src[fx].X, src[fx].Y, src[fx].Z};
        mathv_ref::RepeatIndicesAtPointsOut refOut{};
        mathv_ref::repeatIndicesAtPointsOne(in, refOut, s.vertexCount, py);
        const SwTriIndex& g = result[(size_t)py * s.nFaces + fx];
        float inVec[3] = {(float)in.x, (float)in.y, (float)in.z};
        float gv[3] = {(float)g.X, (float)g.Y, (float)g.Z};
        float rv[3] = {(float)refOut.x, (float)refOut.y, (float)refOut.z};
        for (int k = 0; k < 3; ++k) cmpMain.add(gv[k], rv[k], inVec, 3, k, -1.0f, s.tag);
      }
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "repeatindicesatpoints");

  ParityReport rep("selftest-mathv-repeatindicesatpoints");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  return rep.finish();
}

// order 1075: transpiler-batch wave-2 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1075, {"mathv-repeatindicesatpoints", runMathvRepeatIndicesAtPointsSelfTest});

}  // namespace sw
