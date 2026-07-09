// selftests_mathv_snappointstogrid.cpp — --selftest-mathv-snappointstogrid (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md pilot 工單3 (branchy eps class): fuzz GPU "snaptogrid"
// (app/shaders/snaptogrid.metal) vs the R-authored CPU oracle (mathv_ref_snappointstogrid.h,
// TRANSCRIBED from external/tixl HLSL) via the shared mathv harness (direct-kernel dispatch, §1.3).
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
// ── CONFIRMED UNDOCUMENTED DIVERGENCE found while authoring this TU (NOT a NAMED FORK -- batch-
// tagged per §4.3 rule 4, not silently green'd) ── snaptogrid.metal:107
// `safeGridSize = select(1, gridSize, gridSize!=0)` has NO counterpart in the ref (transcribed
// straight from TiXL HLSL, which also has no guard) nor in the metal file's own NAMED FORKS list.
// Whenever GridScale==0 or any GridStretch axis==0 (in-domain; mathv_input's finiteSpecials()
// ALWAYS injects a literal 0.0 regardless of domain bounds) TiXL/ref divides by zero -> Inf ->
// flooredMod1(Inf)=Inf-floor(Inf)=NaN -> whole position NaN-cascades, while sw substitutes
// gridSize=1 on that axis -> finite. A 100%-reproducible NaN-vs-Finite class mismatch, which
// mathv_compare.h's Comparator::add records as a hard miss BEFORE the branchy exemption is even
// consulted (no exemption possible). Isolated below by runZeroGridSizeDiagnostic() (measured table,
// not a guess) -- this is the expected RED in the main 3-layer fuzz. Routes to S/production-shader-
// fix-ticket per §7 ("手推==ref≠GPU -> 【MSL 錯】"): (a) document as a 5th NAMED FORK + update the
// ref to match, or (b) revert the guard for strict parity. NOT dodged by narrowing the domain --
// GridScale/GridStretch are load-bearing (unlike UseCamera), so excluding 0 would be scope-gaming a
// green, which this work order explicitly forbids ("別弄綠").
//
// ── QUIRK PROBES ── ref's applyGainAndBiasVec4() NOTED-QUIRK: TiXL's SnapPointsToGrid.hlsl:61 calls
// the float4 ApplyGainAndBias overload (bias-functions.hlsl:52-90) whose final statement is the
// unclamped `return v4;` (a hand-slip bug); sw's snaptogrid.metal instead ports the SCALAR
// overload's clamped early-out (`value>0.9999->1; value<0.00001->0`, lines 71-72) -- the apparently-
// INTENDED (bug-free) behavior, not what TiXL's real kernel executes. checkApplyGainAndBiasQuirk()
// reproduces the ref's own self-check case 3 (:344-359, NaN-producing) + 3 variants against the
// GPU, records both sides -- expected to DIVERGE, recorded as evidence not forced to match.
// checkSaturateNanQuirk() probes Metal's actual saturate(NaN) behavior at the mode-0/1 length-ratio
// site (:122/127), independent of the bias-bug path -- ref's AMBIGUITY note (:100-102) asks for
// exactly this real-hardware probe.
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for SwPoint layout + the
// kernel's params ABI header (data layout, not math).
#include "mathv_harness.h"
#include "mathv_ref_snappointstogrid.h"
#include "runtime/selftest_registry.h"
#include "runtime/snaptogrid_params.h"
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
using mathv::MathvCase;
using mathv::ParamDomain;
using mathv::Rng;

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 / WrapDispatch precedent):
// fill a SwPoint bag, setBytes the 64B SnapToGridParams, dispatch "snaptogrid", readback. `P` = the
// 11 fuzz scalars {GridStretchX,Y,Z, Amount, GridOffsetX,Y,Z, GridScale, Mode, GainAndBiasX(gain),
// GainAndBiasY(bias)} in that order (matches paramTable() below).
struct SnapDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;

  SnapDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("snaptogrid", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SnapDispatch() {
    if (pso) pso->release();
  }
  SnapDispatch(const SnapDispatch&) = delete;

  bool dispatch(const std::vector<float>& P, const std::vector<SwPoint>& in,
                std::vector<SwPoint>& out) const {
    if (!ok || P.size() != 11) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    SnapToGridParams params{};
    params.Count = n;
    params.Amount = P[3]; params.GridScale = P[7]; params.Mode = P[8];
    params.GridStretchX = P[0]; params.GridStretchY = P[1]; params.GridStretchZ = P[2];
    params.GridOffsetX = P[4]; params.GridOffsetY = P[5]; params.GridOffsetZ = P[6];
    params.GainAndBiasX = P[9]; params.GainAndBiasY = P[10];
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, SNAPTOGRID_SourcePoints);
    enc->setBuffer(dstBuf, 0, SNAPTOGRID_ResultPoints);
    enc->setBytes(&params, sizeof(params), SNAPTOGRID_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    out.assign(n, SwPoint{});
    std::memcpy(out.data(), dstBuf->contents(), (size_t)n * sizeof(SwPoint));
    srcBuf->release();
    dstBuf->release();
    return true;
  }
};

// CPU ref params matching the kernel's ACTUAL scope (see header SCOPE note): Scatter/StrengthFactor
// pinned to 0 -- the kernel has no slot for either.
mathv_ref::SnapPointsToGridParams refParamsFrom(const std::vector<float>& P) {
  mathv_ref::SnapPointsToGridParams p{};
  p.gridStretchX = P[0]; p.gridStretchY = P[1]; p.gridStretchZ = P[2];
  p.amount = P[3];
  p.gridOffsetX = P[4]; p.gridOffsetY = P[5]; p.gridOffsetZ = P[6];
  p.gridScale = P[7];
  p.scatter = 0.0f;  // NAMED FORK pin (snaptogrid.metal:29 "Scatter baked to 0")
  p.mode = P[8];
  p.gainAndBiasX = P[9]; p.gainAndBiasY = P[10];
  p.strengthFactor = 0;  // NAMED FORK pin (snaptogrid.metal:30 "StrengthFactor=None baked")
  return p;
}

const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"GridStretchX", -3.0f, 5.0f, ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3:45-50 GridStretch default(1,1,1), no Min/Max -> §3.2 default±4"},
      {"GridStretchY", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"GridStretchZ", -3.0f, 5.0f, ParamDomain::Linear, "ditto"},
      {"Amount", 0.0f, 1.0f, ParamDomain::Linear, "tixl SnapPointsToGrid.t3ui:14-21 Min=0 Max=1"},
      {"GridOffsetX", -4.0f, 4.0f, ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3:5-11 Offset default(0,0,0), no Min/Max -> default±4"},
      {"GridOffsetY", -4.0f, 4.0f, ParamDomain::Linear, "ditto"},
      {"GridOffsetZ", -4.0f, 4.0f, ParamDomain::Linear, "ditto"},
      {"GridScale", -3.5f, 4.5f, ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3:21-23 GridScale default 0.5, no Min/Max -> default±4"},
      {"Mode", 0.0f, 3.0f, ParamDomain::Enum,
       "tixl SnapPointsToGrid.cs:44-49 enum SnapModes{Center=0,Corners=1,AxisCenter=2,AxisEdge=3} "
       "-- domain fixed by the enum, .t3ui carries no Min/Max for Mode"},
      {"GainAndBiasX(gain)", 0.0f, 1.0f, ParamDomain::Linear,
       "tixl SnapPointsToGrid.t3ui:86-98 BiasAndGain Min=0 Max=1 ClampMin/Max true; HLSL "
       "GainAndBias.x read as gain (ref AMBIGUITY note)"},
      {"GainAndBiasY(bias)", 0.0f, 1.0f, ParamDomain::Linear, "ditto, .y read as bias"},
  };
  return t;
}

// Branchy-class boundary: distance from this axis's flooredMod1 argument to the nearest integer
// (the mod-wrap discontinuity, SnapPointsToGrid.hlsl:38 / ref detail::flooredMod1). Returns -1 (no
// exemption) when this axis's gridSize is degenerate (~0/non-finite) -- that's the SEPARATE,
// non-exemptable zero-gridSize divergence (file header), not this tooth's legit fp-jitter boundary.
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

// ── DIAGNOSTIC: batch-tag instrumentation isolating the zero-gridSize divergence (§4.3 rule 4:
// miss attribution measured, not guessed). Sweeps the 4 gridSize-forming params (GridStretchX/Y/Z,
// GridScale) through {0 exact, 1e-40 denormal, 1e-6 tiny-normal, 1.0 control} at a radial-broadcast
// Mode (0) and a per-axis Mode (2), tabulating GPU vs ref NaN/Inf/Finite classification. ──────────
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
      const bool matchesHypothesis =
          isExactZero ? (dispatchOk && mismatch > 0 && gN == 0 && rN > 0) : true;
      if (!matchesHypothesis) anyUnexpected = true;
    }
  }
  printf("[mathv-snappointstogrid-diag-zerogrid] hypothesis (exact-zero gridSize-forming axis -> "
         "ref goes NaN-cascade, gpu stays finite via the undocumented safeGridSize guard; "
         "nonzero -- including denormal 1e-40 -- rows are informational, not asserted) %s\n",
         anyUnexpected ? "PARTIALLY UNEXPECTED (see rows above)" : "CONFIRMED across all exact-zero rows");
  return !anyUnexpected;
}

// ── QUIRK PROBE #1: ApplyGainAndBias NaN-bug — reproduce the ref's self-check case 3 (+3 variants)
// against the GPU; record both sides verbatim. Expected to DIVERGE (sw clamps, ref reproduces
// TiXL's `return v4;` bug) -- evidence, not a pass/fail gate (§ quirk probes: 如實記錄). ───────────
bool checkApplyGainAndBiasQuirk(const SnapDispatch& disp) {
  struct Case {
    const char* name;
    float gain, bias, amount;
    int mode;
  };
  const Case cases[] = {
      {"case3(gain0,bias0,amt1)", 0.0f, 0.0f, 1.0f, 1},   // mathv_ref_snappointstogrid.h :344-359
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
           (gpuNan == refNan) ? "MATCH(pin-candidate)" : "DIVERGE(evidence)");
    if (gpuNan != refNan) anyDivergence = true;
  }
  printf("[mathv-snappointstogrid-quirk-gainbias] verdict: %s\n",
         anyDivergence
             ? "DIVERGENCE CONFIRMED -- sw's early-out clamp (snaptogrid.metal:71-72) implements "
               "the scalar overload's INTENDED semantics, not the buggy vec4 `return v4;` path "
               "TiXL's SnapPointsToGrid.hlsl:61 actually calls. Route to S/production-shader ticket, "
               "not a tooth failure."
             : "all matched (re-check: expected the sw clamp to diverge from the reproduced bug)");
  return true;  // informational probe -- always "collects", does not gate pass/fail (§ quirk probes)
}

// ── QUIRK PROBE #2: saturate(NaN) hardware behavior — ref's AMBIGUITY note (:100-102) wants this
// probed on real hardware. A NaN Position component propagates through normalizedPosition ->
// signedFraction -> length -> the mode-0/1 saturate() call (:122/127); reports GPU vs ref per mode,
// independent of probe #1; the hard bar is no-crash. ───────────────────────────────────────────────
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

  bool diagOk = runZeroGridSizeDiagnostic(disp);
  bool quirk1Ok = checkApplyGainAndBiasQuirk(disp);
  bool quirk2Ok = checkSaturateNanQuirk(disp);

  ParityReport rep("selftest-mathv-snappointstogrid");
  rep.expectTrue("position(3-layer fuzz, branchy) -- RED here is the confirmed zero-gridSize "
                 "divergence, see file header + diag table above, not a formula bug",
                 passPos, passPos ? 1.0 : 0.0);
  rep.expectTrue("diag(zero-gridSize batch-tag confirms isolation)", diagOk, diagOk ? 1.0 : 0.0);
  rep.expectTrue("quirk(ApplyGainAndBias NaN-bug probe, informational, always collects)", quirk1Ok,
                 quirk1Ok ? 1.0 : 0.0);
  rep.expectTrue("quirk(saturate-NaN probe, dispatch-no-crash gate)", quirk2Ok,
                 quirk2Ok ? 1.0 : 0.0);
  return rep.finish();
}

// order 1003: appends after mathv-wrappointposition (1001) / mathv-addnoise (1002).
REGISTER_SELFTESTS(/*orderBase=*/1003,
                   {"mathv-snappointstogrid", runMathvSnapPointsToGridSelfTest});

}  // namespace sw
