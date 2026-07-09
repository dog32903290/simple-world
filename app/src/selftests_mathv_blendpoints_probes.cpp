// selftests_mathv_blendpoints_probes.cpp — idx-real / quirk / boundary probes for the BlendPoints
// mathv TU, split out of selftests_mathv_blendpoints.cpp (§1.4 pre-emptive split: five idx-real
// teeth do not fit in one <400-line TU alongside PRIMARY — mathv_snappointstogrid_probes.cpp
// precedent). Called from runMathvBlendPointsSelfTest() (the other TU) via the declarations in
// selftests_mathv_blendpoints_shared.h; not independently registered/dispatched.
//
// Every tooth here dispatches a REAL N-thread batch (aligned countA==countB==resultCount unless a
// tooth is specifically testing count mismatch) and compares GPU vs mathv_ref::blendPointsOne called
// with the SAME real idx/resultCount/countA/countB the GPU thread used — unlike PRIMARY, which can
// only use the idx-independent Mix/Scatter=0 slice, every tooth below is free to use the REAL
// per-thread t=idx/(resultCount-1) because it computes ref per-element with the true idx it belongs
// to (see mathv_ref::blendPointsOne's direct-call shape in each function below).
//
// ZONE: shell tier (app/src/ root, mathv support). Same zone as the TU it splits from.
#include "selftests_mathv_blendpoints_shared.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace sw {
namespace mathv_bp_shared {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::Rng;

// ── TOOTH BLENDMODE: enum {0..4} exhaustive (§3 enum 逐值全掃), REAL per-thread idx/t so Ranged
// (mode 3, :226) and RangedSmooth (mode 4, fmod-fold + SmootherStep, :230-236) get genuine coverage
// — unreachable from PRIMARY's idx-independent slice. Full 16-lane output comparison per element
// (every written channel, equal strength).
bool checkBlendModeTooth(const BlendPointsDispatch& disp) {
  Comparator cmp("mathv-blendpoints-blendmode", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("blendpoints-blendmode"));
  const uint32_t N = 200;
  const char* modeTags[5] = {"blendmode-0-mix", "blendmode-1-useaf1", "blendmode-2-usebf1",
                              "blendmode-3-ranged", "blendmode-4-rangedsmooth"};
  bool dispatchOk = true;
  for (int mode = 0; mode <= 4; ++mode) {
    std::vector<SwPoint> A(N), B(N);
    for (uint32_t i = 0; i < N; ++i) { fillRandomPoint(rng, A[i]); fillRandomPoint(rng, B[i]); }
    // BlendFactor spans [-1,2] (overshoot + RangedSmooth's fmod(.,2) fold, PRIMARY's cited domain);
    // Width kept away from 0 (near-zero blows Ranged's division up consistently on both sides --
    // not this tooth's concern, see file header).
    float blendFactor = rng.uniform(-1.0f, 2.0f);
    float width = rng.uniform(0.2f, 2.0f);
    BlendPointsParams hprm = hostParamsFrom(blendFactor, (float)mode, 0.0f, width, 0.0f);
    mathv_ref::BlendPointsParams rprm = refParamsFrom(blendFactor, (float)mode, 0.0f, width, 0.0f);
    std::vector<SwPoint> out;
    if (!disp.dispatch(hprm, A, B, out) || out.size() != N) { dispatchOk = false; continue; }
    for (uint32_t i = 0; i < N; ++i) {
      SwPoint refOut{};
      mathv_ref::blendPointsOne(A.data(), N, B.data(), N, N, i, rprm, refOut);
      float in16[16]; packPoint(A[i], in16);
      float g16[16], r16[16]; packPoint(out[i], g16); packPoint(refOut, r16);
      for (int k = 0; k < 16; ++k) cmp.add(g16[k], r16[k], in16, 16, k, -1.0f, modeTags[mode]);
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── TOOTH SCATTER: Scatter!=0, real idx/t across a real N=257-thread batch (spans >4 threadgroups
// of 64, addnoise TOOTH VARIATION/IDX precedent) — verifies hash11(t) jitter + the
// fallOffFromCenter*abs(f-0.5) weighting (:240-241) bit-for-bit against shared/hash.metal.h.
bool checkScatterTooth(const BlendPointsDispatch& disp) {
  Comparator cmp("mathv-blendpoints-scatter", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("blendpoints-scatter"));
  const uint32_t N = 257;
  const float scatters[] = {0.1f, 0.5f, 1.0f, 2.0f, -0.5f};
  const char* scatterTags[] = {"scatter-0.1", "scatter-0.5", "scatter-1.0", "scatter-2.0",
                                "scatter-neg0.5"};
  bool dispatchOk = true;
  for (int si = 0; si < 5; ++si) {
    std::vector<SwPoint> A(N), B(N);
    for (uint32_t i = 0; i < N; ++i) { fillRandomPoint(rng, A[i]); fillRandomPoint(rng, B[i]); }
    float blendFactor = rng.uniform(0.0f, 1.0f);
    BlendPointsParams hprm = hostParamsFrom(blendFactor, 0.0f, 0.0f, 0.5f, scatters[si]);
    mathv_ref::BlendPointsParams rprm = refParamsFrom(blendFactor, 0.0f, 0.0f, 0.5f, scatters[si]);
    std::vector<SwPoint> out;
    if (!disp.dispatch(hprm, A, B, out) || out.size() != N) { dispatchOk = false; continue; }
    for (uint32_t i = 0; i < N; ++i) {
      SwPoint refOut{};
      mathv_ref::blendPointsOne(A.data(), N, B.data(), N, N, i, rprm, refOut);
      float in16[16]; packPoint(A[i], in16);
      float g16[16], r16[16]; packPoint(out[i], g16); packPoint(refOut, r16);
      for (int k = 0; k < 16; ++k) cmp.add(g16[k], r16[k], in16, 16, k, -1.0f, scatterTags[si]);
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── TOOTH NOBLEND-NAN (§9 isnan/fast-math trap, PINNED): isnan(A.Scale.x*B.Scale.x) (:243-246)
// collapses f to a hard 0/1 -- reachable per-ELEMENT via BlendMode=UseA_F1 (f=A.FX1, :221) so one
// dispatch covers both sides of the f<0.5 threshold plus an ordinary (no-NaN) control element. A
// Metal build that fast-math-folded isnan() to constant-false would leave Scale.x itself NaN (no
// collapse) -- gpuFinite/refFinite disagreeing is exactly that regression.
bool checkNoBlendNanTooth(const BlendPointsDispatch& disp) {
  Rng rng(mathv::mathvSeed("blendpoints-noblend-nan"));
  const uint32_t N = 4;
  std::vector<SwPoint> A(N), B(N);
  for (uint32_t i = 0; i < N; ++i) { fillRandomPoint(rng, A[i]); fillRandomPoint(rng, B[i]); }
  A[0].FX1 = 0.2f; A[0].Scale.x = std::nanf("");   // rawF=0.2<0.5, A NaN -> collapse to Scale=A
  A[1].FX1 = 0.8f; A[1].Scale.x = std::nanf("");   // rawF=0.8>=0.5, A NaN -> collapse to Scale=B
  A[2].FX1 = 0.3f; B[2].Scale.x = std::nanf("");   // rawF=0.3<0.5, B NaN (product still NaN) -> Scale=A
  A[3].FX1 = 0.6f;                                 // control: no NaN -> ordinary lerp, NOT collapsed

  BlendPointsParams hprm = hostParamsFrom(0.0f, /*mode=UseA_F1*/ 1.0f, 0.0f, 0.5f, 0.0f);
  mathv_ref::BlendPointsParams rprm = refParamsFrom(0.0f, 1.0f, 0.0f, 0.5f, 0.0f);
  std::vector<SwPoint> out;
  bool dispatched = disp.dispatch(hprm, A, B, out) && out.size() == N;

  auto allFinite = [](const SwPoint& p) {
    return std::isfinite(p.Scale.x) && std::isfinite(p.Scale.y) && std::isfinite(p.Scale.z);
  };
  const char* labels[4] = {"elem0(A-NaN,f<0.5->Scale=A)", "elem1(A-NaN,f>=0.5->Scale=B)",
                            "elem2(B-NaN,f<0.5->Scale=A)", "elem3(control,no-NaN,ordinary-lerp)"};
  bool ok = dispatched;
  for (uint32_t i = 0; i < N && dispatched; ++i) {
    SwPoint refOut{};
    mathv_ref::blendPointsOne(A.data(), N, B.data(), N, N, i, rprm, refOut);
    bool refFin = allFinite(refOut), gpuFin = allFinite(out[i]);
    bool matchClass = (refFin == gpuFin);
    bool scaleOk;
    if (i == 3) {
      scaleOk = std::fabs(out[i].Scale.x - refOut.Scale.x) < 1e-4f;
    } else {
      // collapse selects A (f<0.5) or B (>=0.5); for elem0 the selected side (A) IS the NaN-marked
      // one, so `expect` is itself NaN there -- MEASURED (a first pass comparing via subtraction
      // against a NaN `expect` always reads false, since x-NaN==NaN and NaN<1e-4f is false, even
      // though both sides correctly produced NaN): the isnan-aware branch below is required, not an
      // arbitrary relaxation.
      float expect = (i == 1) ? B[i].Scale.x : A[i].Scale.x;
      if (std::isnan(expect)) {
        scaleOk = std::isnan(out[i].Scale.x) && std::isnan(refOut.Scale.x);
      } else {
        scaleOk =
            std::fabs(out[i].Scale.x - expect) < 1e-4f && std::fabs(refOut.Scale.x - expect) < 1e-4f;
      }
    }
    printf("[mathv-blendpoints-noblend-nan] %s gpuFinite=%s refFinite=%s scaleOk=%s\n", labels[i],
           gpuFin ? "yes" : "no", refFin ? "yes" : "no", scaleOk ? "yes" : "no");
    ok = ok && matchClass && scaleOk;
  }
  printf("[mathv-blendpoints-noblend-nan] verdict: %s\n", ok ? "PINNED" : "RED -- regression");
  return ok;
}

// ── TOOTH PAIRING-COUNTS: (1) PairingMode dead-branch proof -- resultCount==CountA is HARDCODED
// (blendpoints.metal:48), so firstIx=max(firstIxA,firstIxB)>=firstIxA==floor(i*CountA/CountA)==i
// STRUCTURALLY (max() can only grow, never shrink, regardless of what firstIxB evaluates to even in
// the countB==0 div-by-zero corner) -- the Adjust skip-gate `i>firstIx` (:63-68) can therefore NEVER
// fire, for ANY countA/countB combo. Proof: dispatch identical inputs under PairingMode=0 vs 1 and
// assert bit-identical GPU output. (2) b-count-guard agreement (blendpoints_params.h alignment note
// vs mathv_ref_blendpoints.h detail::readPointOrZero): countB<countA -> BOTH sides now read a ZERO B
// point for i>=countB (D3D OOB==0, the kernel zero-fills instead of clamping to B[countB-1]) -- this
// used to be a documented, EXPECTED divergence; after the b-count-guard fix it is asserted as
// AGREEMENT (rows all agree, no fork left to record).
bool checkPairingCountsTooth(const BlendPointsDispatch& disp) {
  Rng rng(mathv::mathvSeed("blendpoints-pairing-counts"));
  bool ok = true;

  const uint32_t countCombos[][2] = {{5, 5}, {5, 9}, {9, 5}, {3, 1}, {1, 3}, {7, 0}};
  for (auto& cc : countCombos) {
    uint32_t countA = cc[0], countB = cc[1];
    std::vector<SwPoint> A(countA), B(countB);
    for (auto& p : A) fillRandomPoint(rng, p);
    for (auto& p : B) fillRandomPoint(rng, p);
    BlendPointsParams p0 = hostParamsFrom(0.5f, 0.0f, /*pairing=*/0.0f, 0.5f, 0.0f);
    BlendPointsParams p1 = hostParamsFrom(0.5f, 0.0f, /*pairing=*/1.0f, 0.5f, 0.0f);
    std::vector<SwPoint> out0, out1;
    bool d0 = disp.dispatch(p0, A, B, out0), d1 = disp.dispatch(p1, A, B, out1);
    bool identical = d0 && d1 && out0.size() == countA && out1.size() == countA;
    if (identical) {
      // countA==1 makes resultCount==1 -> t=idx/(resultCount-1)==0/0==NaN (the documented
      // singularity, mathv_ref_blendpoints.h :180-183); with Scatter==0 pinned, hash11(NaN)*0==NaN
      // (NOT 0 -- NaN propagates through multiplication by zero in IEEE754), so BOTH PairingMode legs
      // legitimately produce a NaN-poisoned f/output here. MEASURED (a first pass using raw `==`
      // reported "pairing has an effect" for exactly this ONE combo, {1,3}): NaN!=NaN under `==`, so
      // two independently-NaN-poisoned outputs read as "different" even though both sides are
      // equally (and correctly) undefined -- the isnan-aware match below is required, not a
      // relaxation of the dead-gate claim itself.
      for (uint32_t i = 0; i < countA; ++i) {
        float g0[16], g1[16]; packPoint(out0[i], g0); packPoint(out1[i], g1);
        for (int k = 0; k < 16; ++k) {
          bool same = (g0[k] == g1[k]) || (std::isnan(g0[k]) && std::isnan(g1[k]));
          identical = identical && same;
        }
      }
    }
    printf("[mathv-blendpoints-pairing-counts] deadGate countA=%u countB=%u -> %s\n", countA, countB,
           identical ? "identical(pairing-dead, as proven)" : "RED -- pairing has an effect");
    ok = ok && identical;
  }

  {
    const uint32_t countA = 6, countB = 3;
    std::vector<SwPoint> A(countA), B(countB);
    for (auto& p : A) fillRandomPoint(rng, p);
    for (auto& p : B) fillRandomPoint(rng, p);
    BlendPointsParams hprm = hostParamsFrom(0.5f, 0.0f, 0.0f, 0.5f, 0.0f);
    mathv_ref::BlendPointsParams rprm = refParamsFrom(0.5f, 0.0f, 0.0f, 0.5f, 0.0f);
    std::vector<SwPoint> out;
    bool dispatched = disp.dispatch(hprm, A, B, out) && out.size() == countA;
    int agreeRows = 0, disagreeRows = 0;
    for (uint32_t i = 0; dispatched && i < countA; ++i) {
      SwPoint refOut{};
      mathv_ref::blendPointsOne(A.data(), countA, B.data(), countB, countA, i, rprm, refOut);
      bool bIsOob = (i >= countB);
      float gPos = out[i].Position.x, rPos = refOut.Position.x;
      bool agrees = std::fabs(gPos - rPos) < 1e-4f;
      if (bIsOob) {
        // both sides now zero-fill B for i>=countB (D3D SRV OOB-read==0 contract), so ref and GPU
        // must land on the SAME expected value -- no clamp-vs-zero divergence left to predict.
        float expectPos = A[i].Position.x + 0.5f * (0.0f - A[i].Position.x);
        bool matchesPredicted = std::fabs(rPos - expectPos) < 1e-3f && std::fabs(gPos - expectPos) < 1e-3f;
        agrees = agrees && matchesPredicted;
        printf("[mathv-blendpoints-pairing-counts]   b-oob i=%u ref=%.6f gpu=%.6f (both zero-B) "
               "agrees=%s\n", i, rPos, gPos, agrees ? "yes" : "NO-UNEXPECTED");
      }
      if (agrees) ++agreeRows; else ++disagreeRows;
    }
    bool allAgree = dispatched && disagreeRows == 0 && agreeRows == (int)countA;
    printf("[mathv-blendpoints-pairing-counts] b-count-guard rows=%d/%u agree, disagree=%d -> %s\n",
           agreeRows, countA, disagreeRows,
           allAgree ? "ALL AGREE (aligned to TiXL, no fork)" : "RED -- unexpected divergence");
    ok = ok && allAgree;
  }
  return ok;
}

// ── TOOTH MODE4-SCATTER-FOLD: BlendMode=4 (RangedSmooth) TOGETHER WITH Scatter!=0 -- the only
// combination that exercises hash11(t) reading the POST-fold `t` (BlendPoints.hlsl:84 `t = 1 - t`
// mutates the function-scope t that :92's hash11(t) subsequently reads). Neither existing tooth
// covers this: checkBlendModeTooth (above) pins Scatter=0 for every mode; checkScatterTooth pins
// BlendMode=Mix(0) for every scatter value. Anchor probe values: BlendFactor in {0.5 (b<=1, NO
// fold taken), 1.5 (b>1, fold IS taken)}, Width=1, Scatter=1, N=11. BEFORE mathv_ref_blendpoints.h's
// :84 fix (an earlier revision folded into a throwaway local `tt`, leaving hash11(t) reading the
// UN-mirrored t while the GPU kernel mutates t in place) this tooth was RED on the bf=1.5 leg only
// (bf=0.5 never takes the fold branch, so it was already GREEN even with the bug) -- AFTER the fix
// both legs are GREEN.
bool checkMode4ScatterFoldTooth(const BlendPointsDispatch& disp) {
  Comparator cmp("mathv-blendpoints-mode4-scatter-fold", EpsSpec::transcendental(), 5);
  Rng rng(mathv::mathvSeed("blendpoints-mode4-scatter-fold"));
  const uint32_t N = 11;
  const float blendFactors[2] = {0.5f, 1.5f};
  const char* bfTags[2] = {"bf-0.5(b<=1,no-fold)", "bf-1.5(b>1,fold-taken)"};
  bool dispatchOk = true;
  for (int bi = 0; bi < 2; ++bi) {
    std::vector<SwPoint> A(N), B(N);
    for (uint32_t i = 0; i < N; ++i) { fillRandomPoint(rng, A[i]); fillRandomPoint(rng, B[i]); }
    BlendPointsParams hprm = hostParamsFrom(blendFactors[bi], /*mode=*/4.0f, /*pairing=*/0.0f,
                                             /*width=*/1.0f, /*scatter=*/1.0f);
    mathv_ref::BlendPointsParams rprm =
        refParamsFrom(blendFactors[bi], 4.0f, 0.0f, 1.0f, 1.0f);
    std::vector<SwPoint> out;
    if (!disp.dispatch(hprm, A, B, out) || out.size() != N) { dispatchOk = false; continue; }
    for (uint32_t i = 0; i < N; ++i) {
      SwPoint refOut{};
      mathv_ref::blendPointsOne(A.data(), N, B.data(), N, N, i, rprm, refOut);
      float in16[16]; packPoint(A[i], in16);
      float g16[16], r16[16]; packPoint(out[i], g16); packPoint(refOut, r16);
      for (int k = 0; k < 16; ++k) cmp.add(g16[k], r16[k], in16, 16, k, -1.0f, bfTags[bi]);
    }
  }
  cmp.print();
  return dispatchOk && cmp.verdict();
}

// ── TOOTH OFF-BY-ONE: over-dispatch idx==resultCount boundary. mathv_ref_blendpoints.h :168-174
// documents the transcribed HLSL guard as strict `>` (idx==resultCount WOULD write) while
// blendpoints.metal guards `>=` (does NOT write) -- registered as fork[guard-off-by-one] in both
// files' NAMED-FORKS bullet lists. sw's `>=` is a deliberate, memory-safe fix of a real upstream
// off-by-one; this tooth is the probe that confirms the fork behaves exactly as documented.
bool checkOffByOneTooth(const BlendPointsDispatch& disp) {
  Rng rng(mathv::mathvSeed("blendpoints-off-by-one"));
  const uint32_t countA = 5;
  std::vector<SwPoint> A(countA), B(countA);
  for (auto& p : A) fillRandomPoint(rng, p);
  for (auto& p : B) fillRandomPoint(rng, p);
  BlendPointsParams hprm = hostParamsFrom(0.5f, 0.0f, 0.0f, 0.5f, 0.0f);
  mathv_ref::BlendPointsParams rprm = refParamsFrom(0.5f, 0.0f, 0.0f, 0.5f, 0.0f);

  // Over-dispatch: launch countA+2 GPU threads (tid.x in [0,countA+1]) against a Result buffer sized
  // countA+2, pre-filled with an implausible sentinel -- so we can inspect whether thread
  // idx==countA(==resultCount) wrote anything.
  SwPoint sentinel{};
  sentinel.FX2 = -999.0f;
  std::vector<SwPoint> out;
  bool dispatched =
      disp.dispatch(hprm, A, B, countA + 2, countA + 2, out, sentinel) && out.size() == countA + 2;

  // ref: blendPointsOne itself supports idx==resultCount (transcribed guard is strict `>`). Calling
  // it DIRECTLY (not through mathvRefBlendPoints's idx<resultCount loop) at idx==countA reproduces
  // the HLSL text's literal behavior: it computes and WOULD write.
  SwPoint refAtBoundary{};
  bool refWrote = mathv_ref::blendPointsOne(A.data(), countA, B.data(), countA,
                                            /*resultCount=*/countA, /*idx=*/countA, rprm, refAtBoundary);
  bool gpuUntouched = dispatched && std::fabs(out[countA].FX2 - sentinel.FX2) < 1e-4f;
  bool refComputedReal =
      refWrote && std::isfinite(refAtBoundary.FX2) && std::fabs(refAtBoundary.FX2 - sentinel.FX2) > 1e-3f;

  printf("[mathv-blendpoints-off-by-one] idx==resultCount(%u): ref wrote=%s (FX2=%.6f) gpu "
         "untouched=%s (FX2=%.6f, sentinel=%.6f)\n", countA, refWrote ? "yes" : "no",
         (double)refAtBoundary.FX2, gpuUntouched ? "yes" : "no",
         dispatched ? (double)out[countA].FX2 : (double)std::nanf(""), (double)sentinel.FX2);

  // in-bounds sanity: idx==countA-1 (last REAL thread) must still match ordinarily on both sides.
  SwPoint refLast{};
  mathv_ref::blendPointsOne(A.data(), countA, B.data(), countA, countA, countA - 1, rprm, refLast);
  bool lastOk = dispatched && std::fabs(out[countA - 1].FX2 - refLast.FX2) < 1e-4f;

  bool asDocumented = dispatched && refWrote && refComputedReal && gpuUntouched && lastOk;
  printf("[mathv-blendpoints-off-by-one] fork[guard-off-by-one]: %s\n",
         asDocumented ? "CONFIRMED as documented" : "RED -- unexpected shape");
  return asDocumented;
}

}  // namespace mathv_bp_shared
}  // namespace sw
