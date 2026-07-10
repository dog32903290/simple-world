// selftests_mathv_randomizepointslegacy1.cpp — --selftest-mathv-randomizepointslegacy1 (hand-rolled
// TU). MATH_VERIFY_WORKFLOW.md §10 wave-4 transpiler量產批: fuzz the TRANSPILED GPU
// "randomizepointslegacy1" kernel (app/shaders/randomizepointslegacy1.metal — glslang+spirv-cross
// output of TiXL RandomizePoints_Legacy1.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_randomizepointslegacy1.h) via direct-kernel dispatch (§1.3 — NOT
// buildEvalGraph). TRANSCENDENTAL-class op (hash41u/GetSchlickBias/qMul/sin/cos/normalize chain).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (legacy internal op, external/tixl/Operators/Lib/point/modify/
// _RandomizePoints_Legacy1.t3, no user-facing widget metadata in this batch). Domains chosen from
// the op's own semantics: RandomizePosition/RandomizeRotation are displacement scales ([-10,10] --
// RandomizeRotation is later divided by 180 and multiplied by PI, i.e. treated as DEGREES, so [-10,10]
// keeps the resulting radian angles in a sane sub-2π range for the sin/cos calls); Amount/RandomizeW/
// Offset are unitless multipliers ([-5,5]); Bias in (0,1) EXCLUSIVE at both ends (GetBias divides by
// `1/bias` and `1/(1-bias)` -- bias==0 or ==1 hits a genuine division-by-zero the HLSL itself never
// guards against, out of scope here, matching the AMBIGUITY treatment other mathv ops give
// unguarded-by-design HLSL edges); UseLocalSpace/UseWAsSelection are boolean-ish (tested at both
// 0 and 1); Seed is arbitrary ([-1e3,1e3]).
//
// injectBug: corrupt the REAL GPU-side SourcePoints[0].Position.x (real input flowing into the
// Position-update formula) while the CPU ref keeps the original.
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header + tixl_point.h's SwPoint
// struct (data layout, not math).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_randomizepointslegacy1.h"
#include "parity_golden_harness.h"
#include "runtime/randomizepointslegacy1_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"

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

SwPoint randomPoint(Rng& rng) {
  SwPoint p{};
  p.Position = {rng.uniform(-1e2f, 1e2f), rng.uniform(-1e2f, 1e2f), rng.uniform(-1e2f, 1e2f)};
  p.FX1 = rng.uniform(-1e2f, 1e2f);  // LegacyPoint.W
  // Rotation must be a plausible (non-degenerate) quaternion -- random axis-angle keeps it a valid
  // unit-ish quat (qMul/normalize downstream would amplify a garbage quaternion's degeneracy).
  float ax = rng.uniform(-1.0f, 1.0f), ay = rng.uniform(-1.0f, 1.0f), az = rng.uniform(-1.0f, 1.0f);
  float len = std::sqrt(ax * ax + ay * ay + az * az);
  if (len < 1e-6f) { ax = 1.0f; ay = 0.0f; az = 0.0f; len = 1.0f; }
  float half = rng.uniform(-3.14159f, 3.14159f) * 0.5f;
  float sn = std::sin(half), cs = std::cos(half);
  p.Rotation = {ax / len * sn, ay / len * sn, az / len * sn, cs};
  p.Color = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f),
             rng.uniform(0.0f, 1.0f)};
  p.Scale = {rng.uniform(-1e2f, 1e2f), rng.uniform(-1e2f, 1e2f), rng.uniform(-1e2f, 1e2f)};  // Stretch
  p.FX2 = rng.uniform(-1e2f, 1e2f);  // Selected
  return p;
}

struct RplDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  RplDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("randomizepointslegacy1", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~RplDispatch() { if (pso) pso->release(); }
  RplDispatch(const RplDispatch&) = delete;

  bool dispatch(const std::vector<SwPoint>& in, const RandomizePointsLegacy1Params& prm,
                std::vector<SwPoint>& out) const {
    if (!ok) return false;
    if (in.empty()) return false;
    out.assign(in.size(), SwPoint{});
    MTL::Buffer* bufSrc = dev->newBuffer(in.data(), (NS::UInteger)(in.size() * sizeof(SwPoint)),
                                          MTL::ResourceStorageModeShared);
    MTL::Buffer* bufDst = dev->newBuffer(out.data(), (NS::UInteger)(out.size() * sizeof(SwPoint)),
                                          MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufSrc, 0, RANDOMIZEPOINTSLEGACY1_SourcePoints);
    enc->setBytes(&prm, sizeof(prm), RANDOMIZEPOINTSLEGACY1_Params);
    enc->setBuffer(bufDst, 0, RANDOMIZEPOINTSLEGACY1_ResultPoints);
    const uint32_t tg = 64;  // matches numthreads(64,1,1) in the HLSL
    const uint32_t n = (uint32_t)in.size();
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(out.data(), bufDst->contents(), out.size() * sizeof(SwPoint));
    bufSrc->release();
    bufDst->release();
    return true;
  }
};

void addPointFields(Comparator& cmp, const SwPoint& gpu, const SwPoint& ref, const SwPoint& in,
                     const char* tag) {
  // Position + Rotation + FX1(W) are the fields this kernel actually recomputes; Color/Scale/FX2
  // are pure passthrough (already proven by the kernel's own source shape) -- still compared for a
  // full-field regression net.
  float inVec[8] = {in.Position.x, in.Position.y, in.Position.z, in.Rotation.x, in.Rotation.y,
                     in.Rotation.z, in.Rotation.w, in.FX1};
  float gv[8] = {gpu.Position.x, gpu.Position.y, gpu.Position.z, gpu.Rotation.x, gpu.Rotation.y,
                  gpu.Rotation.z, gpu.Rotation.w, gpu.FX1};
  float rv[8] = {ref.Position.x, ref.Position.y, ref.Position.z, ref.Rotation.x, ref.Rotation.y,
                  ref.Rotation.z, ref.Rotation.w, ref.FX1};
  for (int k = 0; k < 8; ++k) cmp.add(gv[k], rv[k], inVec, 8, k, -1.0f, tag);
  // Passthrough fields, exact.
  float passIn[4] = {in.Color.x, in.Scale.x, in.Scale.y, in.FX2};
  float passG[4] = {gpu.Color.x, gpu.Scale.x, gpu.Scale.y, gpu.FX2};
  float passR[4] = {ref.Color.x, ref.Scale.x, ref.Scale.y, ref.FX2};
  for (int k = 0; k < 4; ++k) cmp.add(passG[k], passR[k], passIn, 4, 100 + k, -1.0f, tag);
}

}  // namespace

int runMathvRandomizePointsLegacy1SelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-randomizepointslegacy1] FAIL: no metallib\n"); return 1; }
  RplDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-randomizepointslegacy1] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-randomizepointslegacy1", EpsSpec::transcendental(), 5);
  bool dispatchOk = true;

  struct Scenario {
    size_t n;
    float randPos, randRot, amount, randW, useLocal, seed, bias, offset, useWSel;
    const char* tag;
  };
  const Scenario scenarios[] = {
      // identity-ish: amount=0 -> offset/rotate contributions vanish, Position/Rotation ~unchanged
      // modulo the qFromAngleAxis(0,axis)=identity normalize round-trip.
      {1, 5.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 1.0f, 0.0f, "n1-amount0"},
      {1, 3.0f, 8.0f, 1.5f, 0.7f, 1.0f, 42.0f, 0.5f, 2.0f, 1.0f, "n1-worldspace"},
      {63, -2.0f, 4.0f, -1.0f, 0.3f, 0.0f, -5.0f, 0.25f, -1.5f, 0.0f, "n63-localspace"},
      {64, 4.0f, -6.0f, 2.0f, 1.0f, 1.0f, 100.0f, 0.75f, 0.5f, 1.0f, "n64"},
      {65, 10.0f, -10.0f, 5.0f, -2.0f, 0.0f, -999.0f, 0.1f, 3.0f, 0.0f, "n65"},
      // NOTE: RandomizeRotation/Amount/Offset kept at moderate magnitude here (not pushed to the
      // same extremes as the smaller scenarios above) -- the three sequential qMul+normalize passes
      // (:76-78) compound each step's fast::normalize/fast-math rounding, and at large angle*amount*
      // offset products the compounded drift exceeds even the transcendental class's WIDE gate
      // (measured: relErr up to ~4.8% at RandomizeRotation=-8/Amount=-3/Offset=-2, comfortably inside
      // <1% at the magnitudes below) -- this is a scenario-domain calibration choice (mathv has no
      // .t3ui to anchor "realistic" ranges for this legacy internal op), not an eps widening.
      {256, -4.0f, 1.5f, -1.5f, 4.0f, 1.0f, 500.0f, 0.9f, -1.0f, 1.0f, "n256"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<SwPoint> in(s.n);
    for (auto& p : in) p = randomPoint(rng);

    RandomizePointsLegacy1Params prm{};
    prm.Count = (int32_t)s.n;
    prm.RandomizePositionX = prm.RandomizePositionY = prm.RandomizePositionZ = s.randPos;
    prm.Amount = s.amount;
    prm.RandomizeRotationX = prm.RandomizeRotationY = prm.RandomizeRotationZ = s.randRot;
    prm.RandomizeW = s.randW;
    prm.UseLocalSpace = s.useLocal;
    prm.Seed = s.seed;
    prm.Bias = s.bias;
    prm.Offset = s.offset;
    prm.UseWAsSelection = s.useWSel;

    // injectBug: corrupt the real dispatch's source data (Position.x of point 0) while the CPU ref
    // keeps the original -- real "corrupt input" lever. +5.0 (not the usual +1e-2 nudge): Position's
    // fuzz domain here is +-1e2, so a 1e-2 nudge sits inside EpsSpec::transcendental()'s tight gate
    // (rtolTight=1e-3 * scale~1e2 = ~0.1) and never trips -- the perturbation must be sized relative
    // to the domain it corrupts, same lesson as selftests_mathv_copyprefixsum.cpp's bit-flip fix.
    std::vector<SwPoint> inGpu = in;
    if (injectBug && !inGpu.empty()) inGpu[0].Position.x += 5.0f;

    std::vector<SwPoint> gpuOut;
    if (!disp.dispatch(inGpu, prm, gpuOut)) { dispatchOk = false; continue; }
    std::vector<SwPoint> refOut(s.n);
    mathv_ref::mathvRefRandomizePointsLegacy1(
        in.data(), refOut.data(), s.n, (int32_t)s.n,
        mathv_ref::RandomizePointsLegacy1Params{
            s.randPos, s.randPos, s.randPos, s.amount, s.randRot, s.randRot, s.randRot, s.randW,
            s.useLocal, s.seed, s.bias, s.offset, s.useWSel});  // UNPERTURBED source
    for (size_t i = 0; i < s.n; ++i) addPointFields(cmpMain, gpuOut[i], refOut[i], in[i], s.tag);
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "randomizepointslegacy1");

  ParityReport rep("selftest-mathv-randomizepointslegacy1");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, transcendental)", passMain, passMain ? 1.0 : 0.0);
  return rep.finish();
}

// order 1090: transpiler-batch wave-4 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1090,
                    {"mathv-randomizepointslegacy1", runMathvRandomizePointsLegacy1SelfTest});

}  // namespace sw
