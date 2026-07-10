// selftests_mathv_pointsimulation.cpp — --selftest-mathv-pointsimulation (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md §10 transpiler量產批: fuzz the TRANSPILED GPU "pointsimulation" kernel
// (app/shaders/pointsimulation.metal — glslang+spirv-cross output of TiXL PointSimulation.hlsl, §10.1
// recipe, with the §10.5① GetDimensions substitution) against the R-authored CPU oracle
// (app/src/mathv_ref_pointsimulation.h) via direct-kernel dispatch (§1.3). PointSimulation blends via
// qSlerp on Rotation (acos/sin) -> §2.2 TRANSCENDENTAL class for the whole batch (applied uniformly to
// the affine fields too — strictly looser there, no false-negative risk, simpler than splitting).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ─────────────────────────────────────────────
// PointSimulation.t3ui:… MixOriginal carries NO Min/Max (only Scale=0.001), Default=0.005
// (PointSimulation.t3:14-17) -> §3.2 fallback Default±4 = [-3.995, 4.005]. Reset is a bool
// (Default=false) — tested exhaustively at {0,1} via a dedicated tooth (checkResetTooth), not fuzzed
// continuously through the generic domain table.
//
// ── SCOPE: all six fields (Position/W/Color/Stretch/Selected/Rotation) — PointSimulation.hlsl
// touches every one of them (unlike SimBlendTo/AppendPoints' narrower writes). ─────────────────────
//
// ZONE: shell tier; crosses runtime only for SwPoint (LegacyPoint's byte-identical host mirror) + the
// kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit (MathvCase/runMathvFuzz unused — hand-rolled TU)
#include "mathv_input.h"
#include "mathv_ref_pointsimulation.h"
#include "parity_golden_harness.h"
#include "runtime/pointsimulation_params.h"
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

// direct-kernel dispatch adapter: ResultPoints is the IN-PLACE UAV ("current"), SourcePoints is the
// SRV ("source"). `Count` feeds the §10.5① GetDimensions substitution (SourcePoints_1BufferSize =
// Count*64u) — normally == dispatched thread count, but the boundGuard tooth deliberately sets it
// LOWER to exercise the `idx>=sourcePointcount` early-return branch.
struct PointSimulationDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  PointSimulationDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("pointsimulation", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~PointSimulationDispatch() { if (pso) pso->release(); }
  PointSimulationDispatch(const PointSimulationDispatch&) = delete;

  bool dispatch(uint32_t count, float mixOriginal, float reset, std::vector<SwPoint>& resultInOut,
               const std::vector<SwPoint>& source) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)resultInOut.size();
    if (n == 0 || source.size() != n) return false;
    PointSimulationParams P{count, mixOriginal, reset};
    MTL::Buffer* bufResult = dev->newBuffer(resultInOut.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                            MTL::ResourceStorageModeShared);
    MTL::Buffer* bufSrc = dev->newBuffer(source.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                         MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&P, sizeof(P), POINTSIMULATION_Params);
    enc->setBuffer(bufResult, 0, POINTSIMULATION_ResultPoints);
    enc->setBuffer(bufSrc, 0, POINTSIMULATION_SourcePoints);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(resultInOut.data(), bufResult->contents(), (size_t)n * sizeof(SwPoint));
    bufResult->release(); bufSrc->release();
    return true;
  }
};

SW_PACKED3 randomPos3(Rng& rng) {
  return SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
}
SW_FLOAT4 randomUnitQuat(Rng& rng) {
  float ax, ay, az, len2;
  do {
    ax = rng.uniform(-1.0f, 1.0f); ay = rng.uniform(-1.0f, 1.0f); az = rng.uniform(-1.0f, 1.0f);
    len2 = ax * ax + ay * ay + az * az;
  } while (len2 < 1e-4f);
  float invLen = 1.0f / std::sqrt(len2);
  ax *= invLen; ay *= invLen; az *= invLen;
  float theta = rng.uniform(0.0f, 6.2831853f);
  float s = std::sin(theta * 0.5f), c = std::cos(theta * 0.5f);
  return SW_FLOAT4{ax * s, ay * s, az * s, c};
}
SwPoint randomPoint(Rng& rng) {
  SwPoint p{};
  p.Position = randomPos3(rng); p.FX1 = rng.uniform(-4.0f, 4.0f);
  p.Rotation = randomUnitQuat(rng);
  p.Color = SW_FLOAT4{rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f),
                      rng.uniform(0.0f, 1.0f)};
  p.Scale = randomPos3(rng);  // "Stretch" in TiXL naming
  p.FX2 = rng.uniform(-4.0f, 4.0f);  // "Selected"
  return p;
}
mathv_ref::PointSimulationPoint toRef(const SwPoint& p) {
  mathv_ref::PointSimulationPoint r{};
  r.pos[0] = p.Position.x; r.pos[1] = p.Position.y; r.pos[2] = p.Position.z;
  r.w = p.FX1;
  r.rot[0] = p.Rotation.x; r.rot[1] = p.Rotation.y; r.rot[2] = p.Rotation.z; r.rot[3] = p.Rotation.w;
  r.color[0] = p.Color.x; r.color[1] = p.Color.y; r.color[2] = p.Color.z; r.color[3] = p.Color.w;
  r.stretch[0] = p.Scale.x; r.stretch[1] = p.Scale.y; r.stretch[2] = p.Scale.z;
  r.selected = p.FX2;
  return r;
}

// Compare one batch: N points, dispatched with count==N (bound never trips here — see boundGuard
// tooth for that). 6 fields (Position3+W+Rotation4+Color4+Stretch3+Selected)=15 lanes/element.
bool compareBatch(const PointSimulationDispatch& disp, Comparator& cmp, float gpuMix, float refMix,
                  std::vector<SwPoint> current, const std::vector<SwPoint>& source,
                  const char* batchTag) {
  const uint32_t n = (uint32_t)current.size();
  std::vector<SwPoint> before = current;
  if (!disp.dispatch(n, gpuMix, 0.0f, current, source) || current.size() != n) return false;
  mathv_ref::PointSimulationParams rp{refMix, 0.0f};
  for (uint32_t i = 0; i < n; ++i) {
    mathv_ref::PointSimulationPoint refOut{};
    mathv_ref::pointSimulationOne(i, toRef(before[i]), toRef(source[i]), n, rp, refOut);
    float inVec[2] = {gpuMix, (float)i};
    float g[15] = {current[i].Position.x, current[i].Position.y, current[i].Position.z, current[i].FX1,
                   current[i].Rotation.x, current[i].Rotation.y, current[i].Rotation.z, current[i].Rotation.w,
                   current[i].Color.x, current[i].Color.y, current[i].Color.z, current[i].Color.w,
                   current[i].Scale.x, current[i].Scale.y, current[i].Scale.z};
    float r[15] = {refOut.pos[0], refOut.pos[1], refOut.pos[2], refOut.w,
                   refOut.rot[0], refOut.rot[1], refOut.rot[2], refOut.rot[3],
                   refOut.color[0], refOut.color[1], refOut.color[2], refOut.color[3],
                   refOut.stretch[0], refOut.stretch[1], refOut.stretch[2]};
    // ill-conditioned-lookup exemption (§2 2b / §10.3), Rotation lanes ONLY (4-7): qSlerp's
    // sin(halfAngle*t) argument grows with |MixOriginal|=t; at the universal ±1e6 grid special
    // (mathv_input.h §3.1 — physically outside PointSimulation's real [-3.995,4.005] domain, §3.2
    // fallback), halfAngle*t's ulp becomes a meaningful fraction of sin's 2π period, so fast-math
    // rounding differences between GPU and CPU legally land on a different point of the same periodic
    // curve (same class as AddNoise's noise-lookup exemption, MATH_VERIFY_WORKFLOW.md §2 2b/§9).
    // Candidate iff |t|>=100 (>>the legitimate blend domain) AND lane is a Rotation component;
    // envelope: a qSlerp output is always a UNIT quaternion component (or an unmodified identity/
    // passthrough fallback, also unit-bounded) -- |component|<=1.05 headroom on BOTH sides.
    auto envelopeOk = [](float gg, float rr) { return std::fabs(gg) <= 1.05f && std::fabs(rr) <= 1.05f; };
    for (int k = 0; k < 15; ++k) {
      float bd = (k >= 4 && k <= 7 && std::fabs(gpuMix) >= 100.0f) ? std::fabs(gpuMix) - 100.0f : -1.0f;
      cmp.add(g[k], r[k], inVec, 2, k, bd, batchTag, bd >= 0.0f ? envelopeOk : nullptr);
    }
    // Selected: lane 15 (kept separate — 16 total would overflow neatly, but no functional reason,
    // just avoids a magic-number array resize above).
    cmp.add(current[i].FX2, refOut.selected, inVec, 2, 15, -1.0f, batchTag);
  }
  return true;
}

// ── TOOTH RESET: Reset=1 must unconditionally copy SourcePoints, ignoring MixOriginal entirely. ──
bool checkResetTooth(const PointSimulationDispatch& disp) {
  Comparator cmp("mathv-pointsimulation-reset", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("pointsimulation-reset"));
  const uint32_t N = 128;
  std::vector<SwPoint> current(N), source(N);
  for (uint32_t i = 0; i < N; ++i) { current[i] = randomPoint(rng); source[i] = randomPoint(rng); }
  std::vector<SwPoint> out = current;
  bool ok = disp.dispatch(N, /*mixOriginal=*/0.7f, /*reset=*/1.0f, out, source);
  for (uint32_t i = 0; ok && i < N; ++i) {
    float in1[1] = {(float)i};
    cmp.add(out[i].Position.x, source[i].Position.x, in1, 1, 0, -1.0f, "reset");
    cmp.add(out[i].FX1, source[i].FX1, in1, 1, 1, -1.0f, "reset");
    cmp.add(out[i].Rotation.w, source[i].Rotation.w, in1, 1, 2, -1.0f, "reset");
  }
  cmp.print();
  return ok && cmp.verdict();
}

// ── TOOTH NAN-TRAP: three independent NaN triggers (orgW / currentW / current.Position.x), each must
// force a full reset-from-source — PINNED per mathv_ref_pointsimulation.h's :34 transcription (the
// CURRENT-side-only Position.x asymmetry is deliberate, not a typo — see file header). ──────────────
bool checkNanTrapTooth(const PointSimulationDispatch& disp) {
  Comparator cmp("mathv-pointsimulation-nan-trap", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("pointsimulation-nan-trap"));
  const float kNaN = std::nanf("");
  const char* names[3] = {"orgW-nan", "currentW-nan", "currentPosX-nan"};
  for (int which = 0; which < 3; ++which) {
    SwPoint current = randomPoint(rng), source = randomPoint(rng);
    if (which == 0) source.FX1 = kNaN;
    else if (which == 1) current.FX1 = kNaN;
    else current.Position.x = kNaN;
    std::vector<SwPoint> curV(1, current), srcV(1, source);
    bool ok = disp.dispatch(1, /*mixOriginal=*/0.3f, /*reset=*/0.0f, curV, srcV);
    if (!ok) { printf("[mathv-pointsimulation-nan-trap] %s: dispatch FAILED\n", names[which]); continue; }
    float in1[1] = {(float)which};
    cmp.add(curV[0].Position.y, source.Position.y, in1, 1, 0, -1.0f, names[which]);
    cmp.add(curV[0].FX2, source.FX2, in1, 1, 1, -1.0f, names[which]);
    printf("[mathv-pointsimulation-nan-trap] %s: gpu.pos.y=%.6g want(source)=%.6g\n", names[which],
           curV[0].Position.y, source.Position.y);
  }
  cmp.print();
  return cmp.verdict();
}

// ── TOOTH BOUND-GUARD: Count < dispatched thread count -> the excess threads must leave ResultPoints
// UNCHANGED (the §10.5① substituted GetDimensions guard, `idx>=sourcePointcount` early-return). ──────
bool checkBoundGuardTooth(const PointSimulationDispatch& disp) {
  Comparator cmp("mathv-pointsimulation-bound-guard", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("pointsimulation-bound-guard"));
  const uint32_t N = 64, COUNT = 40;  // threads [COUNT, N) must be untouched
  std::vector<SwPoint> current(N), source(N);
  for (uint32_t i = 0; i < N; ++i) { current[i] = randomPoint(rng); source[i] = randomPoint(rng); }
  std::vector<SwPoint> before = current;
  bool ok = disp.dispatch(COUNT, /*mixOriginal=*/0.5f, /*reset=*/0.0f, current, source);
  for (uint32_t i = COUNT; ok && i < N; ++i) {
    float in1[1] = {(float)i};
    cmp.add(current[i].Position.x, before[i].Position.x, in1, 1, 0, -1.0f, "beyond-count");
    cmp.add(current[i].FX1, before[i].FX1, in1, 1, 1, -1.0f, "beyond-count");
  }
  cmp.print();
  return ok && cmp.verdict();
}

}  // namespace

int runMathvPointSimulationSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-pointsimulation] FAIL: no metallib\n"); return 1; }
  PointSimulationDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-pointsimulation] FAIL: no pointsimulation kernel\n"); return 1; }

  const ParamDomain dom{"MixOriginal", -3.995f, 4.005f, ParamDomain::Linear,
                        "PointSimulation.t3:14-17 Default=0.005, no t3ui Min/Max -> §3.2 Default±4"};
  Rng rngGrid(mathv::mathvSeed("pointsimulation-grid"));
  Rng rngRandom(mathv::mathvSeed("pointsimulation-random"));
  const size_t GRID_N = 128, RANDOM_N = 1024, RANDOM_VECS = 8;
  Comparator cmpMain("mathv-pointsimulation", EpsSpec::transcendental(), 5);
  bool dispatchOk = true;

  // ── layer 1: special-value grid on MixOriginal (finite specials only — Reset stays 0, points
  // stay finite/non-NaN so this layer probes the BLEND formula, not the reset/NaN branches) ──
  {
    std::vector<SwPoint> gridCur(GRID_N), gridSrc(GRID_N);
    for (size_t i = 0; i < GRID_N; ++i) { gridCur[i] = randomPoint(rngGrid); gridSrc[i] = randomPoint(rngGrid); }
    for (float v : mathv::finiteSpecials(dom)) {
      float gpuMix = injectBug ? v + 1e-2f : v;  // §1.5: continuous dependency (blend weight)
      if (!compareBatch(disp, cmpMain, gpuMix, v, gridCur, gridSrc, "grid-mixoriginal")) dispatchOk = false;
    }
  }
  // ── layer 2: in-domain uniform random ──
  for (size_t v = 0; v < RANDOM_VECS; ++v) {
    float mix = mathv::sampleUniform(rngRandom, dom);
    std::vector<SwPoint> cur(RANDOM_N), src(RANDOM_N);
    for (size_t i = 0; i < RANDOM_N; ++i) { cur[i] = randomPoint(rngRandom); src[i] = randomPoint(rngRandom); }
    float gpuMix = injectBug ? mix + 1e-2f : mix;
    if (!compareBatch(disp, cmpMain, gpuMix, mix, cur, src, "random")) dispatchOk = false;
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "pointsimulation");

  // ── identity sentinel: MixOriginal=0 -> output == current (proves params reach the kernel) ──
  Comparator idCmp("mathv-pointsimulation-identity", EpsSpec::exact(), 5);
  {
    std::vector<SwPoint> cur(GRID_N), src(GRID_N);
    for (size_t i = 0; i < GRID_N; ++i) { cur[i] = randomPoint(rngGrid); src[i] = randomPoint(rngGrid); }
    std::vector<SwPoint> before = cur;
    bool ok = disp.dispatch((uint32_t)GRID_N, 0.0f, 0.0f, cur, src);
    for (size_t i = 0; ok && i < GRID_N; ++i) {
      float in3[3] = {before[i].Position.x, before[i].Position.y, before[i].Position.z};
      idCmp.add(cur[i].Position.x, before[i].Position.x, in3, 3, 0, -1.0f, "identity");
      idCmp.add(cur[i].FX1, before[i].FX1, in3, 3, 1, -1.0f, "identity");
      idCmp.add(cur[i].Rotation.w, before[i].Rotation.w, in3, 3, 2, -1.0f, "identity");
    }
    if (!ok) dispatchOk = false;
  }
  idCmp.print();

  bool passReset = checkResetTooth(disp);
  bool passNanTrap = checkNanTrapTooth(disp);
  bool passBoundGuard = checkBoundGuardTooth(disp);

  ParityReport rep("selftest-mathv-pointsimulation");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(grid+random, transcendental/qSlerp)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("identity(MixOriginal=0 -> output==current)", idCmp.verdict(), (double)idCmp.total());
  rep.expectTrue("reset(Reset=1 -> unconditional source copy)", passReset, passReset ? 1.0 : 0.0);
  rep.expectTrue("nanTrap(pinned: 3 independent NaN triggers -> reset)", passNanTrap,
                 passNanTrap ? 1.0 : 0.0);
  rep.expectTrue("boundGuard(Count<dispatch -> excess threads untouched)", passBoundGuard,
                 passBoundGuard ? 1.0 : 0.0);
  return rep.finish();
}

// order 1062: transpiler-batch (MATH_VERIFY_WORKFLOW.md §10), appends after mathv-appendpoints (1061).
REGISTER_SELFTESTS(/*orderBase=*/1062, {"mathv-pointsimulation", runMathvPointSimulationSelfTest});

}  // namespace sw
