// selftests_mathv_snappointstogrid_probes.cpp — diagnostic/quirk/isolation probes for the
// SnapPointsToGrid mathv TU, split out of selftests_mathv_snappointstogrid.cpp (Part D, XS verdict
// 2026-07-10 / §4.3 rule 4: the single-TU file hit 399 lines with zero headroom). Called from
// runMathvSnapPointsToGridSelfTest() (the other TU) via the declarations in
// selftests_mathv_snappointstogrid_shared.h; not independently registered/dispatched.
//
// ── QUIRK PROBES ── ref's applyGainAndBiasVec4() NOTED-QUIRK: TiXL's SnapPointsToGrid.hlsl:61 calls
// the float4 ApplyGainAndBias overload (bias-functions.hlsl:52-90) whose final statement is the
// unclamped `return v4;` (a hand-slip bug); sw's snaptogrid.metal instead ports the SCALAR
// overload's clamped early-out (`value>0.9999->1; value<0.00001->0`, lines 71-72) -- the apparently-
// INTENDED (bug-free) behavior, not what TiXL's real kernel executes. checkApplyGainAndBiasQuirk()
// reproduces the ref's own self-check case 3 (mathv_ref_snappointstogrid.h, NaN-producing before the
// small-D hlslSaturate(NaN)->0 fix, finite-matching after it) + 3 variants against the GPU, records
// both sides. checkSaturateNanQuirk() probes Metal's actual saturate(NaN) behavior at the mode-0/1
// length-ratio site, independent of the bias-bug path -- ref's AMBIGUITY note asks for exactly this
// real-hardware probe. checkBiasGainIsolationProbe() (Part D, new) broadens quirk probe #1's 4
// hand-picked cases into a genuine sweep proving the bug does not leak into the ordinary
// non-degenerate-gridSize corpus.
//
// ZONE: shell tier (app/src/ root, mathv support). Same zone as the TU it splits from.
#include "selftests_mathv_snappointstogrid_shared.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace sw {
namespace mathv_snap_shared {

using mathv::Rng;

// ── DIAGNOSTIC: batch-tag instrumentation isolating the zero-gridSize divergence (§4.3 rule 4:
// miss attribution measured, not guessed). Sweeps the 4 gridSize-forming params (GridStretchX/Y/Z,
// GridScale) through {0 exact, 1e-40 denormal, 1e-6 tiny-normal, 1.0 control} at a radial-broadcast
// Mode (0) and a per-axis Mode (2), tabulating GPU vs ref NaN/Inf/Finite classification.
//
// UPDATED (XS verdict A, 2026-07-10): the ref now carries the SAME zero-gridSize guard as the
// metal kernel (mathv_ref_snappointstogrid.h's safeGridSizeX/Y/Z substitution + gridLenFloor). The
// hypothesis below is therefore the RESOLVED-parity one -- exact-zero rows now expect gpu AND ref
// to BOTH be finite (mismatch==0), not the pre-fix NaN-vs-Finite class mismatch this diagnostic
// originally isolated. ──────────────────────────────────────────────────────────────────────────
bool runZeroGridSizeDiagnostic(const SnapDispatch& disp) {
  struct Row {
    const char* param;
    int idx;
    float value;
  };
  const Row rows[] = {
      {"GridStretchX", 0, 0.0f},   {"GridStretchX", 0, 1e-40f}, {"GridStretchX", 0, 1e-6f},
      {"GridStretchX", 0, 1.0f},   {"GridStretchY", 1, 0.0f},   {"GridStretchZ", 2, 0.0f},
      {"GridScale", 7, 0.0f},      {"GridScale", 7, 1e-40f},    {"GridScale", 7, 1e-6f},
      {"GridScale", 7, 1.0f},
  };
  const int modes[] = {0, 2};  // 0=radial-broadcast (all 3 lanes couple), 2=per-axis-independent
  Rng rng(mathv::mathvSeed("snappointstogrid-zerogrid-diag"));
  bool anyUnexpected = false;
  printf("[mathv-snappointstogrid-diag-zerogrid] param=value mode -> "
         "gpu(NaN,Inf,Fin) ref(NaN,Inf,Fin) mismatches/N (N=%d elems x3 lanes)\n", 32);
  for (const Row& row : rows) {
    for (int mode : modes) {
      std::vector<float> P = {1, 1, 1, 0.5f, 0, 0, 0, 1.0f, (float)mode, 0.5f, 0.5f};
      P[(size_t)row.idx] = row.value;
      const int N = 32;
      std::vector<SwPoint> src(N), dst;
      for (int i = 0; i < N; ++i)
        src[i].Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f),
                            rng.uniform(-4.0f, 4.0f)};
      const bool dispatchOk = disp.dispatch(P, src, dst) && dst.size() == (size_t)N;
      mathv_ref::SnapPointsToGridParams prm = refParamsFrom(P);
      int gN = 0, gI = 0, gF = 0, rN = 0, rI = 0, rF = 0, mismatch = 0;
      for (int i = 0; i < N && dispatchOk; ++i) {
        SwPoint refOut{};
        mathv_ref::snapPointsToGridOne(src[i], refOut, 0, prm);
        const float gv[3] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z};
        const float rv[3] = {refOut.Position.x, refOut.Position.y, refOut.Position.z};
        for (int k = 0; k < 3; ++k) {
          const bool gNan = std::isnan(gv[k]), gInf = std::isinf(gv[k]);
          const bool rNan = std::isnan(rv[k]), rInf = std::isinf(rv[k]);
          gN += gNan; gI += gInf; gF += !gNan && !gInf;
          rN += rNan; rI += rInf; rF += !rNan && !rInf;
          if (gNan != rNan || gInf != rInf) ++mismatch;
        }
      }
      printf("[mathv-snappointstogrid-diag-zerogrid] %-13s=%10.3g mode=%d -> "
             "gpu(%2d,%2d,%2d) ref(%2d,%2d,%2d) mismatches=%d/%d%s\n",
             row.param, (double)row.value, mode, gN, gI, gF, rN, rI, rF, mismatch, N * 3,
             dispatchOk ? "" : " [dispatch FAILED]");
      const bool isExactZero = (row.value == 0.0f);
      // RESOLVED (post-A): exact-zero rows must now be a CLEAN MATCH (both finite, mismatch==0) --
      // the ref's safeGridSize guard makes it track the gpu guard instead of diverging from it.
      const bool matchesHypothesis =
          isExactZero ? (dispatchOk && mismatch == 0 && gN == 0 && rN == 0) : true;
      if (!matchesHypothesis) anyUnexpected = true;
    }
  }
  printf("[mathv-snappointstogrid-diag-zerogrid] hypothesis (exact-zero gridSize-forming axis -> "
         "ref AND gpu both guard to a finite result via the matching safeGridSize substitution; "
         "nonzero -- including denormal 1e-40 -- rows are informational, not asserted) %s\n",
         anyUnexpected ? "PARTIALLY UNEXPECTED (see rows above)" : "CONFIRMED across all exact-zero rows");
  return !anyUnexpected;
}

// ── QUIRK PROBE #1: ApplyGainAndBias NaN-bug — reproduce the ref's self-check case 3 (+3 variants)
// against the GPU; record both sides verbatim. Post-small-D-fix (hlslSaturate(NaN)->0), case3's own
// params now MATCH (both sides land on the same finite (0,0,0)); the other 3 variants are recorded,
// not forced -- still informational, not a pass/fail gate (§ quirk probes: 如實記錄). ─────────────
bool checkApplyGainAndBiasQuirk(const SnapDispatch& disp) {
  struct Case {
    const char* name;
    float gain, bias, amount;
    int mode;
  };
  const Case cases[] = {
      {"case3(gain0,bias0,amt1)", 0.0f, 0.0f, 1.0f, 1},   // mathv_ref_snappointstogrid.h self-check
      {"gain0,bias0,amt0.5", 0.0f, 0.0f, 0.5f, 1},
      {"gain0.3,bias0", 0.3f, 0.0f, 1.0f, 1},
      {"gain0.7,bias1", 0.7f, 1.0f, 1.0f, 1},
  };
  bool anyDivergence = false;
  for (const Case& cs : cases) {
    std::vector<float> P = {1, 1, 1, cs.amount, 0, 0, 0, 1.0f, (float)cs.mode, cs.gain, cs.bias};
    SwPoint in{};
    in.Position = {0.0f, 0.0f, 0.0f};  // matches ref self-check case 3's Position exactly
    std::vector<SwPoint> src(1, in), dst;
    const bool dispatchOk = disp.dispatch(P, src, dst) && dst.size() == 1;
    mathv_ref::SnapPointsToGridParams prm = refParamsFrom(P);
    SwPoint refOut{};
    mathv_ref::snapPointsToGridOne(in, refOut, 0, prm);
    const bool gpuNan = dispatchOk && (std::isnan(dst[0].Position.x) ||
                                        std::isnan(dst[0].Position.y) ||
                                        std::isnan(dst[0].Position.z));
    const bool refNan = std::isnan(refOut.Position.x) || std::isnan(refOut.Position.y) ||
                         std::isnan(refOut.Position.z);
    printf("[mathv-snappointstogrid-quirk-gainbias] %-26s dispatchOk=%d "
           "gpu=(%.6g,%.6g,%.6g) ref=(%.6g,%.6g,%.6g) gpuNaN=%d refNaN=%d -> %s\n",
           cs.name, dispatchOk, dispatchOk ? dst[0].Position.x : NAN,
           dispatchOk ? dst[0].Position.y : NAN, dispatchOk ? dst[0].Position.z : NAN,
           refOut.Position.x, refOut.Position.y, refOut.Position.z, gpuNan, refNan,
           (gpuNan == refNan) ? "MATCH" : "DIVERGE(evidence)");
    if (gpuNan != refNan) anyDivergence = true;
  }
  printf("[mathv-snappointstogrid-quirk-gainbias] verdict: %s\n",
         anyDivergence
             ? "DIVERGENCE CONFIRMED -- sw's early-out clamp (snaptogrid.metal:71-72) implements "
               "the scalar overload's INTENDED semantics, not the buggy vec4 `return v4;` path "
               "TiXL's SnapPointsToGrid.hlsl:61 actually calls. Route to S/production-shader ticket, "
               "not a tooth failure."
             : "all matched (post-small-D-fix: hlslSaturate(NaN)->0 makes the ref's manufactured-NaN "
               "outer result coincide with sw's early-out clamp for these params)");
  return true;  // informational probe -- always "collects", does not gate pass/fail (§ quirk probes)
}

// ── QUIRK PROBE #2: saturate(NaN) hardware behavior — ref's AMBIGUITY note wants this probed on
// real hardware. A NaN Position component propagates through normalizedPosition -> signedFraction
// -> length -> the mode-0/1 saturate() call; reports GPU vs ref per mode, independent of probe #1;
// the hard bar is no-crash. ─────────────────────────────────────────────────────────────────────
bool checkSaturateNanQuirk(const SnapDispatch& disp) {
  const int modes[] = {0, 1, 2, 3};
  bool ok = true;
  for (int mode : modes) {
    std::vector<float> P = {1, 1, 1, 0.5f, 0, 0, 0, 1.0f, (float)mode, 0.5f, 0.5f};
    SwPoint in{};
    in.Position = {std::nanf(""), 1.0f, 1.0f};
    std::vector<SwPoint> src(1, in), dst;
    const bool dispatchOk = disp.dispatch(P, src, dst) && dst.size() == 1;
    mathv_ref::SnapPointsToGridParams prm = refParamsFrom(P);
    SwPoint refOut{};
    mathv_ref::snapPointsToGridOne(in, refOut, 0, prm);
    printf("[mathv-snappointstogrid-quirk-saturate-nan] mode=%d dispatchOk=%d "
           "gpu=(%.6g,%.6g,%.6g) ref=(%.6g,%.6g,%.6g)\n",
           mode, dispatchOk, dispatchOk ? dst[0].Position.x : NAN,
           dispatchOk ? dst[0].Position.y : NAN, dispatchOk ? dst[0].Position.z : NAN,
           refOut.Position.x, refOut.Position.y, refOut.Position.z);
    ok = ok && dispatchOk;  // no-crash is the hard bar; classification is evidence only
  }
  return ok;
}

// ── PROBE: bias/gain isolation reverse-check (Part D, XS verdict 2026-07-10) ── broadens quirk
// probe #1's 4 hand-picked cases into a genuine sweep: gridSize pinned NON-degenerate
// (GridStretch=(1,1,1), GridScale=1 -> gridSize=(1,1,1), well away from the zero-gridSize
// divergence isolated by runZeroGridSizeDiagnostic above), gain/bias/amount/mode/position swept
// across 4 independent seeds, tallying:
//   nanMiss — count where isnan(gpu-lane) != isnan(ref-lane) (a NaN-classification mismatch, same
//             metric quirk probe #1 flags by hand for its 4 cases)
//   finMiss — count where BOTH sides are finite but differ beyond a generous absolute tolerance (a
//             genuine numeric divergence, not just a classification difference)
// Half the samples per seed pin Position to the grid-aligned worst case (org==center exactly, the
// ONLY way case3's snapAmount-lands-on-an-exact-0/1-boundary coincidence reproduces) to stress the
// bug's trigger condition as hard as possible; the other half use continuous random positions (the
// ordinary corpus shape). gain/bias are sampled from [0.02,0.98] (not the literal domain endpoints
// 0.0/1.0) -- the actual NaN-producing division (traced by hand: getBias(bias,x) hits 0/0 or Inf*0
// only when bias lands EXACTLY on 0 or 1 while x lands EXACTLY on the matching boundary) needs a
// bit-exact endpoint coincidence that this probe deliberately does not manufacture, since the
// literal endpoints are already covered by the main fuzz's special-value grid layer (a DIFFERENT,
// already-documented divergence class, not this probe's target) -- this probe's target is whether
// the bug leaks into the *interior* sweep the random layer actually exercises.
// ASSERTS nanMiss==finMiss==0 (a genuine reverse-check, not merely informational like quirk probe
// #1) — a nonzero count here would mean the `return v4;` bug DOES leak into ordinary interior
// bias/gain sweeps and the "corpus-unreachable" claim (file header + snaptogrid.metal's NAMED FORK
// comment) is wrong. ─────────────────────────────────────────────────────────────────────────────
bool checkBiasGainIsolationProbe(const SnapDispatch& disp) {
  int nanMiss = 0, finMiss = 0, total = 0;
  const int kSeeds = 4;
  const int kSamplesPerSeed = 256;
  const float kTol = 1e-3f;
  for (int seedIdx = 0; seedIdx < kSeeds; ++seedIdx) {
    char seedName[64];
    std::snprintf(seedName, sizeof seedName, "snappointstogrid-biasgain-probe-%d", seedIdx);
    Rng rng(mathv::mathvSeed(seedName));
    for (int i = 0; i < kSamplesPerSeed; ++i) {
      const float gain = 0.02f + rng.uniform01() * 0.96f;   // (0.02, 0.98) -- see note above
      const float bias = 0.02f + rng.uniform01() * 0.96f;
      const float amount = rng.uniform(0.0f, 1.0f);
      const int mode = (int)rng.uniform(0.0f, 3.999f);
      const bool gridAligned = (i % 2) == 0;  // worst-case org==center vs ordinary random position
      SwPoint in{};
      if (gridAligned) {
        in.Position = {0.0f, 0.0f, 0.0f};  // grid=1 -> sf==(0,0,0) exactly, matches case3's setup
      } else {
        in.Position = {rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f), rng.uniform(-4.0f, 4.0f)};
      }
      std::vector<float> P = {1, 1, 1, amount, 0, 0, 0, 1.0f, (float)mode, gain, bias};
      std::vector<SwPoint> src(1, in), dst;
      const bool dispatchOk = disp.dispatch(P, src, dst) && dst.size() == 1;
      if (!dispatchOk) continue;
      mathv_ref::SnapPointsToGridParams prm = refParamsFrom(P);
      SwPoint refOut{};
      mathv_ref::snapPointsToGridOne(in, refOut, 0, prm);
      const float gv[3] = {dst[0].Position.x, dst[0].Position.y, dst[0].Position.z};
      const float rv[3] = {refOut.Position.x, refOut.Position.y, refOut.Position.z};
      for (int k = 0; k < 3; ++k) {
        ++total;
        const bool gNan = std::isnan(gv[k]), rNan = std::isnan(rv[k]);
        if (gNan != rNan) {
          ++nanMiss;
          continue;
        }
        if (!gNan && !rNan && std::fabs(gv[k] - rv[k]) > kTol) ++finMiss;
      }
    }
  }
  printf("[mathv-snappointstogrid-probe-biasgain-isolation] seeds=%d samplesPerSeed=%d "
         "total-lanes=%d nanMiss=%d finMiss=%d -> %s\n",
         kSeeds, kSamplesPerSeed, total, nanMiss, finMiss,
         (nanMiss == 0 && finMiss == 0)
             ? "CONFIRMED (bias/gain-driven ApplyGainAndBias fork does not leak into the "
               "non-degenerate-gridSize interior sweep)"
             : "DIVERGENCE FOUND -- the return-v4 bug is corpus-reachable, escalate to S/production");
  return nanMiss == 0 && finMiss == 0;
}

}  // namespace mathv_snap_shared
}  // namespace sw
