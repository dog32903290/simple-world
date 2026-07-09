// selftests_mathv_clearsomepoints.cpp — --selftest-mathv-clearsomepoints (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 量產鏈 Tier-L: fuzz GPU "clearsomepoints" (app/shaders/clearsomepoints.metal)
// vs the R-authored CPU oracle (mathv_ref_clearsomepoints.h, TRANSCRIBED from external/tixl HLSL)
// via the shared mathv harness (direct-kernel dispatch, §1.3 — NOT buildEvalGraph).
//
// ParamDomain provenance (SHA 395c4c55): ClearSomePoints.t3:4-19 DEFAULTS (Ratio=0.0, Resolution=0,
// Repeat=0, Seed=0); .t3ui:13-32 authors Min/ClampMin/ClampMax for Resolution(Min=1)/Repeat(Min=0)
// only — Ratio/Seed use the §3.2 DefaultValue±4 fallback; Resolution/Repeat honor their authored
// floor (paramTable() cites exact lines). Resolution<=0 / Repeat<0 sit OUTSIDE the swept domain on
// purpose: QUIRK PROBES below, not main-domain fuzz.
//
// IDX BRIDGE (novel here — first idx-DEPENDENT kernel in this batch): unlike WrapPointPosition/
// AddNoise/SnapPointsToGrid (idx-independent, ref hardcodes idx=0), this hash pipeline is keyed on
// `i.x`, but MathvCase::ref's signature carries no idx. Bridge: mathv_harness.h's runCompare calls
// `c.gpu(...)` ONCE per batch (one real n-point buffer, thread i computes at i.x==i) THEN calls
// `c.ref(...)` n times ascending — `refIdxCursor` (captured by reference below) resets to 0 inside
// `c.gpu` (the only visible batch boundary) and increments per `c.ref` call, reproducing the SAME
// idx sequence the dispatch used — depends only on that documented calling order.
//
// QUIRK PROBES (5, from mathv_ref_clearsomepoints.h): 1. uint promotion wraparound (ref :101-113)
// -> checkSeedWraparoundTooth (pinned GREEN). 2. Resolution==0 div-by-zero AMBIGUITY (ref :74-88)
// -> probeResolutionNonPositive (non-gating: clearsomepoints.metal:14-20 already documents cwMod's
// `repeat<=0->0` as a NAMED FORK vs the ref's HLSL-pinned -1 — recorded as evidence per §7, not
// gated). 3. Repeat<0 huge modulus (ref :121-125) -> checkRepeatNegativeTooth (pinned GREEN).
// 4. Repeat==1 collapse (ref :198-203) -> checkRepeatOneCollapseTooth (pinned GREEN). 5. no
// idx>=pointCount guard in the HLSL (ref :135-137, dead locals) — sw DOES guard via P.Count
// (clearsomepoints.metal:61); nothing to probe, noted only.
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for SwPoint + params ABI.
#include "mathv_harness.h"
#include "mathv_ref_clearsomepoints.h"
#include "runtime/clearsomepoints_params.h"
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
using mathv::MathvCase;
using mathv::ParamDomain;
using mathv::Rng;

// nonfinite-smoke (mathv_harness.h §1) feeds NaN/Inf into every param slot incl. Seed/Repeat/
// Resolution — `(int32_t)NaN` is UB (UBSan-caught); land on a defined, classifiable int32 instead
// (smoke only asserts no-crash, so the exact fallback value doesn't matter).
int32_t safeParamInt(float v) { return std::isfinite(v) ? (int32_t)v : 0; }

// direct-kernel dispatch adapter (turbulence_parity_golden.cpp:75-112 shape). `P` = the 4 fuzz
// scalars {Ratio, Seed, Repeat, Resolution} in that order (matches paramTable() below).
struct ClearDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;

  ClearDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn =
        lib->newFunction(NS::String::string("clearsomepoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~ClearDispatch() { if (pso) pso->release(); }
  ClearDispatch(const ClearDispatch&) = delete;

  // Real sequential dispatch: buffer slot i is computed by GPU thread i.x==i (production shape),
  // so idx==i for every point in `in` — the property the IDX BRIDGE (file header) depends on.
  bool dispatch(const std::vector<float>& P, const std::vector<SwPoint>& in,
                std::vector<SwPoint>& out) const {
    if (!ok || P.size() != 4) return false;
    const uint32_t n = (uint32_t)in.size();
    out.clear();
    if (n == 0) return true;
    MTL::Buffer* srcBuf = dev->newBuffer(in.data(), (NS::UInteger)(n * sizeof(SwPoint)),
                                        MTL::ResourceStorageModeShared);
    MTL::Buffer* dstBuf =
        dev->newBuffer((NS::UInteger)(n * sizeof(SwPoint)), MTL::ResourceStorageModeShared);
    ClearSomePointsParams params{};
    params.Count = n;
    params.Ratio = P[0];
    params.Seed = safeParamInt(P[1]);
    params.Repeat = safeParamInt(P[2]);
    params.Resolution = safeParamInt(P[3]);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(srcBuf, 0, CLEARSOMEPOINTS_SourcePoints);
    enc->setBuffer(dstBuf, 0, CLEARSOMEPOINTS_ResultPoints);
    enc->setBytes(&params, sizeof(params), CLEARSOMEPOINTS_Params);
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

mathv_ref::ClearSomePointsParams refParamsFrom(const std::vector<float>& P) {
  mathv_ref::ClearSomePointsParams p{};
  p.ratio = P[0];
  p.seed = safeParamInt(P[1]);
  p.repeat = safeParamInt(P[2]);
  p.resolution = safeParamInt(P[3]);
  return p;
}

const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      // measured (batch-tag): the mechanical §3.2 ±4 fallback ([-4,4]) made -bug (B[0]+=1e-2) fail
      // to trip (miss=0) — Ratio only feeds a `hash<=Ratio` THRESHOLD (ref :162-171), so a 0.01
      // shift only flips draws inside a 0.01-wide band, and width-8 puts most draws outside
      // hash11u's proven [0,1] range (ref :150-154) where the shift never matters. Narrowed to
      // [-1,2] (cites that same proven bound) so >=1/3 of draws land engaged — confirmed by rerun.
      {"Ratio", -1.0f, 2.0f, ParamDomain::Linear,
       "external/tixl ClearSomePoints.t3:4-7 Ratio DefaultValue 0.0, no Min/Max in "
       "ClearSomePoints.t3ui; narrowed from the §3.2 DefaultValue±4 fallback to cite "
       "mathv_ref_clearsomepoints.h's own proven hash11u∈[0,1] bound (measured -bug reliability)"},
      {"Seed", -4.0f, 4.0f, ParamDomain::Int,
       "external/tixl ClearSomePoints.t3:16-19 Seed DefaultValue 0, no Min/Max authored -> §3.2 "
       "DefaultValue±4 fallback"},
      {"Repeat", 0.0f, 32.0f, ParamDomain::Int,
       "external/tixl ClearSomePoints.t3ui:23-32 Repeat Min=0 ClampMin=true (no Max authored) -> "
       "domain honors the authored floor with a finite window; Repeat<0 probed separately (quirk)"},
      {"Resolution", 1.0f, 32.0f, ParamDomain::Int,
       "external/tixl ClearSomePoints.t3ui:13-22 Resolution Min=1 ClampMin=true (no Max authored) "
       "-> domain honors the authored floor; Resolution<=0 probed separately (AMBIGUITY quirk)"},
  };
  return t;
}

// Single-point parity check pinned at buffer position `idxv` (buffer padded to idxv+1 slots so GPU
// thread i.x==idxv really executes — distinct from the generic layer's reset-cursor IDX BRIDGE
// above, which only needs idx to start at 0 per call).
void oneAt(const ClearDispatch& disp, Comparator& cmp, float ratio, int32_t seed, int32_t repeat,
           int32_t resolution, uint32_t idxv, const char* batch) {
  std::vector<float> P = {ratio, (float)seed, (float)repeat, (float)resolution};
  std::vector<SwPoint> buf((size_t)idxv + 1);
  buf[(size_t)idxv].Scale = {1.0f, 2.0f, 3.0f};
  std::vector<SwPoint> dst;
  if (!disp.dispatch(P, buf, dst) || dst.size() != buf.size()) return;
  mathv_ref::ClearSomePointsParams prm = refParamsFrom(P);
  SwPoint refIn{}; refIn.Scale = {1.0f, 2.0f, 3.0f};
  SwPoint refOut{};
  mathv_ref::clearSomePointsOne(refIn, refOut, idxv, prm);
  const SwPoint& g = dst[(size_t)idxv];
  float in3[3] = {1.0f, 2.0f, 3.0f};
  cmp.add(g.Scale.x, refOut.Scale.x, in3, 3, 0, -1.0f, batch);
  cmp.add(g.Scale.y, refOut.Scale.y, in3, 3, 1, -1.0f, batch);
  cmp.add(g.Scale.z, refOut.Scale.z, in3, 3, 2, -1.0f, batch);
}

// TOOTH PASSTHROUGH: non-Scale channels (Position/FX1/Rotation/Color/FX2) are untouched REGARDLESS
// of the clear decision (:44 ResultPoints[i.x]=p carries the whole point; only Scale is ever
// conditionally overwritten, ref :140/:170). Exact eps — plain copy-through.
bool checkPassthroughTooth(const ClearDispatch& disp) {
  Comparator cmp("mathv-clearsomepoints-passthrough", EpsSpec::exact(), 5);
  Rng rng(mathv::mathvSeed("clearsomepoints-passthrough"));
  const auto& dom = paramTable();
  const size_t N = 512;
  for (int v = 0; v < 6; ++v) {
    std::vector<float> P(4);
    for (int k = 0; k < 4; ++k) P[k] = mathv::sampleUniform(rng, dom[k]);
    std::vector<SwPoint> src(N), dst;
    for (size_t i = 0; i < N; ++i) {
      SwPoint& p = src[i];
      p.Position = {rng.uniform(-4, 4), rng.uniform(-4, 4), rng.uniform(-4, 4)};
      p.FX1 = rng.uniform(-4, 4);
      p.Rotation = {rng.uniform(-4, 4), rng.uniform(-4, 4), rng.uniform(-4, 4), rng.uniform(-4, 4)};
      p.Color = {rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0, 1)};
      p.Scale = {rng.uniform(-4, 4), rng.uniform(-4, 4), rng.uniform(-4, 4)};
      p.FX2 = rng.uniform(-4, 4);
    }
    if (!disp.dispatch(P, src, dst) || dst.size() != N) continue;
    for (size_t i = 0; i < N; ++i) {
      float in13[13] = {src[i].Position.x, src[i].Position.y, src[i].Position.z, src[i].FX1,
                        src[i].Rotation.x, src[i].Rotation.y, src[i].Rotation.z, src[i].Rotation.w,
                        src[i].Color.x, src[i].Color.y, src[i].Color.z, src[i].Color.w, src[i].FX2};
      float outv[13] = {dst[i].Position.x, dst[i].Position.y, dst[i].Position.z, dst[i].FX1,
                        dst[i].Rotation.x, dst[i].Rotation.y, dst[i].Rotation.z, dst[i].Rotation.w,
                        dst[i].Color.x, dst[i].Color.y, dst[i].Color.z, dst[i].Color.w, dst[i].FX2};
      for (int k = 0; k < 13; ++k) cmp.add(outv[k], in13[k], in13, 13, k, -1.0f, "passthrough");
    }
  }
  cmp.print();
  return cmp.verdict();
}

// quirk 4 — Repeat==1 collapse (ref :198-203): pointU==0 always -> Ratio=0.0 clears. GREEN expected.
bool checkRepeatOneCollapseTooth(const ClearDispatch& disp) {
  Comparator cmp("mathv-clearsomepoints-repeat1-collapse", EpsSpec::exact(), 5);
  const int idxes[] = {0, 1, 7, 100};
  const int32_t seeds[] = {-5, 0, 5, 123};
  const int32_t resolutions[] = {1, 3, 9, 64};
  const float ratios[] = {0.0f, -0.5f, 2.0f};  // 0.0 clears exactly, -0.5 never, 2.0 always
  for (int idxv : idxes)
    for (int32_t seed : seeds)
      for (int32_t res : resolutions)
        for (float ratio : ratios) oneAt(disp, cmp, ratio, seed, 1, res, (uint32_t)idxv, "repeat1-collapse");
  cmp.print();
  return cmp.verdict();
}

// quirk 3 — Repeat<0 huge modulus (ref :121-125): a negative Repeat bit-reinterprets to a huge
// uint32 identically on ref and shader (clearsomepoints.metal:68). Symmetric port, not an
// ambiguity — PINNED PARITY: GREEN expected.
bool checkRepeatNegativeTooth(const ClearDispatch& disp) {
  Comparator cmp("mathv-clearsomepoints-repeat-negative", EpsSpec::exact(), 5);
  // -2147483648==-2^31 round-trips EXACTLY through float (1-bit mantissa) — see
  // checkSeedWraparoundTooth's note on the shared float-P-vector precision ceiling.
  const int32_t repeats[] = {-1, -5, -1000, -2147483648};
  const int idxes[] = {0, 5, 100};
  const int32_t seeds[] = {0, -3, 7};
  const int32_t resolutions[] = {1, 4, 17};
  for (int32_t rep : repeats)
    for (int idxv : idxes)
      for (int32_t seed : seeds)
        for (int32_t res : resolutions) oneAt(disp, cmp, 0.5f, seed, rep, res, (uint32_t)idxv, "repeat-negative");
  cmp.print();
  return cmp.verdict();
}

// quirk 1 — uint promotion wraparound (ref :101-113): Seed(int32) bit-reinterprets to uint32.
// Values stay exactly float-representable (+/-2^24): the shared P-vector round-trips every param
// through `float`, and INT32_MIN/MAX would silently corrupt through that instead of exercising the
// kernel — deliberately avoided. PINNED PARITY: GREEN expected.
bool checkSeedWraparoundTooth(const ClearDispatch& disp) {
  Comparator cmp("mathv-clearsomepoints-seed-wraparound", EpsSpec::exact(), 5);
  const int32_t seeds[] = {-1, 1, -2, 2, -16777216, 16777215, -8388608, 8388607};
  const int idxes[] = {0, 5, 100};
  const int32_t resolutions[] = {1, 4, 17};
  const int32_t repeats[] = {0, 5};
  for (int32_t seed : seeds)
    for (int idxv : idxes)
      for (int32_t res : resolutions)
        for (int32_t rep : repeats) oneAt(disp, cmp, 0.5f, seed, rep, res, (uint32_t)idxv, "seed-wraparound");
  cmp.print();
  return cmp.verdict();
}

// DEDICATED LIVENESS (replaces the generic nonIdentityFrac — see c.minLivenessFrac comment below):
// Liveness::add() only counts a lane non-identity when BOTH in/out are finite; a cleared point
// (Scale->NaN, all 3 lanes) is finite on neither side, so it's silently SKIPPED — structurally
// blind to whether clearing happens at all. REAL anti-hollow check: Ratio=0.5/Resolution=1 gives
// each point an independent-ish hash draw, ~50% should clear; assert a loose 10-90% band (same
// band point_ops_clearsomepoints.cpp's Tooth 4 already uses).
bool checkClearingRateLiveness(const ClearDispatch& disp) {
  const uint32_t N = 4096;
  std::vector<float> P = {0.5f, 7.0f, 0.0f, 1.0f};
  std::vector<SwPoint> src(N), dst;
  if (!disp.dispatch(P, src, dst) || dst.size() != N) {
    printf("[mathv-clearsomepoints-clearing-rate] FAIL: dispatch failed\n"); return false;
  }
  uint32_t cleared = 0;
  for (const SwPoint& p : dst)
    if (std::isnan(p.Scale.x)) ++cleared;
  const double frac = (double)cleared / (double)N;
  const bool ok = frac >= 0.10 && frac <= 0.90;
  printf("[mathv-clearsomepoints-clearing-rate] Ratio=0.5 Resolution=1 N=%u cleared=%u frac=%.4f "
         "(need 0.10-0.90) -> %s\n", N, cleared, frac, ok ? "ok" : "RED");
  return ok;
}

// quirk 2 — Resolution<=0 (ref :74-88 AMBIGUITY; clearsomepoints.metal:14-20 NAMED FORK): HLSL's
// div-by-zero is D3D-DEFINED (ref pins Mod(val,0)->-1); the shipped MSL short-circuits to modVal=0
// instead (already-cited fork). NON-GATING: record both sides as an AMBIGUITY evidence pack (§7).
double probeResolutionNonPositive(const ClearDispatch& disp) {
  const int32_t resolutions[] = {0, -1, -5, -1000};
  const int idxes[] = {0, 1, 7, 100};
  int total = 0, matched = 0;
  for (int32_t res : resolutions) {
    for (int idxv : idxes) {
      std::vector<float> P = {0.5f, 3.0f, 17.0f, (float)res};
      std::vector<SwPoint> buf((size_t)idxv + 1);
      buf[(size_t)idxv].Scale = {1.0f, 2.0f, 3.0f};
      std::vector<SwPoint> dst;
      if (!disp.dispatch(P, buf, dst) || dst.size() != buf.size()) continue;
      mathv_ref::ClearSomePointsParams prm = refParamsFrom(P);
      SwPoint refIn{}; refIn.Scale = {1.0f, 2.0f, 3.0f};
      SwPoint refOut{};
      mathv_ref::clearSomePointsOne(refIn, refOut, (uint32_t)idxv, prm);
      const SwPoint& g = dst[(size_t)idxv];
      bool gCleared = std::isnan(g.Scale.x);
      bool rCleared = std::isnan(refOut.Scale.x);
      ++total;
      if (gCleared == rCleared) ++matched;
      printf("[mathv-clearsomepoints-resolution-ambiguity] Resolution=%d idx=%d: gpu-cleared=%s "
             "ref(HLSL-pinned)-cleared=%s %s\n",
             res, idxv, gCleared ? "yes" : "no", rCleared ? "yes" : "no",
             (gCleared == rCleared) ? "(agree)" : "(DIVERGE, documented AMBIGUITY)");
    }
  }
  return total ? (double)matched / (double)total : 1.0;
}

}  // namespace

int runMathvClearSomePointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) {
    printf("[selftest-mathv-clearsomepoints] FAIL: no metallib\n");
    return 1;
  }
  ClearDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) {
    printf("[selftest-mathv-clearsomepoints] FAIL: no clearsomepoints kernel\n");
    return 1;
  }

  uint32_t refIdxCursor = 0;  // IDX BRIDGE state (file header) — reset in c.gpu, consumed by c.ref.

  MathvCase c;
  c.opName = "clearsomepoints";
  c.params = paramTable();
  c.inDim = c.outDim = 3;                // Scale — the only channel this kernel ever writes
  c.eps = EpsSpec::exact();              // integer hash + one float compare, no transcendentals
  c.inputLo = -4.0f; c.inputHi = 4.0f;   // Scale magnitude domain (irrelevant to the kill decision)
  // identity: Ratio=-1 is <= hash11u's provable min 0.0 only when hash==0 (ref case 5 uses -1.0
  // strictly-below); hash<=-1 is false for every hash -> Scale passes through unchanged always.
  c.identityParams = {{-1.0f, 2.0f, 5.0f, 3.0f}};
  // measured: nonIdentityFrac()==0.00000 EXACTLY, every run — NOT a dead corpus. A cleared point
  // sets all 3 Scale lanes to NaN; Liveness::add() only counts a lane non-identity when BOTH
  // in/out are finite, silently excluding NaN from both buckets: STRUCTURALLY 0.0 whether clearing
  // works or is broken. floor=0.0 is the true ceiling (variance()>0 still holds);
  // checkClearingRateLiveness() below is the REAL anti-hollow proof (~50% clear at Ratio=0.5).
  c.minLivenessFrac = 0.0f;
  c.gpu = [&disp, &refIdxCursor](const std::vector<float>& P, const std::vector<float>& in,
                                 std::vector<float>& out) -> bool {
    refIdxCursor = 0;  // batch boundary: the ONE place a new dispatch starts (IDX BRIDGE, header).
    std::vector<SwPoint> src(in.size() / 3), dst;
    for (size_t i = 0; i < src.size(); ++i)
      src[i].Scale = {in[i * 3 + 0], in[i * 3 + 1], in[i * 3 + 2]};
    if (!disp.dispatch(P, src, dst) || dst.size() != src.size()) return false;
    out.resize(dst.size() * 3);
    for (size_t i = 0; i < dst.size(); ++i) {
      out[i * 3 + 0] = dst[i].Scale.x;
      out[i * 3 + 1] = dst[i].Scale.y;
      out[i * 3 + 2] = dst[i].Scale.z;
    }
    return true;
  };
  c.ref = [&refIdxCursor](const std::vector<float>& P, const float* in, float* out) {
    mathv_ref::ClearSomePointsParams prm = refParamsFrom(P);
    SwPoint pin{}, pout{};
    pin.Scale = {in[0], in[1], in[2]};
    const uint32_t idx = refIdxCursor++;
    mathv_ref::clearSomePointsOne(pin, pout, idx, prm);
    out[0] = pout.Scale.x; out[1] = pout.Scale.y; out[2] = pout.Scale.z;
  };

  bool passScale = mathv::runMathvFuzz(c, injectBug);
  if (injectBug) return mathv::mathvVerdictToExit(passScale, true, "clearsomepoints");

  bool passPassthrough = checkPassthroughTooth(disp);
  bool passRepeat1 = checkRepeatOneCollapseTooth(disp);
  bool passRepeatNeg = checkRepeatNegativeTooth(disp);
  bool passSeedWrap = checkSeedWraparoundTooth(disp);
  bool passClearingRate = checkClearingRateLiveness(disp);
  double resAmbigFrac = probeResolutionNonPositive(disp);

  ParityReport rep("selftest-mathv-clearsomepoints");
  rep.expectTrue("scale(3-layer fuzz, exact, idx-threaded via reset-cursor bridge)", passScale,
                 passScale ? 1.0 : 0.0);
  rep.expectTrue("passthrough(Position/FX1/Rotation/Color/FX2 always unchanged)", passPassthrough,
                 passPassthrough ? 1.0 : 0.0);
  rep.expectTrue("quirk(Repeat==1 collapse, pinned parity — RED=regression)", passRepeat1,
                 passRepeat1 ? 1.0 : 0.0);
  rep.expectTrue("quirk(Repeat<0 huge modulus, pinned parity — RED=regression)", passRepeatNeg,
                 passRepeatNeg ? 1.0 : 0.0);
  rep.expectTrue("quirk(Seed uint-wraparound, pinned parity — RED=regression)", passSeedWrap,
                 passSeedWrap ? 1.0 : 0.0);
  rep.expectTrue("clearingRate(real anti-hollow liveness, replaces NaN-blind generic check)",
                 passClearingRate, passClearingRate ? 1.0 : 0.0);
  rep.expectTrue("quirk(Resolution<=0, documented AMBIGUITY, non-gating, see stdout evidence)",
                 true, resAmbigFrac);
  return rep.finish();
}

// order 1004: appends after mathv-snappointstogrid (1003).
REGISTER_SELFTESTS(/*orderBase=*/1004,
                   {"mathv-clearsomepoints", runMathvClearSomePointsSelfTest});

}  // namespace sw
