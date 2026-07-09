// selftests_mathv_blendpoints.cpp — --selftest-mathv-blendpoints (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 量產批: fuzz the GPU "blendpoints" kernel (app/shaders/blendpoints.metal)
// against the R-authored CPU oracle (app/src/mathv_ref_blendpoints.h, TRANSCRIBED from external/tixl
// HLSL) via the shared mathv harness (direct-kernel dispatch, §1.3). BlendPoints is Tier-H: quat
// slerp + a dual point-buffer COMBINE (2 SRV in, 1 UAV out), the heaviest structural shape mathv has
// fuzzed yet.
//
// ── COMBINE-CLASS STRUCTURAL LIMIT (why PRIMARY only fuzzes BlendFactor) ──────────────────────────
// The generic 3-layer harness (mathv_harness.h) hands `c.ref` a single element with NO idx/
// resultCount — but BlendPoints's t=i.x/(resultCount-1) (mathv_ref_blendpoints.h :184) is the CORE
// of BlendMode Ranged/RangedSmooth (:226,:236) and of the Scatter*hash11(t) jitter (:241): those are
// fundamentally idx-DEPENDENT, not per-element-pure-functions, so they cannot be represented in the
// generic (P,element)->output shape at all — worse than AddNoise's idx-only-affects-hash41u case,
// since here idx affects the BLEND FACTOR ITSELF for 2 of 5 BlendModes. PRIMARY therefore pins
// BlendMode=Mix(0) (f=BlendFactor directly, :218, no t) and Scatter=0 (zeroes the hash11(t) term
// regardless of t's value, AS LONG AS t stays finite — see the resultCount=2 note in `c.ref` below)
// so a per-element convenience ref call is valid; PairingMode is pinned 0 (aligned countA==countB in
// PRIMARY makes its gate dead regardless, :39/blendpoints.metal:63). Every other axis gets its own
// idx-real dedicated tooth (selftests_mathv_blendpoints_probes.cpp, called from main() below) —
// same "SCOPE split across N teeth, equal strength per axis" discipline as AddNoise's 5-tooth pilot:
//   PRIMARY (3-layer generic harness, THE injectBug tooth) — BlendFactor(1 free param), aligned
//     countA=countB=N. Full 16-channel Result coverage per element (Position/FX1/Rotation(qSlerp)/
//     Color/Scale/FX2 — packPoint/unpackPoint below pack 2 whole SwPoints in, 1 out: inDim=32/
//     outDim=16).
//   TOOTH IDENTITY-AB (this file) — BlendFactor={0,1} boundary hand-check (output==A/==B) +
//     explicit FX1 double-write proof (mathv_ref_blendpoints.h :248-258 NOTED-QUIRK).
//   TOOTH BLENDMODE (probes) — enum {0..4} exhaustive, REAL per-thread idx/t, Ranged/RangedSmooth.
//   TOOTH SCATTER (probes) — Scatter!=0, hash11(t) jitter, real idx/t across a real N-thread batch.
//   TOOTH NOBLEND-NAN (probes) — isnan(Scale.x*Scale.x) collapse (§9 isnan/fast-math trap, PINNED).
//   TOOTH PAIRING-COUNTS (probes) — countA vs countB matrix. PairingMode dead-branch proof
//     (firstIx=max(firstIxA,firstIxB)>=firstIxA==i whenever resultCount==CountA, which
//     blendpoints.metal:48 HARDCODES — so the Adjust skip-gate `i>firstIx` structurally can never
//     fire in the shipped system, for ANY count combo, stronger than the ref's own "resultCount>=
//     max(...)" precondition) + fork[b-count-guard] (blendpoints_params.h:31-34) reproduced and
//     confirmed as an EXPECTED divergence (ref=D3D-OOB-zero, sw=clamp-to-last-B), not a bug.
//   TOOTH OFF-BY-ONE (probes) — over-dispatch idx==resultCount boundary. mathv_ref_blendpoints.h
//     :168-174 documents the transcribed HLSL guard as strict `>` (idx==resultCount WOULD write in
//     HLSL) while blendpoints.metal:52-54 guards `>=` (idx==resultCount does NOT write) — this fix
//     is NOT in either file's enumerated NAMED-FORKS bullet list (only b-count-guard/t-singular are
//     registered there), so this probe records it as a batch-tagged evidence pack per
//     MATH_VERIFY_WORKFLOW.md §7's "無註記分岔" routing — NOT a build failure: sw's `>=` is a
//     deliberate, memory-safe fix of a genuine upstream off-by-one (see the ref file's own
//     AMBIGUITY note for why the ref does not "fix" the bug it transcribes verbatim).
//
// ZONE: shell tier; crosses runtime only for SwPoint + the params ABI header.
#include "selftests_mathv_blendpoints_shared.h"

#include "mathv_harness.h"
#include "mathv_ref_blendpoints.h"
#include "runtime/blendpoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::MathvCase;
using mathv::Rng;
using mathv_bp_shared::BlendPointsDispatch;
using mathv_bp_shared::fillRandomPoint;
using mathv_bp_shared::hostParamsFrom;
using mathv_bp_shared::packPoint;
using mathv_bp_shared::paramTable;
using mathv_bp_shared::QuatF;
using mathv_bp_shared::randomUnitQuat;
using mathv_bp_shared::refParamsFrom;
using mathv_bp_shared::unpackPoint;

// PRIMARY: aligned countA=countB=N (resultCount==CountA, sw's hardcoded convention), BlendMode=Mix
// and Scatter=0 pinned so f==BlendFactor exactly, independent of idx/t — the ONLY axis representable
// in the generic per-element (P,e)->out shape. This is THE injectBug tooth (BlendFactor perturbed
// GPU-side only, per §1.5).
bool runPrimary(const BlendPointsDispatch& disp, bool injectBug) {
  MathvCase c;
  c.opName = "blendpoints";
  c.params = paramTable();  // {BlendFactor}
  c.inDim = 32;             // packed SwPoint A(16) + SwPoint B(16)
  c.outDim = 16;            // packed Result SwPoint
  c.eps = EpsSpec::transcendental();  // qSlerp (acos/sin) governs this bundled comparator's class —
                                       // the other 12/16 lanes are plain lerp/raw-f (exact-class),
                                       // which trivially clears the looser transcendental tight-gate.
  c.inputLo = -4.0f; c.inputHi = 4.0f;
  c.noIdentityReason =
      "combine op: inDim(32, A+B)!=outDim(16, Result) -- no literal input==output identity is "
      "representable in the generic per-element identity layer. Kernel-reaches-params is proven "
      "instead by runIdentityABTooth() below (BlendFactor={0,1} boundary, output==A/==B exactly).";
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in, std::vector<float>& out) {
    const size_t n = in.size() / 32;
    std::vector<SwPoint> A(n), B(n);
    for (size_t i = 0; i < n; ++i) {
      A[i] = unpackPoint(&in[i * 32]);
      B[i] = unpackPoint(&in[i * 32 + 16]);
    }
    BlendPointsParams prm = hostParamsFrom(P[0], /*mode=*/0.0f, /*pairing=*/0.0f, /*width=*/0.5f,
                                            /*scatter=*/0.0f);
    std::vector<SwPoint> res;
    if (!disp.dispatch(prm, A, B, res) || res.size() != n) return false;
    out.resize(n * 16);
    for (size_t i = 0; i < n; ++i) packPoint(res[i], &out[i * 16]);
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    SwPoint A = unpackPoint(in);
    SwPoint B = unpackPoint(in + 16);
    mathv_ref::BlendPointsParams prm =
        refParamsFrom(P[0], /*mode=*/0.0f, /*pairing=*/0.0f, /*width=*/0.5f, /*scatter=*/0.0f);
    SwPoint res{};
    // Per-element convenience call: resultCount is set to 2 (NOT the real per-thread batch size N)
    // purely to keep t=idx/(resultCount-1) FINITE, sidestepping the documented 0/0->NaN singularity
    // at resultCount==1 (mathv_ref_blendpoints.h :180-183/:37 comment). Safe ONLY because Scatter==0
    // here: the scatter term is (hash11(t)-0.5)*scatter*fallOff == (finite)*0*(finite) == 0.0 exactly
    // for ANY finite t (NaN*0==NaN would NOT be safe, which is exactly why resultCount=1 is avoided)
    // -- so the specific t value used here need not match the GPU's real per-position t. BlendMode
    // is pinned Mix, which never reads t outside that scatter term. countA=countB=1 (the REAL bound
    // — idx=0 is in range); resultCount=2 is the only inflated value, chosen only to dodge the
    // singularity.
    mathv_ref::blendPointsOne(&A, 1, &B, 1, /*resultCount=*/2, /*idx=*/0, prm, res);
    packPoint(res, out);
  };
  // Ill-conditioned-lookup exemption wiring (mathv_compare.h §A criteria; MATH_VERIFY_WORKFLOW.md §2
  // 2b, same channel AddNoise's Frequency=+-1e6 finding rides). MEASURED (§4.3 rule 4 batch-tag
  // instrumentation, not guessed): with BlendFactor at its own +-1e6 special-value grid row, ALL 162
  // observed misses (of 581632 compared) are on lanes 4-7 (Rotation) and land EXACTLY on the P[0]==
  // +-1e6 grid-param batches (confirmed by a temporary per-call P[0] print; every other special --
  // 0,+-1e-4,+-0.5,+-1,lo(-1),hi(2),+-1e-40 -- has ZERO misses). Root cause: qSlerp's 3rd argument
  // (`t` in hlslQSlerp/qSlerp, ::100/:117-127 above) IS BlendFactor in Mix mode (f=BlendFactor
  // directly, :218) -- at t=+-1e6 the acos/sin branch (:191-207) evaluates sin(halfAngle*(1-t)) /
  // sin(halfAngle*t) with an argument of magnitude ~1e6 while halfAngle is O(1): float32 ulp at 1e6
  // is ~0.06, i.e. ~1% of sin's 2*pi period, so fast-math re-association of the multiply can legally
  // land on a different point of the cycle -- the exact §2 2b "ill-conditioned lookup" shape, not a
  // ref/MSL formula bug (BlendMode/Scatter/PairingCounts teeth below all use REAL per-thread idx/t
  // and stay clean; this is isolated to PRIMARY's own BlendFactor-as-t special-value probe).
  c.branchDist = [](const std::vector<float>& P, const float* /*in*/, int lane) {
    if (lane < 4 || lane > 7) return -1.0f;  // only Rotation (qSlerp) lanes are candidates
    float t = std::fabs(P[0]);
    return t >= 4096.0f ? (t - 4096.0f) : -1.0f;
  };
  c.illConditionedEnvelope = [](const std::vector<float>& /*P*/, const float* /*in*/, int /*lane*/,
                                float gpu, float ref) {
    // qSlerp's output is either unit-normalized (the acos/sin path) or a raw pass-through of A/B (the
    // cosHalfAngle>=1/<=-1 early-return, :180-183) -- bound generously at the fuzz domain's edge
    // (PRIMARY inputs span [-4,4]) so a genuinely garbage sample (e.g. blown up to ~1e6) still fails.
    const float bound = 4.5f;
    return std::fabs(gpu) <= bound && std::fabs(ref) <= bound;
  };
  return mathv::runMathvFuzz(c, injectBug);
}

// TOOTH IDENTITY-AB: BlendFactor={0,1} boundary hand-check (Mix mode, f=BlendFactor directly, :218):
// Position/Color/Scale/FX2 must equal A (f=0) / B (f=1) exactly -- lerp with t=0 or t=1 is exact in
// IEEE754 (a+0*(b-a)==a, a+1*(b-a)==b as literally written). FX1_out is the double-write quirk in
// action: it ALWAYS equals raw f (BlendFactor), NEVER A.FX1/B.FX1 (mirrors the ref self-check case
// 1's proof, mathv_ref_blendpoints.h :342) -- asserted explicitly so a "cleaned up" port (using the
// dead lerp instead of the final raw-f overwrite) would be caught here.
//
// Rotation needs genuinely-UNIT quaternions for the identity claim to hold (qSlerp normalizes at the
// end) AND a sign-aware check: at t=1 (bf=1.0), qSlerp folds B onto A's shorter arc whenever
// cosHalfAngle<0 (:112-118 above / quat.metal.h:121-126) -- blendA=0,blendB=1 at t=1 regardless, so
// the result is the FOLDED b (== -B_raw when cosHalfAngle<0), not bitwise B_raw. This is the
// well-known quaternion double-cover ambiguity (q and -q represent the identical rotation), not a
// bug -- MEASURED (a first pass asserting bitwise ==B_raw showed rOk=0 with out.Rotation==-expect.
// Rotation component-for-component on 3/3 sampled elements, confirming the sign flip, not a garbled
// result) -- so the check below accepts EITHER sign.
bool runIdentityABTooth(const BlendPointsDispatch& disp) {
  Rng rng(mathv::mathvSeed("blendpoints-identity-ab"));
  const size_t N = 64;
  std::vector<SwPoint> A(N), B(N);
  for (size_t i = 0; i < N; ++i) {
    fillRandomPoint(rng, A[i]);
    fillRandomPoint(rng, B[i]);
  }
  bool ok = true;
  for (float bf : {0.0f, 1.0f}) {
    BlendPointsParams prm = hostParamsFrom(bf, /*mode=*/0.0f, /*pairing=*/0.0f, 0.5f, 0.0f);
    std::vector<SwPoint> out;
    if (!disp.dispatch(prm, A, B, out) || out.size() != N) { ok = false; continue; }
    const std::vector<SwPoint>& expect = (bf == 0.0f) ? A : B;
    auto near = [](float a, float b) { return std::fabs(a - b) < 2e-4f; };
    for (size_t i = 0; i < N; ++i) {
      bool pOk = near(out[i].Position.x, expect[i].Position.x) &&
                 near(out[i].Position.y, expect[i].Position.y) &&
                 near(out[i].Position.z, expect[i].Position.z);
      bool cOk = near(out[i].Color.x, expect[i].Color.x) && near(out[i].Color.w, expect[i].Color.w);
      bool sOk = near(out[i].Scale.x, expect[i].Scale.x);
      bool fx2Ok = near(out[i].FX2, expect[i].FX2);
      bool rSamesign = near(out[i].Rotation.x, expect[i].Rotation.x) &&
                        near(out[i].Rotation.w, expect[i].Rotation.w);
      bool rFlipsign = near(out[i].Rotation.x, -expect[i].Rotation.x) &&
                        near(out[i].Rotation.w, -expect[i].Rotation.w);
      bool rOk = rSamesign || rFlipsign;  // double-cover: q and -q are the same rotation
      bool fx1Ok = near(out[i].FX1, bf);
      ok = ok && pOk && cOk && sOk && fx2Ok && rOk && fx1Ok;
    }
  }
  printf("[mathv-blendpoints-identity-ab] BlendFactor={0,1} boundary + FX1-double-write -> %s\n",
         ok ? "ok" : "RED");
  return ok;
}

}  // namespace

int runMathvBlendPointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-blendpoints] FAIL: no metallib\n");
    return 1;
  }
  BlendPointsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-blendpoints] FAIL: no blendpoints kernel\n");
    return 1;
  }

  bool passPrimary = runPrimary(disp, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPrimary, true, "blendpoints");

  bool passIdentity = runIdentityABTooth(disp);
  bool passBlendMode = mathv_bp_shared::checkBlendModeTooth(disp);
  bool passScatter = mathv_bp_shared::checkScatterTooth(disp);
  bool passNoBlend = mathv_bp_shared::checkNoBlendNanTooth(disp);
  bool passCounts = mathv_bp_shared::checkPairingCountsTooth(disp);
  bool passOffByOne = mathv_bp_shared::checkOffByOneTooth(disp);

  ParityReport rep("selftest-mathv-blendpoints");
  rep.expectTrue("primary(position/rotation/color/scale/fx2/fx1, mix-mode fuzz)", passPrimary,
                 passPrimary ? 1.0 : 0.0);
  rep.expectTrue("identityAB(BlendFactor=0/1 boundary + fx1-double-write)", passIdentity,
                 passIdentity ? 1.0 : 0.0);
  rep.expectTrue("blendMode(enum 0..4 exhaustive, real idx/t)", passBlendMode,
                 passBlendMode ? 1.0 : 0.0);
  rep.expectTrue("scatter(hash11(t) jitter, real idx/t)", passScatter, passScatter ? 1.0 : 0.0);
  rep.expectTrue("noBlendNan(isnan collapse, PINNED)", passNoBlend, passNoBlend ? 1.0 : 0.0);
  rep.expectTrue("pairingCounts(countA!=countB matrix + b-count-guard fork)", passCounts,
                 passCounts ? 1.0 : 0.0);
  rep.expectTrue("offByOne(idx==resultCount boundary probe)", passOffByOne,
                 passOffByOne ? 1.0 : 0.0);
  return rep.finish();
}

// order 1004: appends after mathv-snappointstogrid (1003).
REGISTER_SELFTESTS(/*orderBase=*/1004, {"mathv-blendpoints", runMathvBlendPointsSelfTest});

}  // namespace sw
