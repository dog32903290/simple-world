// selftests_mathv_flipnormals.cpp — --selftest-mathv-flipnormals (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-2 transpiler量產批: fuzz the TRANSPILED GPU "flipnormals" kernel
// (app/shaders/flipnormals.metal — glslang+spirv-cross output of TiXL mesh-FlipNormals.hlsl, §10.1
// recipe, with the wave-2 packed_float3 struct fix documented in that .metal's header) against the
// R-authored CPU oracle (app/src/mathv_ref_flipnormals.h) via direct-kernel dispatch (§1.3 — NOT
// buildEvalGraph). EXACT-class op (negate + passthrough, no branches beyond the dispatch guard).
//
// ── SCOPE ────────────────────────────────────────────────────────────────────────────────────────
// Zero real TiXL cbuffer params (HLSL's Params block is empty) — no ParamDomain/identity-sentinel
// layer in the usual sense. In its place: a TOOTH BUG-INVARIANT (pinned, NOTED-QUIRK not NAMED-FORK,
// see params.h header) — TexCoord2 must stay whatever the destination buffer held BEFORE dispatch,
// since the HLSL body never assigns it (7-of-8-field bug, faithfully preserved). A RED here means the
// kernel started writing TexCoord2 — a real regression, not a discovery (SimBlendTo W-invariant shape).
//
// ZONE: shell tier; crosses runtime only for SwVertex (PbrVertex's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_flipnormals.h"
#include "parity_golden_harness.h"
#include "runtime/flipnormals_params.h"
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

struct FlipNormalsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  FlipNormalsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("flipnormals", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~FlipNormalsDispatch() { if (pso) pso->release(); }
  FlipNormalsDispatch(const FlipNormalsDispatch&) = delete;

  bool dispatch(const std::vector<SwVertex>& src, std::vector<SwVertex>& dstOut) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0 || dstOut.size() != n) return false;
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst =
        dev->newBuffer(dstOut.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    FlipNormalsParams prm{n};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, FLIPNORMALS_SourceVerts);
    enc->setBuffer(bufDst, 0, FLIPNORMALS_ResultVerts);
    enc->setBytes(&prm, sizeof(prm), FLIPNORMALS_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(dstOut.data(), bufDst->contents(), (size_t)n * sizeof(SwVertex));
    bufSrc->release();
    bufDst->release();
    return true;
  }
};

SwVertex randomVertex(Rng& rng, float texCoord2Sentinel) {
  SwVertex v{};
  v.Position = {rng.uniform(-100.0f, 100.0f), rng.uniform(-100.0f, 100.0f), rng.uniform(-100.0f, 100.0f)};
  v.Normal = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Tangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Bitangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Texcoord = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Texcoord2 = {texCoord2Sentinel, texCoord2Sentinel};  // pre-seed the DESTINATION with a sentinel
  v.Selection = rng.uniform(0.0f, 1.0f);
  v.ColorRgb = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  return v;
}

// dstOut must be PRE-SEEDED with the sentinel TexCoord2 before dispatch (dispatch() overwrites Src's
// buffer contents into Dst only for the fields the kernel actually assigns).
bool checkTexCoord2UntouchedTooth(const FlipNormalsDispatch& disp) {
  const float kSentinel = -999999.0f;
  Rng rng(mathv::mathvSeed("flipnormals-texcoord2-invariant"));
  const size_t N = 512;
  std::vector<SwVertex> src(N), dst(N, SwVertex{});
  for (size_t i = 0; i < N; ++i) {
    src[i] = randomVertex(rng, 0.0f);
    dst[i].Texcoord2 = {kSentinel, kSentinel};  // destination buffer pre-seeded before dispatch
  }
  if (!disp.dispatch(src, dst)) return false;
  for (const auto& v : dst)
    if (v.Texcoord2.x != kSentinel || v.Texcoord2.y != kSentinel) return false;
  return true;
}

}  // namespace

int runMathvFlipNormalsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-flipnormals] FAIL: no metallib\n"); return 1; }
  FlipNormalsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-flipnormals] FAIL: no kernel\n"); return 1; }

  Rng rng(mathv::mathvSeed("flipnormals"));
  Comparator cmpMain("mathv-flipnormals", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  const size_t sizes[] = {1, 63, 64, 65, 256, 4096};
  for (size_t n : sizes) {
    std::vector<SwVertex> src(n);
    for (auto& v : src) v = randomVertex(rng, 0.0f);
    std::vector<SwVertex> srcGpu = src;
    if (injectBug) srcGpu[0].Normal.x += 1e-2f;  // corrupt the real dispatch path (no cbuffer scalar)
    std::vector<SwVertex> out(n, SwVertex{});
    if (!disp.dispatch(srcGpu, out)) { dispatchOk = false; continue; }
    char tag[32];
    std::snprintf(tag, sizeof tag, "n=%zu", n);
    for (size_t i = 0; i < n; ++i) {
      mathv_ref::FlipNormalsIn in{src[i].Position.x, src[i].Position.y, src[i].Position.z,
                                  src[i].Normal.x, src[i].Normal.y, src[i].Normal.z};
      mathv_ref::FlipNormalsOut refOut{};
      mathv_ref::flipNormalsOne(in, refOut);
      float inVec[6] = {in.posX, in.posY, in.posZ, in.normX, in.normY, in.normZ};
      float gv[6] = {out[i].Position.x, out[i].Position.y, out[i].Position.z,
                     out[i].Normal.x, out[i].Normal.y, out[i].Normal.z};
      float rv[6] = {refOut.posX, refOut.posY, refOut.posZ, refOut.normX, refOut.normY, refOut.normZ};
      for (int k = 0; k < 6; ++k) cmpMain.add(gv[k], rv[k], inVec, 6, k, -1.0f, tag);
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "flipnormals");

  bool passTexCoord2 = checkTexCoord2UntouchedTooth(disp);

  ParityReport rep("selftest-mathv-flipnormals");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(boundary-sweep, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("texCoord2Invariant(pinned bug: never written)", passTexCoord2, passTexCoord2 ? 1.0 : 0.0);
  return rep.finish();
}

// order 1073: transpiler-batch wave-2 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1073, {"mathv-flipnormals", runMathvFlipNormalsSelfTest});

}  // namespace sw
