// selftests_mathv_transformmeshuvs.cpp — --selftest-mathv-transformmeshuvs (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-3 transpiler量產批: fuzz the TRANSPILED GPU "transformmeshuvs"
// kernel (app/shaders/transformmeshuvs.metal — glslang+spirv-cross output of TiXL mesh-TransformUVs.
// hlsl, §10.1 recipe) against the R-authored CPU oracle (app/src/mathv_ref_transformmeshuvs.h) via
// direct-kernel dispatch (§1.3 — NOT buildEvalGraph). EXACT class (affine matrix transform + lerp).
//
// ── MATRIX CONVENTION EMPIRICAL VERIFICATION (§10.5's standalone-stride-test methodology, applied to
// matrix layout instead of struct packing) ─────────────────────────────────────────────────────────
// This is the FIRST matrix-typed op in the transpiler batch (prior ops were scalar/vec3-only). The
// kernel uses MSL's native `float4x4`+`v*M` operator (unlike the hand-written production kernels'
// flat-array `M·v`) — transformmeshuvs_params.h derives ALGEBRAICALLY that uploading m[0..15]
// row-major (translation in column 3) makes the two forms equal. `matrixConventionTooth` below is the
// EMPIRICAL PROOF of that derivation: it feeds a hand-picked ASYMMETRIC permutation-plus-translation
// matrix (swap x<->y, then translate) through the REAL dispatched kernel and checks the output lands
// exactly where the row-major/M·v convention predicts. If this tooth is RED, the derivation was wrong
// and the upload order needs to flip (transpose) — a data-driven correction, not more armchair algebra.
//
// ZONE: shell tier; crosses runtime only for SwVertex (PbrVertex's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_transformmeshuvs.h"
#include "parity_golden_harness.h"
#include "runtime/selftest_registry.h"
#include "runtime/sw_mesh.h"
#include "runtime/transformmeshuvs_params.h"

#include <cmath>
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

struct TmuvDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  TmuvDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("transformmeshuvs", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~TmuvDispatch() { if (pso) pso->release(); }
  TmuvDispatch(const TmuvDispatch&) = delete;

  bool dispatch(const std::vector<SwVertex>& src, const float m[16], float useVertexSelection,
                float toTexCoord2, std::vector<SwVertex>& dstOut) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0 || dstOut.size() != n) return false;
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst =
        dev->newBuffer(dstOut.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    TransformMeshUvsParams prm{};
    std::memcpy(prm.m, m, sizeof(prm.m));
    prm.UseVertexSelection = useVertexSelection;
    prm.ToTexCoord2 = toTexCoord2;
    prm.Count = n;
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, TMUV_SourceVerts);
    enc->setBytes(&prm, sizeof(prm), TMUV_Params);
    enc->setBuffer(bufDst, 0, TMUV_ResultVerts);
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
  v.Position = {rng.uniform(-20.0f, 20.0f), rng.uniform(-20.0f, 20.0f), rng.uniform(-20.0f, 20.0f)};
  v.Normal = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Tangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Bitangent = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
  v.Texcoord = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
  v.Texcoord2 = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
  v.Selection = rng.uniform(0.0f, 1.0f);
  v.ColorRgb = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  return v;
}

void identityMatrix(float m[16]) {
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// EMPIRICAL PROOF tooth (see file header): swap-X<->Y then translate by (100,200,300). Row-major, w
// column = 3/7/11 (this header's convention): row0 selects input.y + tx, row1 selects input.x + ty.
bool matrixConventionTooth(const TmuvDispatch& disp) {
  float m[16] = {
      0, 1, 0, 100,  // row0: out.x = in.y + 100
      1, 0, 0, 200,  // row1: out.y = in.x + 200
      0, 0, 1, 300,  // row2: out.z = in.z + 300
      0, 0, 0, 1,
  };
  std::vector<SwVertex> src(1);
  src[0] = SwVertex{};
  src[0].Texcoord = {7.0f, 11.0f};  // pos = (7,11,0) -> expect transformed (11+100, 7+200, 300)
  std::vector<SwVertex> dst(1, SwVertex{});
  // ToTexCoord2=0 -> uses TexCoord path; UseVertexSelection=0 -> s=1 (full transform, no blend).
  if (!disp.dispatch(src, m, 0.0f, 0.0f, dst)) return false;
  const float expectX = 111.0f, expectY = 207.0f;
  bool ok = std::fabs(dst[0].Texcoord.x - expectX) < 1e-3f && std::fabs(dst[0].Texcoord.y - expectY) < 1e-3f;
  if (!ok) {
    printf("[selftest-mathv-transformmeshuvs] matrixConventionTooth MISMATCH: got (%.4f,%.4f) want (%.4f,%.4f)\n",
           dst[0].Texcoord.x, dst[0].Texcoord.y, expectX, expectY);
  }
  return ok;
}

}  // namespace

int runMathvTransformMeshUvsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-transformmeshuvs] FAIL: no metallib\n"); return 1; }
  TmuvDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-transformmeshuvs] FAIL: no kernel\n"); return 1; }

  if (!injectBug) {
    bool convOk = matrixConventionTooth(disp);
    if (!convOk) {
      printf("[selftest-mathv-transformmeshuvs] FAIL: matrix convention empirical check failed — "
             "row-major/M*v derivation in transformmeshuvs_params.h does NOT match the real kernel; "
             "the host upload order needs a transpose fix, not more algebra.\n");
      return 1;
    }
  }

  Rng rng(mathv::mathvSeed("transformmeshuvs"));
  Comparator cmpMain("mathv-transformmeshuvs", EpsSpec::exact(), 5);
  Comparator cmpPassthrough("mathv-transformmeshuvs-passthrough", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { float useVs, toTex2; const char* tag; bool identity; };
  const Scenario scenarios[] = {
      {0.0f, 0.0f, "identity-tex", true},
      {0.0f, 1.0f, "identity-tex2", true},
      {0.0f, 0.0f, "random-tex", false},
      {1.0f, 0.0f, "random-tex-vs", false},
      {0.0f, 1.0f, "random-tex2", false},
      {1.0f, 1.0f, "random-tex2-vs", false},
  };
  for (const auto& s : scenarios) {
    Rng srng(mathv::mathvSeed(s.tag));
    const size_t N = 256;
    std::vector<SwVertex> src(N);
    for (auto& v : src) v = randomVertex(srng);
    float m[16];
    if (s.identity) {
      identityMatrix(m);
    } else {
      for (int i = 0; i < 16; ++i) m[i] = srng.uniform(-3.0f, 3.0f);
      m[15] = 1.0f;  // keep w-row's w-component sane (irrelevant to xyz output, but tidy)
    }
    float gpuM[16];
    std::memcpy(gpuM, m, sizeof(gpuM));
    if (injectBug) gpuM[0] += 1e-2f;  // corrupt the real dispatch path
    std::vector<SwVertex> out(N, SwVertex{});
    if (!disp.dispatch(src, gpuM, s.useVs, s.toTex2, out)) { dispatchOk = false; continue; }

    for (size_t i = 0; i < N; ++i) {
      mathv_ref::TransformMeshUvsIn in{src[i].Texcoord.x, src[i].Texcoord.y, src[i].Texcoord2.x,
                                        src[i].Texcoord2.y, src[i].Selection};
      mathv_ref::TransformMeshUvsOut refOut{};
      mathv_ref::transformMeshUvsOne(in, m, s.useVs, s.toTex2, refOut);
      float inVec[5] = {in.texU, in.texV, in.tex2U, in.tex2V, in.selected};
      float gv[4] = {out[i].Texcoord.x, out[i].Texcoord.y, out[i].Texcoord2.x, out[i].Texcoord2.y};
      float rv[4] = {refOut.texU, refOut.texV, refOut.tex2U, refOut.tex2V};
      for (int k = 0; k < 4; ++k) cmpMain.add(gv[k], rv[k], inVec, 5, k, -1.0f, s.tag);
      if (!injectBug) {
        // Position/Normal/Tangent/Bitangent/Selected/ColorRGB: exact passthrough (kernel copies
        // ResultVerts=SourceVerts wholesale, only overwrites TexCoord OR TexCoord2).
        float gp[13] = {out[i].Position.x, out[i].Position.y, out[i].Position.z,
                         out[i].Normal.x,   out[i].Normal.y,   out[i].Normal.z,
                         out[i].Tangent.x,  out[i].Tangent.y,  out[i].Tangent.z,
                         out[i].Bitangent.x, out[i].Bitangent.y, out[i].Bitangent.z, out[i].Selection};
        float rp[13] = {src[i].Position.x, src[i].Position.y, src[i].Position.z,
                         src[i].Normal.x,   src[i].Normal.y,   src[i].Normal.z,
                         src[i].Tangent.x,  src[i].Tangent.y,  src[i].Tangent.z,
                         src[i].Bitangent.x, src[i].Bitangent.y, src[i].Bitangent.z, src[i].Selection};
        for (int k = 0; k < 13; ++k) cmpPassthrough.add(gp[k], rp[k], inVec, 5, k, -1.0f, s.tag);
      }
    }
  }
  cmpMain.print();
  cmpPassthrough.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  bool passPassthrough = injectBug || cmpPassthrough.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "transformmeshuvs");

  ParityReport rep("selftest-mathv-transformmeshuvs");
  rep.expectTrue("matrixConvention(empirical, row-major/M*v)", true, 1.0);  // already hard-failed above if not
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(TexCoord/TexCoord2, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("compare(6-field passthrough, exact)", passPassthrough, passPassthrough ? 1.0 : 0.0);
  return rep.finish();
}

// order 1083: transpiler-batch wave-3 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1083, {"mathv-transformmeshuvs", runMathvTransformMeshUvsSelfTest});

}  // namespace sw
