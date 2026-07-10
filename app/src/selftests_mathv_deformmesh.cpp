// selftests_mathv_deformmesh.cpp — --selftest-mathv-deformmesh (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-3 transpiler量產批: fuzz the TRANSPILED GPU "deformmesh" kernel
// (app/shaders/deformmesh.metal — glslang+spirv-cross output of TiXL mesh-Deform.hlsl, §10.1 recipe,
// with the wave-3 packed_float3 struct fix + host-ABI Count field documented in that .metal's header)
// against the R-authored CPU oracle (app/src/mathv_ref_deformmesh.h) via direct-kernel dispatch
// (§1.3 — NOT buildEvalGraph). TRANSCENDENTAL class (length()/cos()/sin(), §2 double-layer fraction gate).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// external/tixl/Operators/Lib/mesh/modify/DeformMesh.t3ui: Spherize/TaperAmount/TwistAmount/Radius are
// unbounded sliders (no Min/Max) with small defaults (0/0/0/1) — this TU sweeps a generous [-4,4]-ish
// neighborhood plus special values. TaperAxis/TwistAxis are enum-like float selectors {0,1,2} in
// practice (a TiXL dropdown), fuzzed as clean integer-valued floats {0,1,2,3} (3 = deliberately
// out-of-range, exercising the well-defined TaperAxis fallback and the AMBIGUITY-PINNED TwistAxis UB
// resolution — see deformmesh_params.h/mathv_ref_deformmesh.h header notes) — never fractional near
// an integer boundary, so `switch(int(axis))` branch selection is never itself in question (no
// float->int truncation jitter risk).
//
// ── SCOPE ────────────────────────────────────────────────────────────────────────────────────────
// Normal/Tangent/Bitangent/TexCoord/TexCoord2/Selected/ColorRGB are pure passthrough (unlike
// flipnormals/uvsviewer, ALL 8 PbrVertex fields ARE written here — no untouched-sentinel tooth needed)
// — checked once via EXACT class in a dedicated small comparator. Position is the only non-trivial
// field, checked via the main TRANSCENDENTAL comparator.
//
// ZONE: shell tier; crosses runtime only for SwVertex (PbrVertex's byte-identical host mirror,
// runtime/sw_mesh.h) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_deformmesh.h"
#include "parity_golden_harness.h"
#include "runtime/deformmesh_params.h"
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

struct DeformMeshDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  DeformMeshDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("deformmesh", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~DeformMeshDispatch() { if (pso) pso->release(); }
  DeformMeshDispatch(const DeformMeshDispatch&) = delete;

  bool dispatch(const std::vector<SwVertex>& src, const DeformMeshParams& prmIn,
                std::vector<SwVertex>& dstOut) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)src.size();
    if (n == 0 || dstOut.size() != n) return false;
    MTL::Buffer* bufSrc =
        dev->newBuffer(src.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst =
        dev->newBuffer(dstOut.data(), (NS::UInteger)(n * sizeof(SwVertex)), MTL::ResourceStorageModeShared);
    DeformMeshParams prm = prmIn;
    prm.Count = n;
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&prm, sizeof(prm), DEFORMMESH_Params);
    enc->setBuffer(bufSrc, 0, DEFORMMESH_SourceVerts);
    enc->setBuffer(bufDst, 0, DEFORMMESH_ResultVerts);
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
  v.Texcoord = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Texcoord2 = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  v.Selection = rng.uniform(0.0f, 1.0f);
  v.ColorRgb = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)};
  return v;
}

DeformMeshParams zeroParams() {
  DeformMeshParams p{};
  std::memset(&p, 0, sizeof(p));
  return p;
}

mathv_ref::DeformMeshParamsR toRefParams(const DeformMeshParams& p) {
  return {p.UseVertexSelection, p.Spherize, p.Radius, p.TaperAmount, p.TwistAmount, p.TaperAxis,
          p.TwistAxis,          p.Pivot.x,  p.Pivot.y, p.Pivot.z,    p.TwistPivot.x, p.TwistPivot.y,
          p.TwistPivot.z,       p.Taper2.x, p.Taper2.y};
}

}  // namespace

int runMathvDeformMeshSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-deformmesh] FAIL: no metallib\n"); return 1; }
  DeformMeshDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-deformmesh] FAIL: no kernel\n"); return 1; }

  Rng rng(mathv::mathvSeed("deformmesh"));
  Comparator cmpMain("mathv-deformmesh", EpsSpec::transcendental(), 5);
  Comparator cmpPassthrough("mathv-deformmesh-passthrough", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario {
    float spherize, radius, taperAmt, twistAmt, taperAxis, twistAxis;
    const char* tag;
  };
  const Scenario scenarios[] = {
      {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, "identity"},
      {0.5f, 2.0f, 0.3f, 45.0f, 0.0f, 0.0f, "taper0-twist0"},
      {0.5f, 2.0f, 0.3f, 90.0f, 1.0f, 1.0f, "taper1-twist1"},
      {-0.5f, 3.0f, -0.2f, -180.0f, 2.0f, 2.0f, "taper2-twist2"},
      {1.0f, 5.0f, 1.5f, 720.0f, 3.0f, 0.0f, "taperOOR-well-defined"},
      {1.0f, 5.0f, 0.0f, 360.0f, 0.0f, 3.0f, "twistOOR-ambiguity-pinned"},
      {0.2f, 0.5f, 0.1f, 30.0f, 1.0f, 2.0f, "small-radius"},
  };
  for (const auto& s : scenarios) {
    Rng srng(mathv::mathvSeed(s.tag));
    const size_t N = 512;
    std::vector<SwVertex> src(N);
    for (auto& v : src) v = randomVertex(srng);
    DeformMeshParams p = zeroParams();
    p.UseVertexSelection = (srng.uniform(0.0f, 1.0f) > 0.5f) ? 1.0f : 0.0f;
    p.Spherize = s.spherize;
    p.Radius = s.radius;
    p.TaperAmount = s.taperAmt;
    p.TwistAmount = s.twistAmt;
    p.TaperAxis = s.taperAxis;
    p.TwistAxis = s.twistAxis;
    p.Pivot = {srng.uniform(-3.0f, 3.0f), srng.uniform(-3.0f, 3.0f), srng.uniform(-3.0f, 3.0f)};
    p.TwistPivot = {srng.uniform(-3.0f, 3.0f), srng.uniform(-3.0f, 3.0f), srng.uniform(-3.0f, 3.0f)};
    p.Taper2 = {srng.uniform(-1.0f, 1.0f), srng.uniform(-1.0f, 1.0f)};

    DeformMeshParams gpuP = p;
    if (injectBug) gpuP.TwistAmount += 1e-2f;  // corrupt the real dispatch path (continuous-dependent param)
    std::vector<SwVertex> out(N, SwVertex{});
    if (!disp.dispatch(src, gpuP, out)) { dispatchOk = false; continue; }

    mathv_ref::DeformMeshParamsR rp = toRefParams(p);
    for (size_t i = 0; i < N; ++i) {
      mathv_ref::Vec3f pos{src[i].Position.x, src[i].Position.y, src[i].Position.z};
      mathv_ref::Vec3f refPos = mathv_ref::deformMeshOne(pos, src[i].Selection, rp);
      float inVec[4] = {pos.x, pos.y, pos.z, src[i].Selection};
      float gv[3] = {out[i].Position.x, out[i].Position.y, out[i].Position.z};
      float rv[3] = {refPos.x, refPos.y, refPos.z};
      for (int k = 0; k < 3; ++k) cmpMain.add(gv[k], rv[k], inVec, 4, k, -1.0f, s.tag);
      if (!injectBug) {
        float gp[15] = {out[i].Normal.x,   out[i].Normal.y,   out[i].Normal.z,
                         out[i].Tangent.x,  out[i].Tangent.y,  out[i].Tangent.z,
                         out[i].Bitangent.x,out[i].Bitangent.y,out[i].Bitangent.z,
                         out[i].Texcoord.x, out[i].Texcoord.y, out[i].Texcoord2.x,
                         out[i].Texcoord2.y,out[i].Selection,  out[i].ColorRgb.x};
        float rp15[15] = {src[i].Normal.x,   src[i].Normal.y,   src[i].Normal.z,
                           src[i].Tangent.x,  src[i].Tangent.y,  src[i].Tangent.z,
                           src[i].Bitangent.x,src[i].Bitangent.y,src[i].Bitangent.z,
                           src[i].Texcoord.x, src[i].Texcoord.y, src[i].Texcoord2.x,
                           src[i].Texcoord2.y,src[i].Selection,  src[i].ColorRgb.x};
        for (int k = 0; k < 15; ++k) cmpPassthrough.add(gp[k], rp15[k], inVec, 4, k, -1.0f, s.tag);
      }
    }
  }
  cmpMain.print();
  cmpPassthrough.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  bool passPassthrough = injectBug || cmpPassthrough.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "deformmesh");

  ParityReport rep("selftest-mathv-deformmesh");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(Position, transcendental)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("compare(8-field passthrough, exact)", passPassthrough, passPassthrough ? 1.0 : 0.0);
  return rep.finish();
}

// order 1082: transpiler-batch wave-3 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1082, {"mathv-deformmesh", runMathvDeformMeshSelfTest});

}  // namespace sw
