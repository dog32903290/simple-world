// selftests_mathv_uvsviewer.cpp — --selftest-mathv-uvsviewer (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-3 transpiler量產批: fuzz the TRANSPILED GPU "uvsviewer" kernel
// (app/shaders/uvsviewer.metal — glslang+spirv-cross output of TiXL mesh-UVs.hlsl, §10.1 recipe, with
// the wave-3 packed_float3 struct fix documented in that .metal's header) against the R-authored CPU
// oracle (app/src/mathv_ref_uvsviewer.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (lerp/mix affine blends + a branch selecting the blend TARGET, no transcendentals;
// the branch input SwitchUV is host-shared bit-identically to both GPU and ref, so no fp-comparison
// divergence risk — see uvsviewer_params.h header for why this stays Exact not Branchy).
//
// ── SCOPE ────────────────────────────────────────────────────────────────────────────────────────
// TWO pinned NAMED FORKS (both faithfully preserved, not fixed — see uvsviewer_params.h/uvsviewer.metal
// header notes):
//   (a) off-by-one dispatch guard (`i.x > Count`, not `>=`): every scenario allocates N+1 elements of
//       headroom. When N%64==0 the guard's off-by-one is INERT (no padding thread reaches index N) —
//       asserted via a pre-seeded sentinel at index N staying untouched. When N%64!=0 the padding
//       thread AT index==N DOES get processed — asserted by comparing index N against the ref too
//       (fed VerticesA[N], which this TU fills with valid random data specifically so that off-by-one
//       read is never garbage/undefined).
//   (b) ColorRGB + TexCoord2 are never written by the kernel (6-of-8 PbrVertex fields) — same
//       destination-buffer-untouched-sentinel treatment as (a)'s inert case, checked independently.
//
// ZONE: shell tier; crosses runtime only for SwVertex (PbrVertex's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_uvsviewer.h"
#include "parity_golden_harness.h"
#include "runtime/selftest_registry.h"
#include "runtime/sw_mesh.h"
#include "runtime/uvsviewer_params.h"

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

const float kSentinel = -999999.0f;

struct UvsViewerDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  UvsViewerDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("uvsviewer", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~UvsViewerDispatch() { if (pso) pso->release(); }
  UvsViewerDispatch(const UvsViewerDispatch&) = delete;

  // src/dstOut MUST both be sized count+1 (headroom for the off-by-one NAMED FORK). Dispatches
  // `count` worth of threadgroups (thread indices [0, 64*ceil(count/64))) so the off-by-one is
  // reproduced exactly as production dispatch sizing would.
  bool dispatch(const std::vector<SwVertex>& src, uint32_t count, float blendFactor, float switchUv,
                std::vector<SwVertex>& dstOut) const {
    if (!ok) return false;
    if (src.size() != count + 1 || dstOut.size() != count + 1) return false;
    MTL::Buffer* bufSrc = dev->newBuffer(src.data(), (NS::UInteger)(src.size() * sizeof(SwVertex)),
                                         MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst = dev->newBuffer(dstOut.data(), (NS::UInteger)(dstOut.size() * sizeof(SwVertex)),
                                         MTL::ResourceStorageModeShared);
    UvsViewerParams prm{count, blendFactor, switchUv};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufDst, 0, UVSVIEWER_ResultVertices);
    enc->setBuffer(bufSrc, 0, UVSVIEWER_VerticesA);
    enc->setBytes(&prm, sizeof(prm), UVSVIEWER_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((count + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(dstOut.data(), bufDst->contents(), dstOut.size() * sizeof(SwVertex));
    bufSrc->release();
    bufDst->release();
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

mathv_ref::UvsViewerIn toRefIn(const SwVertex& v) {
  return {v.Position.x,  v.Position.y,  v.Position.z,  v.Normal.x,    v.Normal.y,    v.Normal.z,
          v.Tangent.x,   v.Tangent.y,   v.Tangent.z,   v.Bitangent.x, v.Bitangent.y, v.Bitangent.z,
          v.Texcoord.x,  v.Texcoord.y,  v.Texcoord2.x, v.Texcoord2.y};
}

}  // namespace

int runMathvUvsViewerSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-uvsviewer] FAIL: no metallib\n"); return 1; }
  UvsViewerDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-uvsviewer] FAIL: no kernel\n"); return 1; }

  Rng rng(mathv::mathvSeed("uvsviewer"));
  Comparator cmpMain("mathv-uvsviewer", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { uint32_t n; float blend; float switchUv; const char* tag; };
  const Scenario scenarios[] = {
      {1, 0.0f, 0.0f, "n1-sw0"},
      {1, 1.0f, 1.0f, "n1-sw1"},
      {63, 0.25f, 0.0f, "n63-sw0"},        // 63%64!=0 -> off-by-one slot IS processed
      {64, 0.5f, 1.0f, "n64-sw1"},         // 64%64==0 -> off-by-one slot untouched
      {65, 0.75f, 0.0f, "n65-sw0"},        // 65%64!=0 -> off-by-one slot IS processed
      {256, 0.1f, 1.0f, "n256-sw1"},       // 256%64==0 -> off-by-one slot untouched
      {4096, 0.9f, 0.0f, "n4096-sw0"},     // 4096%64==0 -> off-by-one slot untouched
  };
  bool offByOneOk = true;
  bool untouchedFieldsOk = true;
  for (const auto& s : scenarios) {
    std::vector<SwVertex> src(s.n + 1);
    for (auto& v : src) v = randomVertex(rng);
    std::vector<SwVertex> dst(s.n + 1, SwVertex{});
    for (auto& v : dst) {
      v.Texcoord2 = {kSentinel, kSentinel};  // never written -> must survive (NOTED-QUIRK b)
      v.ColorRgb = {kSentinel, kSentinel, kSentinel};
    }
    float gpuBlend = injectBug ? s.blend + 1e-2f : s.blend;
    if (!disp.dispatch(src, s.n, gpuBlend, s.switchUv, dst)) { dispatchOk = false; continue; }

    const bool offByOneTriggers = (s.n % 64) != 0;
    const uint32_t compareUpTo = offByOneTriggers ? s.n + 1 : s.n;  // exclusive
    for (uint32_t i = 0; i < compareUpTo; ++i) {
      mathv_ref::UvsViewerOut refOut{};
      mathv_ref::uvsViewerOne(toRefIn(src[i]), s.blend, s.switchUv, refOut);
      float inVec[16];
      {
        auto rin = toRefIn(src[i]);
        std::memcpy(inVec, &rin, sizeof(inVec));
      }
      float gv[11] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z,
                       dst[i].Normal.x,   dst[i].Normal.y,   dst[i].Normal.z,
                       dst[i].Tangent.x,  dst[i].Tangent.y,  dst[i].Tangent.z,
                       dst[i].Bitangent.x, dst[i].Bitangent.y};
      float rv[11] = {refOut.posX, refOut.posY, refOut.posZ, refOut.normX, refOut.normY, refOut.normZ,
                       refOut.tanX, refOut.tanY, refOut.tanZ, refOut.bitanX, refOut.bitanY};
      for (int k = 0; k < 11; ++k) cmpMain.add(gv[k], rv[k], inVec, 16, k, -1.0f, s.tag);
      // Bitangent.z / TexCoord passthrough folded in too (kept out of the fixed-11 array for brevity).
      cmpMain.add(dst[i].Bitangent.z, refOut.bitanZ, inVec, 16, 11, -1.0f, s.tag);
      cmpMain.add(dst[i].Texcoord.x, refOut.texU, inVec, 16, 12, -1.0f, s.tag);
      cmpMain.add(dst[i].Texcoord.y, refOut.texV, inVec, 16, 13, -1.0f, s.tag);
    }
    if (!injectBug) {
      if (!offByOneTriggers) {
        // inert case: slot n must stay whatever dst[n] was seeded with pre-dispatch (fresh SwVertex{}
        // fields, EXCEPT Texcoord2/ColorRgb which were sentinel-seeded above and checked separately).
        if (dst[s.n].Position.x != 0.0f || dst[s.n].Normal.x != 0.0f) offByOneOk = false;
      }
      for (uint32_t i = 0; i < compareUpTo; ++i) {
        if (dst[i].Texcoord2.x != kSentinel || dst[i].Texcoord2.y != kSentinel) untouchedFieldsOk = false;
        if (dst[i].ColorRgb.x != kSentinel) untouchedFieldsOk = false;
      }
    }
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "uvsviewer");

  ParityReport rep("selftest-mathv-uvsviewer");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("offByOneNamedFork(inert-case slot untouched)", offByOneOk, offByOneOk ? 1.0 : 0.0);
  rep.expectTrue("untouchedFields(ColorRGB+TexCoord2 never written)", untouchedFieldsOk,
                 untouchedFieldsOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1081: transpiler-batch wave-3 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1081, {"mathv-uvsviewer", runMathvUvsViewerSelfTest});

}  // namespace sw
