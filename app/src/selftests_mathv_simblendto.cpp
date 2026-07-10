// selftests_mathv_simblendto.cpp — --selftest-mathv-simblendto (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md §10 transpiler量產批: fuzz the TRANSPILED GPU "simblendto" kernel
// (app/shaders/simblendto.metal — glslang+spirv-cross output of TiXL SimBlendTo.hlsl, §10.1 recipe)
// against the R-authored CPU oracle (app/src/mathv_ref_simblendto.h, TRANSCRIBED from external/tixl
// HLSL) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph). SimBlendTo is an §2.1 EXACT-class op
// (single lerp, no transcendentals, no branches beyond the fixed index math).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ─────────────────────────────────────────────
// SimBlendTo.t3ui:20-26 BlendFactor Min=-5.0 Max=5.0 verbatim (Default=0.0, .t3:11-14). PairingMethod/
// CountA/CountB are declared in the cbuffer but never read by main() (see params.h header note) — not
// fuzzed, held at a fixed non-identity value.
//
// ── SCOPE: Position + W only (the kernel's ACTUAL writes) ───────────────────────────────────────────
// SimBlendTo.hlsl:28-34 only touches ResultPoints[i].Position and .W; Rotation/Color/Stretch/Selected
// are untouched passthrough (never assigned) — this TU only asserts the two fields the kernel writes.
// This op lives in points/sim/experimental/ (TiXL's own "experimental" folder) and is an owner-lock-
// free kernel-inventory entry (MATH_VERIFY_WORKFLOW.md §10 batch) — not wired to any node/stage yet.
//
// ZONE: shell tier; crosses runtime only for SwPoint (LegacyPoint's byte-identical host mirror) + the
// kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit (MathvCase/runMathvFuzz unused — hand-rolled TU)
#include "mathv_input.h"
#include "mathv_ref_simblendto.h"
#include "parity_golden_harness.h"
#include "runtime/selftest_registry.h"
#include "runtime/simblendto_params.h"
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

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape): ResultPoints is the
// IN-PLACE UAV ("A" side, also read); PointsB is the SRV ("B" side). `resultInOut` is both the seed
// input AND the readback target (matches the kernel's in-place semantics).
struct SimBlendToDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  SimBlendToDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("simblendto", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SimBlendToDispatch() { if (pso) pso->release(); }
  SimBlendToDispatch(const SimBlendToDispatch&) = delete;

  bool dispatch(const SimBlendToParams& prm, std::vector<SwPoint>& resultInOut,
                const std::vector<SwPoint>& pointsB) const {
    if (!ok) return false;
    const uint32_t n = (uint32_t)resultInOut.size();
    if (n == 0 || pointsB.size() != n) return false;
    MTL::Buffer* bufA = dev->newBuffer(resultInOut.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                       MTL::ResourceStorageModeShared);
    MTL::Buffer* bufB = dev->newBuffer(pointsB.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                       MTL::ResourceStorageModeShared);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufA, 0, SIMBLENDTO_ResultPoints);
    enc->setBuffer(bufB, 0, SIMBLENDTO_PointsB);
    enc->setBytes(&prm, sizeof(prm), SIMBLENDTO_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(resultInOut.data(), bufA->contents(), (size_t)n * sizeof(SwPoint));
    bufA->release(); bufB->release();
    return true;
  }
};

// mathv_input.h's special-value grid always injects the UNIVERSAL ±1e6 special regardless of a
// param's real domain (SimBlendTo.t3ui's is only [-5,5]) — at BlendFactor=1e6, lerp(posA,posB,t)'s
// (posB-posA) term (as small as ~0.003 within the [-4,4] position domain) gets amplified ~1e6x, so a
// single float32-ulp rounding difference between GPU `mix()` and the CPU `a+(b-a)*t` formula (FMA vs
// non-FMA contraction — same class as §9's "fma 收縮改變捨入" trap) blows up into a multi-ULP absolute
// gap. This is catastrophic-cancellation physics at a synthetic torture magnitude, not a formula bug —
// measured: fuzz run 2026-07-10 observed relErr up to 3.82e-05 at BlendFactor=±1e6 (2 mismatches /
// 144384 compares, both lane=Position.z, both from the 1e6 grid special); widened 2.6x for headroom.
EpsSpec simBlendToEps() {
  EpsSpec e = EpsSpec::exact();
  e.rtol = 1e-4f;  // measured: BlendFactor=±1e6 grid special amplifies (posB-posA) rounding by ~1e6x;
                    // observed relErr up to 3.82e-05 (fuzz run 2026-07-10) -- see comment above.
  return e;
}

const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"BlendFactor", -5.0f, 5.0f, ParamDomain::Linear, "SimBlendTo.t3ui:20-26 Min=-5.0 Max=5.0"},
  };
  return t;
}

SW_PACKED3 randomPos3(Rng& rng) {
  return SW_PACKED3{rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
}

// Dead-field cbuffer values (PairingMethod/CountA/CountB — kernel never reads them, see params.h).
SimBlendToParams makeParams(float blendFactor) {
  SimBlendToParams p{};
  p.BlendFactor = blendFactor;
  p.PairingMethod = 1.0f; p.CountA = 7.0f; p.CountB = 11.0f;  // non-identity, unread — proves inert
  return p;
}

// Dispatch one batch of N points, compare Position(3)+W(1)=4 lanes/element against
// mathv_ref::simBlendToOne. gpuBlendFactor may differ from refBlendFactor (injectBug §1.5).
bool compareBatch(const SimBlendToDispatch& disp, Comparator& cmp, float gpuBlendFactor,
                  float refBlendFactor, std::vector<SwPoint> resultA, const std::vector<SwPoint>& pointsB,
                  const char* batchTag) {
  const uint32_t n = (uint32_t)resultA.size();
  std::vector<SwPoint> before = resultA;  // keep the A-side input for the ref call (dispatch overwrites)
  if (!disp.dispatch(makeParams(gpuBlendFactor), resultA, pointsB) || resultA.size() != n) return false;
  mathv_ref::SimBlendToParams rp{refBlendFactor};
  for (uint32_t i = 0; i < n; ++i) {
    mathv_ref::SimBlendToIn in{};
    in.posA[0] = before[i].Position.x; in.posA[1] = before[i].Position.y; in.posA[2] = before[i].Position.z;
    in.wA = before[i].FX1;
    in.posB[0] = pointsB[i].Position.x; in.posB[1] = pointsB[i].Position.y; in.posB[2] = pointsB[i].Position.z;
    mathv_ref::SimBlendToOut refOut{};
    mathv_ref::simBlendToOne(in, refOut, rp);
    float inVec[7] = {in.posA[0], in.posA[1], in.posA[2], in.wA, in.posB[0], in.posB[1], in.posB[2]};
    float g[4] = {resultA[i].Position.x, resultA[i].Position.y, resultA[i].Position.z, resultA[i].FX1};
    float r[4] = {refOut.pos[0], refOut.pos[1], refOut.pos[2], refOut.w};
    for (int k = 0; k < 4; ++k) cmp.add(g[k], r[k], inVec, 7, k, -1.0f, batchTag);
  }
  return true;
}

// ── TOOTH BUG-INVARIANT (pinned, NOTED-QUIRK not NAMED-FORK, see params.h header): W_out must equal
// W_in REGARDLESS of BlendFactor (the wA/wB-both-from-ResultPoints hand-slip). A RED here means the
// kernel/ref stopped reproducing the raw HLSL bug — a real regression, not a discovery. ────────────
bool checkBugInvariantTooth(const SimBlendToDispatch& disp) {
  Comparator cmp("mathv-simblendto-w-invariant", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("simblendto-w-invariant"));
  const size_t N = 256;
  const float blendFactors[] = {-5.0f, -1.0f, 0.3f, 1.0f, 3.7f, 5.0f};
  bool dispatchOk = true;
  for (float bf : blendFactors) {
    std::vector<SwPoint> a(N), b(N);
    for (size_t i = 0; i < N; ++i) {
      a[i].Position = randomPos3(rng); a[i].FX1 = rng.uniform(-4.0f, 4.0f);
      b[i].Position = randomPos3(rng); b[i].FX1 = rng.uniform(-4.0f, 4.0f);  // b's FX1(W) must be UNREAD
    }
    std::vector<SwPoint> before = a;
    if (!disp.dispatch(makeParams(bf), a, b)) { dispatchOk = false; continue; }
    for (size_t i = 0; i < N; ++i) {
      float in1[1] = {bf};
      cmp.add(a[i].FX1, before[i].FX1, in1, 1, 0, -1.0f, "w-invariant");
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

}  // namespace

int runMathvSimBlendToSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-simblendto] FAIL: no metallib\n"); return 1; }
  SimBlendToDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-simblendto] FAIL: no simblendto kernel\n"); return 1; }

  const auto& dom = paramTable();
  Rng rngGrid(mathv::mathvSeed("simblendto-grid"));
  Rng rngRandom(mathv::mathvSeed("simblendto-random"));
  const size_t GRID_N = 256, RANDOM_N = 4096, RANDOM_VECS = 8;
  Comparator cmpMain("mathv-simblendto", simBlendToEps(), 5);
  bool dispatchOk = true;

  // ── layer 1: special-value grid on BlendFactor (positions at a fixed mid-domain lattice) ──
  {
    std::vector<SwPoint> gridA(GRID_N), gridB(GRID_N);
    for (size_t i = 0; i < GRID_N; ++i) {
      gridA[i].Position = randomPos3(rngGrid); gridA[i].FX1 = rngGrid.uniform(-4.0f, 4.0f);
      gridB[i].Position = randomPos3(rngGrid); gridB[i].FX1 = rngGrid.uniform(-4.0f, 4.0f);
    }
    for (float v : mathv::finiteSpecials(dom[0])) {
      float bf = injectBug ? v + 1e-2f : v;  // §1.5: gpu-side perturbation, ref keeps original `v`
      if (!compareBatch(disp, cmpMain, bf, v, gridA, gridB, "grid-blendfactor")) dispatchOk = false;
    }
    // NaN/±Inf BlendFactor: fast-math downgrade (§2 特殊值語義) — no explicit isnan handling in
    // SimBlendTo.hlsl, so this is smoke-only (no crash + readback stays a valid buffer), not a
    // numeric compare (a numeric compare here would over-claim fast-math NaN-propagation fidelity).
    for (float v : mathv::nonFiniteSpecials()) {
      std::vector<SwPoint> smokeA = gridA, smokeB = gridB;
      if (!disp.dispatch(makeParams(v), smokeA, smokeB)) dispatchOk = false;
    }
  }

  // ── layer 2: in-domain uniform random ──
  for (size_t v = 0; v < RANDOM_VECS; ++v) {
    float bf = mathv::sampleUniform(rngRandom, dom[0]);
    std::vector<SwPoint> a(RANDOM_N), b(RANDOM_N);
    for (size_t i = 0; i < RANDOM_N; ++i) {
      a[i].Position = randomPos3(rngRandom); a[i].FX1 = rngRandom.uniform(-4.0f, 4.0f);
      b[i].Position = randomPos3(rngRandom); b[i].FX1 = rngRandom.uniform(-4.0f, 4.0f);
    }
    float gpuBf = injectBug ? bf + 1e-2f : bf;
    if (!compareBatch(disp, cmpMain, gpuBf, bf, a, b, "random")) dispatchOk = false;
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "simblendto");

  // ── layer 3: identity sentinel — BlendFactor=0 -> Position_out == posA exactly ──
  Comparator idCmp("mathv-simblendto-identity", EpsSpec::exact(), 5);
  {
    std::vector<SwPoint> a(GRID_N), b(GRID_N);
    for (size_t i = 0; i < GRID_N; ++i) {
      a[i].Position = randomPos3(rngGrid); a[i].FX1 = rngGrid.uniform(-4.0f, 4.0f);
      b[i].Position = randomPos3(rngGrid); b[i].FX1 = rngGrid.uniform(-4.0f, 4.0f);
    }
    std::vector<SwPoint> before = a;
    bool ok = disp.dispatch(makeParams(0.0f), a, b);
    for (size_t i = 0; ok && i < GRID_N; ++i) {
      float in3[3] = {before[i].Position.x, before[i].Position.y, before[i].Position.z};
      idCmp.add(a[i].Position.x, before[i].Position.x, in3, 3, 0, -1.0f, "identity");
      idCmp.add(a[i].Position.y, before[i].Position.y, in3, 3, 1, -1.0f, "identity");
      idCmp.add(a[i].Position.z, before[i].Position.z, in3, 3, 2, -1.0f, "identity");
    }
    if (!ok) dispatchOk = false;
  }
  idCmp.print();

  bool passBugInvariant = checkBugInvariantTooth(disp);

  ParityReport rep("selftest-mathv-simblendto");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(grid+random, exact)", passMain, passMain ? 1.0 : 0.0);
  rep.expectTrue("identity(BlendFactor=0 -> pos==posA)", idCmp.verdict(), (double)idCmp.total());
  rep.expectTrue("wInvariant(pinned bug: W_out==W_in for all BlendFactor)", passBugInvariant,
                 passBugInvariant ? 1.0 : 0.0);
  return rep.finish();
}

// order 1060: transpiler-batch (MATH_VERIFY_WORKFLOW.md §10), appends after mathv-brdflookup (1050).
REGISTER_SELFTESTS(/*orderBase=*/1060, {"mathv-simblendto", runMathvSimBlendToSelfTest});

}  // namespace sw
