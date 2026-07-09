#pragma once
// mathv_harness — the shared fuzz driver for mathv (math-verify) goldens
// (docs/agent/MATH_VERIFY_WORKFLOW.md §1). Header-only, shell tier (app/src/ root).
//
// NOT A PARALLEL SYSTEM (§0): the RAII Metal skeleton is REUSED from parity_golden_harness.h
// (sw::ParityHarness — device/queue/metallib bootstrap) and the final verdict table is the existing
// sw::ParityReport organ. What THIS header adds is the fuzz driver: it eats a TU's ParamDomain
// table + a dispatch-adapter lambda + an EpsSpec, runs the three sampling layers from
// mathv_input.h (special-value grid / in-domain random / identity sentinels), feeds every output
// scalar through the mathv_compare.h comparator, and reports.
//
// GPU FEEDING SHAPE (§1.3): direct-kernel dispatch, NEVER buildEvalGraph — each op TU
// (selftests_mathv_<op>.cpp) brings its own ~30-line adapter lambda in the dispatchTurb shape
// (turbulence_parity_golden.cpp:75-112): fill MTLBuffer → setBytes params → dispatch → readback →
// return the outputs as a flat float vector. The adapter is the ONLY place that touches Metal; the
// driver below is adapter-agnostic (which is also what lets selftests_mathv_core.cpp verify the
// comparator with pure-CPU synthetic function pairs — no GPU in the loop).
//
// injectBug (§1.5, P1-safe bite): the -bug leg perturbs the params VECTOR SENT TO THE GPU ADAPTER
// (first float += 1e-2) while the CPU ref keeps the ORIGINAL values → the two sides necessarily
// fork → the comparator MUST flip RED, proving the whole compare path (dispatch → readback →
// comparator gates) is live. This corrupts the real dispatch path, not the expectation (no
// want-flip); a -bug leg that stays green means the tooth is dead — the TU returns 0 for it
// (mathvVerdictToExit) so --bite's NO-BITE list catches it.
//
// CPU REF DISCIPLINE: the `ref` lambda must come from a mathv_ref_<op>.h / *_oracle.h TRANSCRIBED
// from external/tixl HLSL (P5-safe oracle 判準, GOLDEN_STANDARD.md) — never from sw's MSL. The
// `<op>_params.h` ABI header is the ONLY legal shared artifact between adapter and ref (names /
// offsets, not math).
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "mathv_compare.h"
#include "mathv_input.h"
#include "parity_golden_harness.h"  // sw::ParityHarness (RAII Metal) + sw::ParityReport — reused

namespace sw {
namespace mathv {

// Everything one op's fuzz run needs. The TU fills this and calls runMathvFuzz.
struct MathvCase {
  const char* opName = "op";               // seeds the PRNG (splitmix64(fnv1a(opName)))
  std::vector<ParamDomain> params;         // cited domains — see mathv_input.h
  int inDim = 3;                           // floats per input element (e.g. 3 = position)
  int outDim = 3;                          // floats per output element
  EpsSpec eps;                             // one of the three classes (mathv_compare.h)
  float inputLo = -4.0f, inputHi = 4.0f;   // input-element domain (TU may widen, cite why)
  size_t gridElems = 256;                  // elements per special-value-grid batch
  size_t randomVectors = 8;                // param vectors in the random layer (§3 default)
  size_t randomElems = 4096;               // input elements per random batch (§3 default)
  int maxEvidence = 5;                     // mismatch evidence rows printed per comparator
  bool quiet = false;                      // suppress comparator detail when GREEN (meta-tests)
  // Per-op override of the mid-segment liveness floor (mathv_input.h Liveness::pass's default
  // 0.90 threshold). Leave at default for every op whose corpus naturally lands >=90% non-identity;
  // set lower ONLY with a measured comment justifying the actual observed fraction (see
  // selftests_mathv_snappointstogrid.cpp for the first user) -- this is a per-op escape hatch, not a
  // blanket relaxation, so every override must cite its own measurement.
  float minLivenessFrac = 0.90f;

  // GPU dispatch adapter: (params P, inputs N*inDim) -> outputs N*outDim. false = dispatch failed.
  std::function<bool(const std::vector<float>& P, const std::vector<float>& in,
                     std::vector<float>& out)>
      gpu;
  // CPU scalar reference (TiXL-transcribed): per element.
  std::function<void(const std::vector<float>& P, const float* in, float* out)> ref;
  // Branchy class: distance from the nearest branch boundary for (P, element, out lane); return <0
  // for "unknown" (no exemption possible on that lane). Transcendental class REUSES this same
  // channel as the ill-conditioned-lookup candidate flag (mathv_compare.h header note): >=0 =
  // candidate, <0 = not a candidate. Either way the callback owns its own class-appropriate meaning.
  std::function<float(const std::vector<float>& P, const float* in, int lane)> branchDist;
  // Transcendental class only: output-envelope bound for the ill-conditioned-lookup exemption.
  // Consulted ONLY when branchDist(...)>=0 flagged a lane a candidate. The TU computes the bound
  // from that sample's P/in (Comparator itself never sees P) and checks it against the actual
  // gpu/ref magnitudes it's handed. Null (default) => feature inert — zero behavior change for
  // every TU that doesn't set it (mathv_compare.h Comparator::add's envelopeOk stays nullptr).
  std::function<bool(const std::vector<float>& P, const float* in, int lane, float gpu, float ref)>
      illConditionedEnvelope;

  // ≥1 identity param vector (Amount=0 / Scale=1): expected output == input. If the op TRULY has
  // no identity configuration, set noIdentityReason instead (cited) — leaving both empty is RED.
  std::vector<std::vector<float>> identityParams;
  const char* noIdentityReason = nullptr;
};

// Run the three-layer fuzz. Returns the aggregate PASS verdict (the TU maps it to an exit code via
// mathvVerdictToExit so -bug polarity is uniform).
inline bool runMathvFuzz(const MathvCase& c, bool injectBug) {
  char tag[96];
  std::snprintf(tag, sizeof tag, "mathv-%s", c.opName);
  uint64_t seed = mathvSeed(c.opName);
  std::printf("[%s] seed=0x%016llx (SW_MATHV_SEED overrides)%s\n", tag,
              (unsigned long long)seed,
              injectBug ? " [injectBug: gpu-side params[0] += 1e-2, ref keeps original]" : "");

  ParityReport rep(tag);
  const bool structural = (bool)c.gpu && (bool)c.ref && !c.params.empty() && c.inDim > 0 &&
                          c.outDim > 0 && c.gridElems > 0;
  rep.expectTrue("structural(case-complete)", structural, (double)c.params.size());
  if (!structural) return rep.finish() == 0;
  const bool identityDeclared =
      !c.identityParams.empty() || (c.noIdentityReason && *c.noIdentityReason);
  rep.expectTrue("identity(declared-or-cited)", identityDeclared,
                 (double)c.identityParams.size());

  Rng rng(seed);
  // Shared finite lattice for the grid + identity layers (deterministic from the seed).
  std::vector<float> grid(c.gridElems * (size_t)c.inDim);
  for (float& x : grid) x = rng.uniform(c.inputLo, c.inputHi);

  Comparator cmp(tag, c.eps, c.maxEvidence);
  Liveness live;
  bool dispatchOk = true, smokeOk = true;
  int smokeBatches = 0;

  auto gpuParams = [&](const std::vector<float>& P) {
    if (!injectBug) return P;
    std::vector<float> B = P;
    B[0] += 1e-2f;  // §1.5 — corrupts the REAL dispatch path; the ref still sees P
    return B;
  };
  // Full-compare batch: gpu(P') vs ref(P) per element/lane; optionally feeds the liveness probe.
  auto runCompare = [&](const std::vector<float>& P, const std::vector<float>& in,
                        const char* batch, Liveness* lv) {
    const size_t n = in.size() / (size_t)c.inDim;
    std::vector<float> out;
    if (!c.gpu(gpuParams(P), in, out) || out.size() != n * (size_t)c.outDim) {
      dispatchOk = false;
      return;
    }
    std::vector<float> refOut((size_t)c.outDim);
    for (size_t i = 0; i < n; ++i) {
      const float* e = &in[i * (size_t)c.inDim];
      const float* g = &out[i * (size_t)c.outDim];
      c.ref(P, e, refOut.data());
      for (int k = 0; k < c.outDim; ++k) {
        float bd = c.branchDist ? c.branchDist(P, e, k) : -1.0f;
        std::function<bool(float, float)> envOk = nullptr;
        if (c.illConditionedEnvelope) {
          envOk = [&c, &P, e, k](float gg, float rr) {
            return c.illConditionedEnvelope(P, e, k, gg, rr);
          };
        }
        cmp.add(g[k], refOut[k], e, c.inDim, k, bd, batch, envOk);
      }
      if (lv) lv->add(e, c.inDim, g, c.outDim);
    }
  };
  // NaN/±Inf batch: fast-math downgrade (§2) — "no crash + readback classifiable" only.
  auto runSmoke = [&](const std::vector<float>& P, const std::vector<float>& in) {
    std::vector<float> out;
    const size_t n = in.size() / (size_t)c.inDim;
    if (!c.gpu(gpuParams(P), in, out) || out.size() != n * (size_t)c.outDim) {
      smokeOk = false;
      return;
    }
    ++smokeBatches;
  };

  // ── layer 1: special-value grid (params walk the specials, others sit mid-non-identity) ──
  std::vector<float> mids(c.params.size());
  for (size_t k = 0; k < c.params.size(); ++k) mids[k] = midNonIdentity(c.params[k]);
  for (size_t k = 0; k < c.params.size(); ++k) {
    for (float v : finiteSpecials(c.params[k])) {
      std::vector<float> P = mids;
      P[k] = v;
      runCompare(P, grid, "grid-param", nullptr);
    }
    for (float v : nonFiniteSpecials()) {
      std::vector<float> P = mids;
      P[k] = v;
      runSmoke(P, grid);
    }
  }
  // Input-element specials (finite → full compare; NaN/Inf → smoke), params at mid.
  const ParamDomain inDom{"input", c.inputLo, c.inputHi, ParamDomain::Linear,
                          "MathvCase.inputLo/Hi"};
  {
    std::vector<float> sp = grid;
    const std::vector<float> fs = finiteSpecials(inDom);
    for (size_t j = 0; j < c.gridElems; ++j)
      sp[j * (size_t)c.inDim + (j % (size_t)c.inDim)] = fs[j % fs.size()];
    runCompare(mids, sp, "grid-input", nullptr);
    std::vector<float> nfin = grid;
    const std::vector<float> nf = nonFiniteSpecials();
    for (size_t j = 0; j < c.gridElems; ++j)
      nfin[j * (size_t)c.inDim + (j % (size_t)c.inDim)] = nf[j % nf.size()];
    runSmoke(mids, nfin);
  }

  // ── layer 2: in-domain uniform random (+ the liveness probe rides this layer) ──
  for (size_t v = 0; v < c.randomVectors; ++v) {
    std::vector<float> P(c.params.size());
    for (size_t k = 0; k < c.params.size(); ++k) P[k] = sampleUniform(rng, c.params[k]);
    std::vector<float> in(c.randomElems * (size_t)c.inDim);
    for (float& x : in) x = rng.uniform(c.inputLo, c.inputHi);
    runCompare(P, in, "random", &live);
  }

  // ── layer 3: identity sentinels (output == INPUT under exact eps — params reach the kernel) ──
  Comparator idCmp((std::string(tag) + "-identity").c_str(), EpsSpec::exact(), c.maxEvidence);
  bool idShapeOk = true;
  for (const std::vector<float>& idP : c.identityParams) {
    if (idP.size() != c.params.size() || c.inDim != c.outDim) {
      idShapeOk = false;
      continue;
    }
    std::vector<float> out;
    if (!c.gpu(gpuParams(idP), grid, out) || out.size() != grid.size()) {
      dispatchOk = false;
      continue;
    }
    for (size_t i = 0; i < c.gridElems; ++i)
      for (int k = 0; k < c.inDim; ++k)
        idCmp.add(out[i * (size_t)c.inDim + k], grid[i * (size_t)c.inDim + k],
                  &grid[i * (size_t)c.inDim], c.inDim, k, -1.0f, "identity");
  }

  // ── verdict table (ParityReport — the existing organ) ──
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("nonfinite-smoke(no-crash)", smokeOk, (double)smokeBatches);
  rep.expectTrue("compare(3-layer corpus)", cmp.verdict(), (double)cmp.total());
  if (!c.identityParams.empty())
    rep.expectTrue("identity(sentinel==input)", idShapeOk && idCmp.verdict(),
                   (double)idCmp.total());
  rep.expectTrue("liveness(variance>0)", live.variance() > 0.0, live.variance());
  rep.expectTrue("liveness(nonIdentity>=floor)",
                 !live.dimsMatch || live.nonIdentityFrac() >= c.minLivenessFrac,
                 live.nonIdentityFrac());

  const bool pass = rep.pass();
  if (!c.quiet || !pass) {
    cmp.print();
    if (!c.identityParams.empty()) idCmp.print();
  }
  return rep.finish() == 0;
}

// Uniform exit-code mapping so every mathv tooth has --bite-correct polarity:
//   no-bug leg: pass -> 0 (GREEN) / fail -> 1 (RED).
//   -bug leg:   RED  -> 1 (the tooth BIT — --bite reads nonzero as bite);
//               still green -> "did not trip" -> 0, landing on the NO-BITE list (GOLDEN_STANDARD
//               rule 3 / lint P1 polarity: a dead tooth must be VISIBLE, not exit-1-invisible).
inline int mathvVerdictToExit(bool pass, bool injectBug, const char* opName) {
  if (!injectBug) return pass ? 0 : 1;
  if (pass) {
    std::printf("[mathv-%s] injectBug did not trip — tooth has no bite\n", opName);
    return 0;
  }
  std::printf("[mathv-%s] injectBug leg RED as expected (bite)\n", opName);
  return 1;
}

}  // namespace mathv
}  // namespace sw
