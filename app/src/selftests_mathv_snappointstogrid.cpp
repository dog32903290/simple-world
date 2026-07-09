// selftests_mathv_snappointstogrid.cpp — --selftest-mathv-snappointstogrid (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md pilot 工單3 (branchy eps class): fuzz GPU "snaptogrid"
// (app/shaders/snaptogrid.metal) vs the R-authored CPU oracle (mathv_ref_snappointstogrid.h,
// TRANSCRIBED from external/tixl HLSL) via the shared mathv harness (direct-kernel dispatch, §1.3).
// Dispatch adapter / params bridge shared with selftests_mathv_snappointstogrid_probes.cpp via
// selftests_mathv_snappointstogrid_shared.h (Part D split, XS verdict 2026-07-10 — this file alone
// was 399 lines with zero headroom before this work order's additions).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ── SnapPointsToGrid.t3:4-58 DefaultValue +
// .t3ui Min/Max where authored: GridStretch default(1,1,1) no Min/Max -> §3.2 default±4=[-3,5]/axis;
// Amount .t3ui:14-21 Min0/Max1; GridOffset default(0,0,0) no Min/Max -> default±4=[-4,4]/axis;
// GridScale default 0.5 no Min/Max -> default±4=[-3.5,4.5]; Mode SnapPointsToGrid.cs:44-49 enum
// {0=CenterDistance,1=CornersDistance,2=AxisCenterDistance,3=AxisEdgeDistance} (domain=enum, no
// .t3ui Min/Max); BiasAndGain .t3ui:86-98 Min0/Max1 ClampMin/Max true both lanes (HLSL field
// GainAndBias reads .x=gain .y=bias -- AMBIGUITY per the ref header, this TU follows HLSL).
//
// ── SCOPE: Scatter/StrengthFactor pinned to 0, NOT swept (WrapPointPosition UseCamera precedent) ──
// snaptogrid.metal:28-31 NAMED FORKS already document "Scatter baked to 0" (no hash41u call at all)
// and "StrengthFactor=None baked" (no FX1/FX2 read); snaptogrid_params.h's SnapToGridParams struct
// has no slot for either -- sweeping would only measure "not implemented yet", not kernel math.
// refParamsFrom() pins scatter=0/strengthFactor=0 to match; FX1/FX2 irrelevant when
// strengthFactor==0 (weight=1 unconditionally, ref :288-293).
//
// ── RESOLVED (XS verdict A, 2026-07-10): the zero-gridSize divergence this TU originally isolated
// is now CLOSED, INCLUDING the denormal-boundary case the original isolation missed. A bit-exact
// `gridSize==0` guard alone was NOT enough: at the literal 1e-40 special (finiteSpecials() injects
// it for every param) Apple's GPU ALU flushes the subnormal *result* of GridScale*GridStretch to
// zero (FTZ) before its own `!=0` guard ever runs, while a bit-exact-only CPU ref still divides by
// the true nonzero 1e-40 and NaN-cascades. mathv_ref_snappointstogrid.h's guard is therefore
// FTZ-equivalent (`fabs(gridSize) < FLT_MIN`, catching {0} ∪ every subnormal), matching
// snaptogrid.metal:107/:121-122/:126-127's safeGridSize+gridLenFloor. runZeroGridSizeDiagnostic()
// (probes TU) now shows 0 mismatches across every exact-zero AND denormal row.
//
// ── RESIDUAL TAIL, RESOLVED (orchestrator verdict 2026-07-10, following pilot #2 precedent):
// after the zero/denormal guard closed, the main fuzz still showed 506/201984 (0.25%) hard misses —
// every one FINITE-vs-FINITE with small relative error (~1e-5 to 4e-4), at branchDist far from any
// mod-wrap boundary (0.12-0.46 vs deltaBranch=1e-4). Root cause: finiteSpecials()'s blanket ±1e6
// stress value (injected for EVERY param regardless of this op's authored domain) drives
// |gridSize| = |GridScale·GridStretch| (:32) to ~4e4-5.4e5, and SnapPointsToGrid's output chain
// multiplies the mod-lookup result by gridSize (:39 centerPoint = pos - sf·gridSize/2, :69 lerp),
// so CPU/GPU ULP-level scheduling/FMA drift in sf (:36-:38) is amplified by |gridSize|/2 into an
// absolute error that exceeds the Branchy tight gate (atol=1e-6, rtol=1e-5) while remaining pure
// fp-physics — not a formula bug on either side (hand-derivation matches both). RESOLUTION: the
// ill-conditioned-lookup exemption (pilot #2's S-verdict mechanism) is now EXTENDED to the Branchy
// class (mathv_compare.h Branchy arm) and this TU opts in via c.illConditionedEnvelope below —
// candidacy = the sample's intrinsic fp-error floor (FLT_EPSILON · |noff| · |gridSize|, from the
// :36-:39 amplification chain) exceeds the class gate; envelope = |out| ≤ |org| + |gridSize|/2
// (from :38 |sf|<1, :68 ff∈[0,1] since Amount is .t3ui-clamped [0,1] and StrengthFactor=None).
// The 1% exemptMax cap still gates globally (0.25% observed, 4× headroom) and the -bug leg still
// bites (its ~37% miss rate blows the cap regardless of any per-sample exemption).
//
// ── QUIRK PROBES (selftests_mathv_snappointstogrid_probes.cpp) ── ref's applyGainAndBiasVec4()
// NOTED-QUIRK: TiXL's SnapPointsToGrid.hlsl:61 calls the float4 ApplyGainAndBias overload
// (bias-functions.hlsl:52-90) whose final statement is the unclamped `return v4;` (a hand-slip
// bug); sw's snaptogrid.metal instead ports the SCALAR overload's clamped early-out -- the
// apparently-INTENDED (bug-free) behavior, not what TiXL's real kernel executes.
// checkApplyGainAndBiasQuirk() reproduces the ref's own self-check case 3 + 3 variants against the
// GPU; checkSaturateNanQuirk() probes Metal's actual saturate(NaN) hardware behavior (which fed
// back into the small-D hlslSaturate(NaN)->0 fix in the ref); checkBiasGainIsolationProbe() (Part D)
// sweeps gain/bias/mode/position across 4 seeds asserting the bug does not leak into the ordinary
// interior corpus (nanMiss==finMiss==0).
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for SwPoint layout + the
// kernel's params ABI header (data layout, not math).
#include "selftests_mathv_snappointstogrid_shared.h"

#include "runtime/selftest_registry.h"

#include <cfloat>  // FLT_MIN / FLT_EPSILON -- illConditionedEnvelope's guard + intrinsic-error floor
#include <cmath>
#include <cstdio>
#include <vector>

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

using mathv::EpsSpec;
using mathv::MathvCase;
using mathv_snap_shared::paramTable;
using mathv_snap_shared::refParamsFrom;
using mathv_snap_shared::SnapDispatch;

// Branchy-class boundary: distance from this axis's flooredMod1 argument to the nearest integer
// (the mod-wrap discontinuity, SnapPointsToGrid.hlsl:38 / ref detail::flooredMod1). Returns -1 (no
// exemption) when this axis's gridSize is degenerate (~0/non-finite) -- that's the SEPARATE,
// now-guarded zero-gridSize case (see file header), not this tooth's legit fp-jitter boundary.
float modBranchDist(const std::vector<float>& P, const float* in, int lane) {
  if (lane < 0 || lane > 2) return -1.0f;
  const float stretch = P[(size_t)lane];        // GridStretchX/Y/Z at indices 0,1,2
  const float scale = P[7];                     // GridScale
  const float offset = P[4 + (size_t)lane];     // GridOffsetX/Y/Z at indices 4,5,6
  const float gridSize = scale * stretch;
  if (!std::isfinite(gridSize) || std::fabs(gridSize) < 1e-6f) return -1.0f;
  const float noff = in[lane] / gridSize + 0.5f - offset;
  if (!std::isfinite(noff)) return -1.0f;
  const float frac = noff - std::floor(noff);
  return std::min(frac, 1.0f - frac);
}

}  // namespace

int runMathvSnapPointsToGridSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-snappointstogrid] FAIL: no metallib\n");
    return 1;
  }
  SnapDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-snappointstogrid] FAIL: no snaptogrid kernel\n");
    return 1;
  }

  MathvCase c;
  c.opName = "snappointstogrid";
  c.params = paramTable();
  c.inDim = c.outDim = 3;  // Position only
  c.eps = EpsSpec::branchy();  // rounding/select-dense op, §6 pilot 3 assignment
  c.inputLo = -4.0f;
  c.inputHi = 4.0f;
  // identity sentinel: Amount=0 -> strength=Amount*1=0 -> ff=0 exactly, PROVIDED biasedSnap stays
  // finite (picks the safe gain=bias=0.5 identity per mathv_ref_snappointstogrid.h self-check case
  // 1's own comment: "GetBias/GetSchlickBias both reduce to IDENTITY at 0.5") so 0*finite==0, not
  // the 0*NaN==NaN corner the quirk probes above exercise deliberately.
  c.identityParams = {{1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f}};
  c.branchDist = modBranchDist;
  c.minLivenessFrac = 0.50f;
  // measured: ff collapses to 0 for ~half of Amount∈[0,1] below snap-engagement knee;
  // nonIdentityFrac 0.626/0.672/0.689/0.889 across 4 seeds; floor 0.50 keeps >0.12 margin, still
  // catches ≥25% movement-loss regression.
  //
  // ill-conditioned-lookup exemption (Branchy extension, orchestrator verdict 2026-07-10 — see
  // file-header RESIDUAL TAIL note + mathv_compare.h Branchy arm; AddNoise's envelope pattern).
  // CANDIDACY (criterion 1's fine judgment; the comparator's own branchDist>=0 pre-gate rides
  // modBranchDist above): the sample's intrinsic fp-error floor exceeds the class gate. From the
  // HLSL chain — :36 norm = pos/gridSize, :37 noff = norm+0.5-Offset, :38 sf = (mod(noff,1)-.5)*2,
  // :39 center = pos - sf*gridSize/2, :63-:68 strength = Amount (StrengthFactor=None), ff =
  // (1-saturate(...))*strength so |ff| <= |Amount|, :69 out = org + ff*(center-org) — a 1-ULP
  // drift in noff (CPU vs GPU FMA/scheduling) moves sf by ~2*eps*|noff|, and the biased/saturate
  // chain contributes an eps-scale absolute drift in the (1-saturate(...)) factor; both are then
  // AMPLIFIED by |Amount|*|gridSize|/2 through :39/:69's multiplies. intrinsic =
  // 8*eps*max(|Amount|,1)*max(|gridSize|,1)*(|noff|+4). measured across the 506-miss corpus's two
  // amplifier shapes: (a) GridScale/GridStretch=±1e6 rows (|gridSize|~4e4-5.4e5, Amount mid 0.37,
  // |noff|~1.5): intrinsic 0.21-2.8 vs observed absErr 0.03-0.30 (headroom ~2-9x, accumulation
  // seen ~2-4x); (b) Amount=±1e6 rows (|gridSize|~0.0216, |noff|~46): intrinsic ~48 vs observed
  // 0.03-0.30 (looser there — the noff/saturate error terms are hard to split; the envelope+1% cap
  // below carry the containment). Ordinary random-layer samples (|Amount|<=1, |gridSize|<=22.5)
  // land at/below ~2e-4 vs gates >=1e-6+1e-5|out| — overwhelmingly non-candidates, and the -bug
  // leg's ~37% miss rate blows the 1% cap regardless, so the bite is preserved. measured (final,
  // seed 0x6a506f487de3fc82): no-bug leg miss=0 exempt=0.00252 (4x under cap) -> PASS; -bug leg
  // miss=74640/201984 -> RED (bite), exit polarity 0/1 verified.
  // ENVELOPE (criterion 3): |sf|<1 (:38 mod range) + |ff|<=|Amount| (:68) bound the output:
  // |out| <= |org| + |Amount|*|gridSize|/2, with 1e-3 relative + 1.0 absolute slack.
  c.illConditionedEnvelope = [](const std::vector<float>& P, const float* in, int lane, float gpu,
                                float ref) {
    float gs = P[7] * P[(size_t)lane];              // :32 gridSize for this lane
    if (std::fabs(gs) < FLT_MIN) gs = 1.0f;          // same FTZ-equivalent guard both sides apply
    const float ags = std::fabs(gs);
    const float aAmt = std::fmax(std::fabs(P[3]), 1.0f);  // :63-:68 |ff| <= |Amount| amplifier
    const float noff = in[lane] / gs + 0.5f - P[4 + (size_t)lane];  // :36-:37 lookup coordinate
    if (!std::isfinite(noff)) return false;
    const float intrinsic =
        8.0f * FLT_EPSILON * aAmt * std::fmax(ags, 1.0f) * (std::fabs(noff) + 4.0f);
    const float gate = 1e-6f + 1e-5f * std::fmax(std::fabs(gpu), std::fabs(ref));
    if (intrinsic <= gate) return false;  // well-conditioned here: a miss stays a miss
    const float bound = (std::fabs(in[lane]) + aAmt * ags * 0.5f) * 1.001f + 1.0f;
    return std::fabs(gpu) <= bound && std::fabs(ref) <= bound;
  };
  c.gpu = [&disp](const std::vector<float>& P, const std::vector<float>& in,
                  std::vector<float>& out) {
    std::vector<SwPoint> src(in.size() / 3), dst;
    for (size_t i = 0; i < src.size(); ++i)
      src[i].Position = {in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2]};
    if (!disp.dispatch(P, src, dst) || dst.size() != src.size()) return false;
    out.resize(dst.size() * 3);
    for (size_t i = 0; i < dst.size(); ++i) {
      out[i * 3 + 0] = dst[i].Position.x;
      out[i * 3 + 1] = dst[i].Position.y;
      out[i * 3 + 2] = dst[i].Position.z;
    }
    return true;
  };
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    mathv_ref::SnapPointsToGridParams prm = refParamsFrom(P);
    SwPoint pin{}, pout{};
    pin.Position = {in[0], in[1], in[2]};
    mathv_ref::snapPointsToGridOne(pin, pout, 0, prm);
    out[0] = pout.Position.x;
    out[1] = pout.Position.y;
    out[2] = pout.Position.z;
  };

  bool passPos = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passPos, true, "snappointstogrid");

  bool diagOk = mathv_snap_shared::runZeroGridSizeDiagnostic(disp);
  bool quirk1Ok = mathv_snap_shared::checkApplyGainAndBiasQuirk(disp);
  bool quirk2Ok = mathv_snap_shared::checkSaturateNanQuirk(disp);
  bool isoOk = mathv_snap_shared::checkBiasGainIsolationProbe(disp);

  ParityReport rep("selftest-mathv-snappointstogrid");
  rep.expectTrue("position(3-layer fuzz, branchy + ill-conditioned envelope) -- zero/denormal "
                 "guard CLOSED (XS verdict A) + huge-magnitude tail exempted under the 1% cap "
                 "(orchestrator verdict); RED here is a real formula bug",
                 passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("diag(zero-gridSize batch-tag confirms resolved parity)", diagOk,
                 diagOk ? 1.0 : 0.0);
  rep.expectTrue("quirk(ApplyGainAndBias NaN-bug probe, informational, always collects)", quirk1Ok,
                 quirk1Ok ? 1.0 : 0.0);
  rep.expectTrue("quirk(saturate-NaN probe, dispatch-no-crash gate)", quirk2Ok,
                 quirk2Ok ? 1.0 : 0.0);
  rep.expectTrue("probe(bias/gain isolation, nanMiss=finMiss=0 gate)", isoOk, isoOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1003: appends after mathv-wrappointposition (1001) / mathv-addnoise (1002).
REGISTER_SELFTESTS(/*orderBase=*/1003,
                   {"mathv-snappointstogrid", runMathvSnapPointsToGridSelfTest});

}  // namespace sw
