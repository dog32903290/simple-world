// selftests_mathv_appendpoints.cpp — --selftest-mathv-appendpoints (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md §10 transpiler量產批: fuzz the TRANSPILED GPU "appendpoints" kernel
// (app/shaders/appendpoints.metal — glslang+spirv-cross output of TiXL AppendPoints.hlsl, §10.1
// recipe) against the R-authored CPU oracle (app/src/mathv_ref_appendpoints.h) via direct-kernel
// dispatch (§1.3). AppendPoints is an §2.3 BRANCHY-class op (three branches keyed on integer-ish
// thresholds derived from CountA/CountB — NOT a Branchy-fp-jitter case, the branches are exact-integer
// boundaries so this TU uses EpsSpec::exact() and instead sweeps EVERY boundary explicitly).
//
// ── ParamDomain provenance ────────────────────────────────────────────────────────────────────────
// CountA/CountB have NO t3ui Min/Max: _AppendPoints.t3 (external/tixl SHA 395c4c55,
// Operators/Lib/point/_internal/_AppendPoints.t3) wires them from GetBufferComponents->IntToFloat on
// Points1/Points2's REAL element counts — an internal glue op, not a user-authored input (see
// runtime/appendpoints_params.h). The universal ±1e6/denormal grid (mathv_input.h §3.1) is physically
// meaningless for a buffer-length-derived count (would require multi-MB GPU allocations per probe just
// to stay in-bounds) — this TU instead fuzzes CountA/CountB over a bounded, buffer-safe domain
// [-1.0, 30.0] (SAFETY note below) and exhaustively walks the THREE boundary conditions by construction
// (countA-1 bag-end, countA+round(CountB+0.5) bag-end, idx>CountA+CountB catch-all) rather than relying
// on the generic special-value grid to stumble onto them by chance.
//
// ── SAFETY (why the domain is bounded, not universal) ────────────────────────────────────────────
// Metal device-buffer OOB reads are UB (unlike HLSL/D3D's defined-zero OOB reads) — CountA_param in
// [-1.0, 30.0] keeps CountA_param+1.5 >= 0.5 (never negative-to-uint, itself UB) and every derived index
// (countA, countA+round(CountB+0.5), idx) stays inside the FIXED_CAP=64 buffers this TU always allocates
// (max derived magnitude ~61, comfortable margin below 64).
//
// ── SCOPE: Position + W only ──────────────────────────────────────────────────────────────────────
// AppendPoints.hlsl:29-40 struct-copies the WHOLE point on the copy branches (Position/Rotation/Color/
// Stretch/Selected all move verbatim) but only ever WRITES .W directly (the NaN markers) — this TU
// asserts Position (proves the struct-copy branch selection is right) + W (proves both NaN markers AND
// the pass-through-on-OOB semantics).
//
// ZONE: shell tier; crosses runtime only for SwPoint (LegacyPoint's byte-identical host mirror) + the
// kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit (MathvCase/runMathvFuzz unused — hand-rolled TU)
#include "mathv_input.h"
#include "mathv_ref_appendpoints.h"
#include "parity_golden_harness.h"
#include "runtime/appendpoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"

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
using mathv::ParamDomain;
using mathv::Rng;

constexpr uint32_t FIXED_CAP = 64;  // see file header SAFETY note

// direct-kernel dispatch adapter: ResultPoints is a pure OUTPUT UAV but IN-PLACE (the OOB branch only
// overwrites .W, leaving the rest at whatever `resultSeed` was — see mathv_ref_appendpoints.h).
struct AppendPointsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  AppendPointsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("appendpoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~AppendPointsDispatch() { if (pso) pso->release(); }
  AppendPointsDispatch(const AppendPointsDispatch&) = delete;

  // resultSeed provides the "before" content the OOB branch's partial write is layered onto (FIXED_CAP
  // elements); points1/points2 are FIXED_CAP-element SRV source arrays. Returns FIXED_CAP-element output.
  bool dispatch(const AppendPointsParams& prm, const std::vector<SwPoint>& resultSeed,
               const std::vector<SwPoint>& points1, const std::vector<SwPoint>& points2,
               std::vector<SwPoint>& out) const {
    if (!ok || resultSeed.size() != FIXED_CAP || points1.size() != FIXED_CAP ||
        points2.size() != FIXED_CAP)
      return false;
    const size_t bytes = (size_t)FIXED_CAP * sizeof(SwPoint);
    MTL::Buffer* bufResult = dev->newBuffer(resultSeed.data(), (NS::UInteger)bytes,
                                            MTL::ResourceStorageModeShared);
    MTL::Buffer* buf1 = dev->newBuffer(points1.data(), (NS::UInteger)bytes,
                                       MTL::ResourceStorageModeShared);
    MTL::Buffer* buf2 = dev->newBuffer(points2.data(), (NS::UInteger)bytes,
                                       MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&prm, sizeof(prm), APPENDPOINTS_Params);
    enc->setBuffer(bufResult, 0, APPENDPOINTS_ResultPoints);
    enc->setBuffer(buf1, 0, APPENDPOINTS_Points1);
    enc->setBuffer(buf2, 0, APPENDPOINTS_Points2);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((FIXED_CAP + tg - 1) / tg, 1, 1),
                              MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(FIXED_CAP, SwPoint{});
    std::memcpy(out.data(), bufResult->contents(), bytes);
    bufResult->release(); buf1->release(); buf2->release();
    return true;
  }
};

SW_PACKED3 randomPos3(Rng& rng) {
  return SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
}
std::vector<SwPoint> randomPointArray(Rng& rng, float wLo = -4.0f, float wHi = 4.0f) {
  std::vector<SwPoint> v(FIXED_CAP);
  for (auto& p : v) { p.Position = randomPos3(rng); p.FX1 = rng.uniform(wLo, wHi); }
  return v;
}

// Compare one full dispatch: GPU vs mathv_ref::appendPointsOne per index, Position(3)+W(1)=4 lanes.
bool compareBatch(const AppendPointsDispatch& disp, Comparator& cmp, const AppendPointsParams& gpuP,
                  const mathv_ref::AppendPointsParams& refP, const std::vector<SwPoint>& seed,
                  const std::vector<SwPoint>& pts1, const std::vector<SwPoint>& pts2,
                  const char* batchTag) {
  std::vector<SwPoint> out;
  if (!disp.dispatch(gpuP, seed, pts1, pts2, out) || out.size() != FIXED_CAP) return false;
  std::vector<mathv_ref::AppendPointsPoint> r1(FIXED_CAP), r2(FIXED_CAP);
  for (uint32_t i = 0; i < FIXED_CAP; ++i) {
    r1[i] = {{pts1[i].Position.x, pts1[i].Position.y, pts1[i].Position.z}, pts1[i].FX1};
    r2[i] = {{pts2[i].Position.x, pts2[i].Position.y, pts2[i].Position.z}, pts2[i].FX1};
  }
  for (uint32_t i = 0; i < FIXED_CAP; ++i) {
    mathv_ref::AppendPointsPoint before{{seed[i].Position.x, seed[i].Position.y, seed[i].Position.z},
                                        seed[i].FX1};
    mathv_ref::AppendPointsPoint refOut{};
    mathv_ref::appendPointsOne(i, before, r1.data(), FIXED_CAP, r2.data(), FIXED_CAP, refP, refOut);
    float inVec[3] = {gpuP.CountA, gpuP.CountB, (float)i};
    float g[4] = {out[i].Position.x, out[i].Position.y, out[i].Position.z, out[i].FX1};
    float r[4] = {refOut.pos[0], refOut.pos[1], refOut.pos[2], refOut.w};
    for (int k = 0; k < 4; ++k) cmp.add(g[k], r[k], inVec, 3, k, -1.0f, batchTag);
  }
  return true;
}

}  // namespace

int runMathvAppendPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-appendpoints] FAIL: no metallib\n"); return 1; }
  AppendPointsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-appendpoints] FAIL: no appendpoints kernel\n"); return 1; }

  Rng rng(mathv::mathvSeed("appendpoints"));
  Comparator cmp("mathv-appendpoints", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  // ── layer 1: exhaustive boundary sweep — CountA/CountB integers in [0,20], EVERY combination hits
  // one of: countA==0 (empty bag A), the countA-1 marker, the countA+round(CountB+0.5) marker, AND the
  // idx>CountA+CountB catch-all (idx ranges the full FIXED_CAP dispatch every time) ──
  for (int a = -1; a <= 20; a += 3) {
    for (int b = -1; b <= 20; b += 4) {
      // §1.5 injectBug, ADAPTED: CountA reaches the output ONLY through the truncating
      // countA=(uint)(CountA+1.5) cast (this op is fully branch/copy-driven, no continuous blend
      // anywhere — see file header) — the canonical "+1e-2" perturbation would cross that integer
      // truncation boundary with near-zero probability (exactly the doc §1.5 anti-pattern: "勿選僅經
      // 離散分支影響輸出的參數"). +1.0f is a DETERMINISTIC substitute: floor(X+1)==floor(X)+1 is an
      // exact identity for any real X, so countA_bugged == countA_correct+1 on every single sample —
      // guaranteed to corrupt the real dispatch path (wrong source array picked near the boundary).
      float gpuA = injectBug ? (float)a + 1.0f : (float)a;
      AppendPointsParams gpuP{gpuA, (float)b};
      mathv_ref::AppendPointsParams refP{(float)a, (float)b};
      std::vector<SwPoint> seed = randomPointArray(rng), pts1 = randomPointArray(rng),
                          pts2 = randomPointArray(rng);
      if (!compareBatch(disp, cmp, gpuP, refP, seed, pts1, pts2, "boundary-sweep")) dispatchOk = false;
    }
  }
  // ── layer 2: in-domain uniform random CountA/CountB (continuous, not integer-snapped) ──
  const ParamDomain dom{"CountA/CountB", -1.0f, 30.0f, ParamDomain::Linear,
                        "internal glue op, no t3ui — bounded buffer-safe domain, see file header"};
  for (int v = 0; v < 8; ++v) {
    float a = mathv::sampleUniform(rng, dom), b = mathv::sampleUniform(rng, dom);
    float gpuA = injectBug ? a + 1.0f : a;  // see boundary-sweep loop's comment for why +1.0f not +1e-2f
    AppendPointsParams gpuP{gpuA, b};
    mathv_ref::AppendPointsParams refP{a, b};
    std::vector<SwPoint> seed = randomPointArray(rng), pts1 = randomPointArray(rng),
                        pts2 = randomPointArray(rng);
    if (!compareBatch(disp, cmp, gpuP, refP, seed, pts1, pts2, "random")) dispatchOk = false;
  }
  cmp.print();
  bool passMain = dispatchOk && cmp.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "appendpoints");

  // ── identity-ish sentinel: CountA=-1,CountB=-1 -> countA=(uint)(0.5)=0, threshold=-2 -> EVERY idx
  // (>=0) is > -2 -> every element hits the OOB catch-all -> Position passes through `seed` unchanged,
  // W becomes NaN everywhere (proves the params genuinely reach the kernel: a kernel that ignored
  // CountA/CountB would still copy from Points1/Points2, diverging from this expectation). ──
  Comparator idCmp("mathv-appendpoints-empty-sentinel", EpsSpec::exact(), 5);
  {
    std::vector<SwPoint> seed = randomPointArray(rng), pts1 = randomPointArray(rng),
                        pts2 = randomPointArray(rng);
    std::vector<SwPoint> out;
    bool ok = disp.dispatch(AppendPointsParams{-1.0f, -1.0f}, seed, pts1, pts2, out);
    for (uint32_t i = 0; ok && i < FIXED_CAP; ++i) {
      float in3[3] = {seed[i].Position.x, seed[i].Position.y, seed[i].Position.z};
      idCmp.add(out[i].Position.x, seed[i].Position.x, in3, 3, 0, -1.0f, "empty-sentinel-pos");
      idCmp.add(out[i].Position.y, seed[i].Position.y, in3, 3, 1, -1.0f, "empty-sentinel-pos");
      idCmp.add(out[i].Position.z, seed[i].Position.z, in3, 3, 2, -1.0f, "empty-sentinel-pos");
      if (!std::isnan(out[i].FX1)) idCmp.add(0.0f, 1.0f, in3, 3, 3, -1.0f, "empty-sentinel-w-must-be-nan");
    }
    if (!ok) dispatchOk = false;
  }
  idCmp.print();

  ParityReport rep("selftest-mathv-appendpoints");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(boundary-sweep+random, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("emptySentinel(CountA=CountB=-1 -> all-OOB, W=NaN)", idCmp.verdict(),
                 (double)idCmp.total());
  return rep.finish();
}

// order 1061: transpiler-batch (MATH_VERIFY_WORKFLOW.md §10), appends after mathv-simblendto (1060).
REGISTER_SELFTESTS(/*orderBase=*/1061, {"mathv-appendpoints", runMathvAppendPointsSelfTest});

}  // namespace sw
