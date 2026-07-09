// selftests_mathv_clearsomepoints.cpp — --selftest-mathv-clearsomepoints (D role, fuzz driver).
// MATH_VERIFY_WORKFLOW.md 量產鏈 Tier-L: fuzz the GPU "clearsomepoints" kernel
// (app/shaders/clearsomepoints.metal) against the R-authored CPU oracle
// (app/src/mathv_ref_clearsomepoints.h, TRANSCRIBED from external/tixl HLSL) via the shared mathv
// harness (direct-kernel dispatch, §1.3 — NOT buildEvalGraph).
//
// ── ParamDomain provenance (external/tixl SHA 395c4c55) ──────────────────────────────────────────
// ClearSomePoints.t3:4-19 authors DEFAULTS (Ratio=0.0, Resolution=0, Repeat=0, Seed=0);
// ClearSomePoints.t3ui:13-32 authors Min/ClampMin/ClampMax for Resolution (Min=1) and Repeat
// (Min=0) only — Ratio/Seed have NO Min/Max. Per §3.2 ("無 Min/Max 用 DefaultValue 鄰域 ×[-4,+4]")
// Ratio/Seed use the DefaultValue±4 fallback; Resolution/Repeat honor their authored floor with a
// finite window (see paramTable() below for exact cites). Resolution<=0 and Repeat<0 sit OUTSIDE
// the authored/swept domain on purpose — they are QUIRK PROBES (see below), not main-domain fuzz.
//
// ── IDX BRIDGE (novel for this op — first idx-DEPENDENT kernel in the mathv batch) ────────────────
// Unlike WrapPointPosition/AddNoise/SnapPointsToGrid (whose per-point math is idx-independent, so
// their ref lambdas hardcode idx=0), ClearSomePoints' entire hash pipeline is keyed on `i.x` (the
// GPU thread's buffer position) — mathv_ref_clearsomepoints.h's computePointU() takes idx as a
// first-class argument. But MathvCase::ref's signature (P, in-element-pointer, out) carries NO idx.
// Bridge: mathv_harness.h's runCompare calls `c.gpu(...)` EXACTLY ONCE per batch (building ONE real
// n-point buffer, dispatched so GPU thread i really computes at i.x==i) and THEN calls `c.ref(...)`
// n times in ascending order — see mathv_harness.h runCompare. `refIdxCursor` (captured by
// reference in both lambdas below) is reset to 0 inside `c.gpu` — the only place a new batch
// boundary is visible — and incremented once per `c.ref` call, so it always reproduces the SAME
// idx sequence 0..n-1 the GPU dispatch actually used for that call. This depends on nothing but the
// documented gpu-then-ref calling contract already fixed by the shared harness.
//
// ── QUIRK PROBES (5, from mathv_ref_clearsomepoints.h) ────────────────────────────────────────────
//   1. uint promotion wraparound (ref :101-113)        -> checkSeedWraparoundTooth (pinned, GREEN)
//   2. Resolution==0 div-by-zero AMBIGUITY (ref :74-88) -> probeResolutionNonPositive (non-gating)
//   3. Repeat<0 huge modulus (ref :121-125)              -> checkRepeatNegativeTooth (pinned, GREEN)
//   4. Repeat==1 collapse (ref :198-203)                 -> checkRepeatOneCollapseTooth (pinned, GREEN)
//   5. no idx>=pointCount guard in the HLSL (ref :135-137, dead `GetDimensions` locals only) — the
//      sw kernel DOES guard via P.Count (clearsomepoints.metal:61); nothing to probe, noted only.
// probeResolutionNonPositive is the ONE non-gating probe: clearsomepoints.metal:14-20 already
// documents cwMod's `repeat<=0 -> 0` guard as a NAMED FORK against the ref's HLSL-pinned
// div-by-zero convention (-1) — a real, already-cited divergence, not a fuzz discovery, so it is
// recorded as evidence rather than gated red/green (§7 "sw NAMED FORK -> pin 引註").
//
// ZONE: shell tier (app/src/ root, mathv support). Crosses runtime only for the SwPoint layout +
// the kernel's params ABI header (data layout, not math).
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
  ~ClearDispatch() {
    if (pso) pso->release();
  }
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
    params.Seed = (int32_t)P[1];
    params.Repeat = (int32_t)P[2];
    params.Resolution = (int32_t)P[3];
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
  p.seed = (int32_t)P[1];
  p.repeat = (int32_t)P[2];
  p.resolution = (int32_t)P[3];
  return p;
}

const std::vector<ParamDomain>& paramTable() {
  static const std::vector<ParamDomain> t = {
      {"Ratio", -4.0f, 4.0f, ParamDomain::Linear,
       "external/tixl ClearSomePoints.t3:4-7 Ratio DefaultValue 0.0, no Min/Max in "
       "ClearSomePoints.t3ui -> MATH_VERIFY_WORKFLOW.md §3.2 DefaultValue±4 fallback"},
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
// thread i.x==idxv really executes — the production dispatch shape, distinct from the generic
// layer's reset-cursor IDX BRIDGE above, which only needs idx to start at 0 per call).
void oneAt(const ClearDispatch& disp, Comparator& cmp, float ratio, int32_t seed, int32_t repeat,
           int32_t resolution, uint32_t idxv, const char* batch) {
  std::vector<float> P = {ratio, (float)seed, (float)repeat, (float)resolution};
  std::vector<SwPoint> buf((size_t)idxv + 1);
  buf[(size_t)idxv].Scale = {1.0f, 2.0f, 3.0f};
  std::vector<SwPoint> dst;
  if (!disp.dispatch(P, buf, dst) || dst.size() != buf.size()) return;
  mathv_ref::ClearSomePointsParams prm = refParamsFrom(P);
  SwPoint refIn{};
  refIn.Scale = {1.0f, 2.0f, 3.0f};
  SwPoint refOut{};
  mathv_ref::clearSomePointsOne(refIn, refOut, idxv, prm);
  const SwPoint& g = dst[(size_t)idxv];
  float in3[3] = {1.0f, 2.0f, 3.0f};
  cmp.add(g.Scale.x, refOut.Scale.x, in3, 3, 0, -1.0f, batch);
  cmp.add(g.Scale.y, refOut.Scale.y, in3, 3, 1, -1.0f, batch);
  cmp.add(g.Scale.z, refOut.Scale.z, in3, 3, 2, -1.0f, batch);
}

// ── TOOTH PASSTHROUGH: every non-Scale channel (Position/FX1/Rotation/Color/FX2) is untouched by
// this kernel REGARDLESS of the clear decision (:44 ResultPoints[i.x]=p carries the whole point;
// only Scale is ever conditionally overwritten, ref :140/:170). Exact eps — plain copy-through. ──
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

// quirk 4 — Repeat==1 collapse (ref :198-203): divisor==1 -> pointU==0 for ANY idx/Seed/Resolution
// -> hash11u(0)==0.0 exactly -> Ratio=0.0 clears (inclusive <=). PINNED PARITY: GREEN expected.
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
// uint32 BEFORE the outer modulo, identically on both the ref (divisorSigned/divisor) and the
// shader (`(uint)(P.Repeat==0?999999999:P.Repeat)`, clearsomepoints.metal:68). Symmetric port, not
// an ambiguity — PINNED PARITY: GREEN expected.
bool checkRepeatNegativeTooth(const ClearDispatch& disp) {
  Comparator cmp("mathv-clearsomepoints-repeat-negative", EpsSpec::exact(), 5);
  const int32_t repeats[] = {-1, -5, -1000, -2000000000};
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

// quirk 1 — uint promotion wraparound (ref :101-113): Seed(int32) bit-reinterprets to uint32
// BEFORE the *_PRIME1 multiply. Values stay exactly float-representable (+/-2^24 ceiling): the
// shared MathvCase P-vector round-trips every param through `float`, and INT32_MIN/MAX would
// silently corrupt through that round-trip (2147483647 has no exact float representation) instead
// of exercising the kernel — so this probe deliberately avoids them. PINNED PARITY: GREEN expected.
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

// quirk 2 — Resolution<=0 (ref :74-88 AMBIGUITY; clearsomepoints.metal:14-20 NAMED FORK): HLSL's
// div-by-zero is D3D-DEFINED (ref pins Mod(val,0)->-1); the shipped MSL guards the SAME condition
// by short-circuiting to modVal=0 instead (an already-cited fork, not a fuzz discovery). NON-GATING:
// record both sides verbatim as an AMBIGUITY evidence pack (§7 "HLSL 本體歧義").
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
      SwPoint refIn{};
      refIn.Scale = {1.0f, 2.0f, 3.0f};
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
  // identity sentinel: Ratio=-1 is BELOW hash11u's provable min of 0.0 (ref self-check case 5) ->
  // hash<=-1 is false for every hash -> Scale passes through unchanged for ANY idx/Seed/Repeat/Res.
  c.identityParams = {{-1.0f, 2.0f, 5.0f, 3.0f}};
  // measured (seed printed at run time, SW_MATHV_SEED unset): Ratio's domain is [-4,4] but only
  // Ratio∈[0,1) partially clears / Ratio>=1 fully clears / Ratio<0 never clears (ref self-check
  // cases 4/5) — with a domain this wide, roughly half of the 8 random-layer param draws land
  // Ratio<0 (0% nonIdentity for that whole 4096-element batch), so the corpus does NOT naturally
  // clear >=90% of elements. Measured nonIdentityFrac ~0.40-0.55 across seeds (see build/run log);
  // floor set below with headroom under that measurement, not at the framework default.
  c.minLivenessFrac = 0.20f;
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
  rep.expectTrue("quirk(Resolution<=0, documented AMBIGUITY, non-gating, see stdout evidence)",
                 true, resAmbigFrac);
  return rep.finish();
}

// order 1004: appends after mathv-snappointstogrid (1003).
REGISTER_SELFTESTS(/*orderBase=*/1004,
                   {"mathv-clearsomepoints", runMathvClearSomePointsSelfTest});

}  // namespace sw
