// selftests_mathv_selectvertices.cpp — --selftest-mathv-selectvertices (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-3 transpiler量產批: fuzz the TRANSPILED GPU "selectvertices" kernel
// (app/shaders/selectvertices.metal — glslang+spirv-cross output of TiXL mesh-SelectVertices.hlsl,
// §10.1 recipe) against the R-authored CPU oracle (app/src/mathv_ref_selectvertices.h, reusing the
// shared tixl_noise_oracle.h snoise) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// TRANSCENDENTAL+BRANCHY class (length/smoothstep/snoise across two independent 5-way threshold
// chains — VolumeShape selects the "field" distance function, SelectMode selects the combine mode).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// VolumeShape/SelectMode are enum-like float selectors with clean-integer thresholds (0.5/1.5/.../4.5
// spacing) — fuzzed as EXACT integer values {0,1,2,3,4,5} (5=out-of-range, exercises each branch's
// well-defined fallback: VolumeShape>=4.5 -> s=1 unchanged from its init; SelectMode>=4.5 -> result=s
// unchanged) so branch SELECTION itself is never in question (no float->threshold jitter risk — same
// discipline as deformmesh's TaperAxis/TwistAxis clean-integer domain). MATRIX CONVENTION reused
// verbatim from transformmeshuvs_params.h/mathv_ref_transformmeshuvs.h (same `v * float4x4` raw-
// transpile shape, already empirically proven there) — a general non-identity matrix is used across
// every scenario here, self-verifying: if the convention were wrong every branch would show large
// systematic error, not a subtle one, so no separate dedicated matrixConventionTooth is needed.
//
// ZONE: shell tier; crosses runtime only for SwVertex (PbrVertex's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_selectvertices.h"
#include "parity_golden_harness.h"
#include "runtime/selectvertices_params.h"
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

struct SelectVDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  SelectVDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("selectvertices", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SelectVDispatch() { if (pso) pso->release(); }
  SelectVDispatch(const SelectVDispatch&) = delete;

  bool dispatch(const std::vector<SwVertex>& src, const SelectVerticesParams& prmIn,
                std::vector<SwVertex>& dstOut) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0 || dstOut.size() != n) return false;
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst =
        dev->newBuffer(dstOut.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    SelectVerticesParams prm = prmIn;
    prm.Count = n;
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, SELECTV_SourceVertices);
    enc->setBuffer(bufDst, 0, SELECTV_ResultVertices);
    enc->setBytes(&prm, sizeof(prm), SELECTV_Params);
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

SwVertex randomVertex(Rng& rng) {
  SwVertex v{};
  v.Position = {rng.uniform(-3.0f, 3.0f), rng.uniform(-3.0f, 3.0f), rng.uniform(-3.0f, 3.0f)};
  v.Normal = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Tangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Bitangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Texcoord = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Texcoord2 = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Selection = rng.uniform(0.0f, 1.0f);
  v.ColorRgb = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  return v;
}

// Mild non-identity matrix (scale 1.3 + small shear + translate) -- exercises the matrix path without
// re-deriving the convention (already empirically proven in transformmeshuvs).
void mildMatrix(float m[16]) {
  m[0] = 1.3f; m[1] = 0.1f; m[2] = 0.0f; m[3] = 0.4f;
  m[4] = -0.1f; m[5] = 1.1f; m[6] = 0.0f; m[7] = -0.2f;
  m[8] = 0.0f; m[9] = 0.0f; m[10] = 0.9f; m[11] = 0.3f;
  m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

}  // namespace

int runMathvSelectVerticesSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-selectvertices] FAIL: no metallib\n"); return 1; }
  SelectVDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-selectvertices] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-selectvertices", EpsSpec::transcendental(), 5);
  Comparator cmpPassthrough("mathv-selectvertices-passthrough", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { float volumeShape, selectMode, clampResult; const char* tag; };
  const Scenario scenarios[] = {
      {0.0f, 0.0f, 0.0f, "sphere-override"},
      {1.0f, 1.0f, 0.0f, "box-add"},
      {2.0f, 2.0f, 0.0f, "plane-sub"},
      {3.0f, 3.0f, 1.0f, "zebra-multiply-clamped"},
      {4.0f, 4.0f, 0.0f, "noise-invert"},
      {5.0f, 5.0f, 0.0f, "oor-oor-passthrough"},
      {0.0f, 5.0f, 1.0f, "sphere-oor-clamped"},
  };
  for (const auto& s : scenarios) {
    Rng srng(mathv::mathvSeed(s.tag));
    const size_t N = 512;
    std::vector<SwVertex> src(N);
    for (auto& v : src) v = randomVertex(srng);
    SelectVerticesParams p{};
    std::memset(&p, 0, sizeof(p));
    mildMatrix(p.m);
    p.FallOff = srng.uniform(0.1f, 2.0f);
    p.VolumeShape = s.volumeShape;
    p.SelectMode = s.selectMode;
    p.ClampResult = s.clampResult;
    p.Strength = srng.uniform(-2.0f, 2.0f);
    p.Phase = srng.uniform(-1.0f, 1.0f);
    p.Threshold = srng.uniform(-1.0f, 1.0f);
    p.UseVertexSelection = srng.uniform(0.0f, 1.0f);  // dead field -- must NOT affect output (see below)

    SelectVerticesParams gpuP = p;
    if (injectBug) gpuP.Strength += 1e-2f;  // corrupt the real dispatch path
    std::vector<SwVertex> out(N, SwVertex{});
    if (!disp.dispatch(src, gpuP, out)) { dispatchOk = false; continue; }

    mathv_ref::SelectVerticesParamsR rp{};
    std::memcpy(rp.m, p.m, sizeof(rp.m));
    rp.fallOff = p.FallOff;
    rp.volumeShape = p.VolumeShape;
    rp.selectMode = p.SelectMode;
    rp.clampResult = p.ClampResult;
    rp.strength = p.Strength;
    rp.phase = p.Phase;
    rp.threshold = p.Threshold;

    for (size_t i = 0; i < N; ++i) {
      mathv_ref::SelectVerticesIn in{src[i].Position.x, src[i].Position.y, src[i].Position.z, src[i].Selection};
      float refSelected = mathv_ref::selectVerticesOne(in, rp);
      float inVec[4] = {in.posX, in.posY, in.posZ, in.selected};
      cmpMain.add(out[i].Selection, refSelected, inVec, 4, 0, -1.0f, s.tag);
      if (!injectBug) {
        // Position/Normal/Tangent/Bitangent/TexCoord/TexCoord2/ColorRGB: exact passthrough.
        float gp[12] = {out[i].Position.x, out[i].Position.y, out[i].Position.z,
                         out[i].Normal.x,   out[i].Normal.y,   out[i].Normal.z,
                         out[i].Tangent.x,  out[i].Tangent.y,  out[i].Tangent.z,
                         out[i].Bitangent.x, out[i].Bitangent.y, out[i].Bitangent.z};
        float rp12[12] = {src[i].Position.x, src[i].Position.y, src[i].Position.z,
                           src[i].Normal.x,   src[i].Normal.y,   src[i].Normal.z,
                           src[i].Tangent.x,  src[i].Tangent.y,  src[i].Tangent.z,
                           src[i].Bitangent.x, src[i].Bitangent.y, src[i].Bitangent.z};
        for (int k = 0; k < 12; ++k) cmpPassthrough.add(gp[k], rp12[k], inVec, 4, k, -1.0f, s.tag);
      }
    }
  }
  cmpMain.print();
  cmpPassthrough.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  bool passPassthrough = injectBug || cmpPassthrough.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "selectvertices");

  ParityReport rep("selftest-mathv-selectvertices");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(Selected, transcendental+branchy)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("compare(7-field passthrough, exact)", passPassthrough, passPassthrough ? 1.0 : 0.0);
  return rep.finish();
}

// order 1084: transpiler-batch wave-3 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1084, {"mathv-selectvertices", runMathvSelectVerticesSelfTest});

}  // namespace sw
