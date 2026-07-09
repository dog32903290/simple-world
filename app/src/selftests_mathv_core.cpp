// app/src/selftests_mathv_core.cpp — --selftest-mathv-core: the mathv COMPARATOR'S OWN teeth
// (MATH_VERIFY_WORKFLOW.md 工單0 交付物 4). Before any op trusts the mathv harness, this tooth
// proves the judge itself judges correctly, using SYNTHETIC function pairs (pure CPU on both the
// "gpu adapter" and the ref side — no Metal in the loop, so a red here is always the comparator's
// fault, never a driver/kernel ambiguity):
//   • three EpsSpec classes, polarity BOTH ways — an identical pair must pass, a deliberately
//     perturbed pair (y=2x+1 vs y=2x+1+微差) must fail, per class gate (exact 100% / transcendental
//     double fraction gate / branchy boundary exemption + ≤1% rate);
//   • special-value semantics — NaN class match, ±Inf sign, +0==−0, denormal FTZ;
//   • mid-segment liveness self-check — a constant op and an identity op must both go RED;
//   • identity sentinel — an op that ignores its params must go RED at the declared identity;
//   • injectBug leg — the harness's params[0]+=1e-2 GPU-side perturbation must flip a HEALTHY
//     pair RED (measured in the no-bug leg as a meta-check, and it IS this tooth's own -bug leg).
// Shell-tier manifest leaf; self-registers into selftestRegistry() (zero CMake edits — the
// selftests_*.cpp glob picks this file up).
#include "mathv_harness.h"
#include "runtime/selftest_registry.h"

#include <cmath>
#include <cstdio>

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::MathvCase;
using mathv::ParamDomain;

// ── synthetic op 1 (Exact class): y = sign(mode) * (a*x + b) ────────────────────────────────────
float affineEval(const std::vector<float>& P, float x) {
  float s = P[2] < 2.0f ? 1.0f : -1.0f;  // enum lane: modes 0/1 → +, 2/3 → −
  return s * (P[0] * x + P[1]);
}
MathvCase affineCase(const char* name) {
  MathvCase c;
  c.opName = name;
  c.params = {
      {"a", 0.25f, 4.0f, ParamDomain::Linear, "synthetic comparator self-test (no TiXL op)"},
      {"b", -2.0f, 2.0f, ParamDomain::Linear, "synthetic"},
      {"mode", 0.0f, 3.0f, ParamDomain::Enum, "synthetic (exercises enum full-sweep)"},
  };
  c.inDim = c.outDim = 1;
  c.eps = EpsSpec::exact();
  c.identityParams = {{1.0f, 0.0f, 0.0f}};  // a=1 b=0 mode=0 → y == x
  c.quiet = true;
  c.maxEvidence = 2;
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    out[0] = affineEval(P, in[0]);
  };
  c.gpu = [](const std::vector<float>& P, const std::vector<float>& in, std::vector<float>& out) {
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = affineEval(P, in[i]);
    return true;
  };
  return c;
}

// ── synthetic op 2 (Transcendental class): y = sin(a*x) with controllable jitter ────────────────
// jitterEvery-th element gets ×(1+jitterMul) (the beyond-tight fraction knob); every other element
// gets ×(1+2e-4) (inside the tight gate); wideKill puts element 0 of each batch ×1.5 (beyond wide).
MathvCase sinCase(const char* name, int jitterEvery, float jitterMul, bool wideKill) {
  MathvCase c;
  c.opName = name;
  c.params = {{"a", 0.5f, 3.0f, ParamDomain::Linear, "synthetic comparator self-test"}};
  c.inDim = c.outDim = 1;
  c.eps = EpsSpec::transcendental();
  c.noIdentityReason = "synthetic transcendental probe: y=sin(a*x) has no identity param vector";
  c.quiet = true;
  c.maxEvidence = 2;
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    out[0] = std::sin(P[0] * in[0]);
  };
  c.gpu = [jitterEvery, jitterMul, wideKill](const std::vector<float>& P,
                                             const std::vector<float>& in,
                                             std::vector<float>& out) {
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
      float y = std::sin(P[0] * in[i]);
      if (wideKill && i == 0)
        y *= 1.5f;  // beyond the 10× wide gate → hard miss
      else if (jitterEvery > 0 && (i % (size_t)jitterEvery) == 0)
        y *= (1.0f + jitterMul);  // beyond tight (1e-3), inside wide (1e-2)
      else
        y *= (1.0f + 2e-4f);  // inside the tight gate — fp-physics-sized jitter
      out[i] = y;
    }
    return true;
  };
  return c;
}

// ── synthetic op 3 (Branchy class): y = floor(a*x + shift) vs ref floor(a*x) ────────────────────
// shift=3e-5 forks ONLY within deltaBranch of a cell boundary (exempt, rare) → must PASS;
// shift=0.25 shifts the threshold itself (forks ~25% of the domain, far from boundaries) → RED.
MathvCase floorCase(const char* name, float shift) {
  MathvCase c;
  c.opName = name;
  c.params = {{"a", 0.5f, 2.0f, ParamDomain::Linear, "synthetic comparator self-test"}};
  c.inDim = c.outDim = 1;
  c.eps = EpsSpec::branchy();
  c.noIdentityReason = "synthetic branchy probe: y=floor(a*x) has no identity param vector";
  c.quiet = true;
  c.maxEvidence = 2;
  c.ref = [](const std::vector<float>& P, const float* in, float* out) {
    out[0] = std::floor(P[0] * in[0]);
  };
  c.gpu = [shift](const std::vector<float>& P, const std::vector<float>& in,
                  std::vector<float>& out) {
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = std::floor(P[0] * in[i] + shift);
    return true;
  };
  c.branchDist = [](const std::vector<float>& P, const float* in, int) {
    float z = P[0] * in[0];
    return std::fabs(z - std::round(z));  // distance to the nearest floor() cell boundary
  };
  return c;
}

// ── synthetic op 4 (MathvCase.minLivenessFrac probe): y=x when x>=threshold (identity), y=x+5
// otherwise (non-identity). gpu and ref run the IDENTICAL x-only formula (no index/param
// dependence), so the compare gate always passes trivially — only nonIdentityFrac moves, letting
// this isolate the liveness-floor override from every other gate. threshold=0 splits the
// inputLo/Hi=[-4,4] domain ~50/50 (nonIdentityFrac≈0.5); threshold=-3.2 pins the bottom ~10% of
// the domain to non-identity (nonIdentityFrac≈0.1) — both deterministic from the fixed per-op seed.
MathvCase thresholdIdentityCase(const char* name, float threshold) {
  MathvCase c;
  c.opName = name;
  c.params = {{"unused", 0.0f, 1.0f, ParamDomain::Linear, "synthetic (probe ignores params)"}};
  c.inDim = c.outDim = 1;
  c.eps = EpsSpec::exact();
  c.noIdentityReason = "synthetic liveness-floor probe: deliberately mixed identity/non-identity op";
  c.quiet = true;
  c.maxEvidence = 1;
  auto fn = [threshold](const float* in, float* out) {
    out[0] = (in[0] >= threshold) ? in[0] : (in[0] + 5.0f);
  };
  c.ref = [fn](const std::vector<float>&, const float* in, float* out) { fn(in, out); };
  c.gpu = [fn](const std::vector<float>&, const std::vector<float>& in, std::vector<float>& out) {
    out.resize(in.size());
    for (size_t i = 0; i < in.size(); ++i) fn(&in[i], &out[i]);
    return true;
  };
  return c;
}

// ── direct Comparator pokes (single scalars / synthetic populations) ────────────────────────────
bool pokeScalar(const EpsSpec& eps, float g, float r, float dist = -1.0f) {
  Comparator c("mathv-core-poke", eps, /*maxEvidence=*/0);
  const float in = 0.0f;
  c.add(g, r, &in, 1, 0, dist, "poke");
  return c.verdict();
}
bool pokeExemptRate(int matches, int exempts) {
  Comparator c("mathv-core-poke", EpsSpec::branchy(), 0);
  const float in = 0.0f;
  for (int i = 0; i < matches; ++i) c.add(1.0f, 1.0f, &in, 1, 0, -1.0f, "poke");
  for (int i = 0; i < exempts; ++i) c.add(0.0f, 1.0f, &in, 1, 0, 1e-5f, "poke");
  return c.verdict();
}
bool pokeTightFrac(int tight, int beyondTight) {
  Comparator c("mathv-core-poke", EpsSpec::transcendental(), 0);
  const float in = 0.0f;
  for (int i = 0; i < tight; ++i) c.add(1.0f, 1.0f, &in, 1, 0, -1.0f, "poke");
  for (int i = 0; i < beyondTight; ++i) c.add(1.004f, 1.0f, &in, 1, 0, -1.0f, "poke");
  return c.verdict();
}
// Transcendental ill-conditioned-lookup exemption (mathv_compare.h §A criteria) — polarity BOTH
// ways. `tightOk` healthy samples pad the tightFrac gate; each `candidate` sample is a HARD miss
// (gpu=100 vs ref=1, beyond the wide gate) with branchDist>=0 (criterion-1 flag) and `envelopeOk`
// controlling criterion 3. exemptMax stays at its class default (0.01) throughout — no override.
bool pokeIllConditionedExempt(int tightOk, int candidates, bool envelopeOk) {
  Comparator c("mathv-core-poke", EpsSpec::transcendental(), 0);
  const float in = 0.0f;
  for (int i = 0; i < tightOk; ++i) c.add(1.0f, 1.0f, &in, 1, 0, -1.0f, "poke");
  for (int i = 0; i < candidates; ++i)
    c.add(100.0f, 1.0f, &in, 1, 0, /*branchDist=*/0.0f, "poke",
          [envelopeOk](float, float) { return envelopeOk; });
  return c.verdict();
}

}  // namespace

int runMathvCoreSelfTest(bool injectBug) {
  if (injectBug) {
    // -bug leg = the harness's OWN §1.5 mechanism on a HEALTHY pair: gpu-side params[0]+=1e-2
    // while the ref keeps the original → the compare must flip RED (bite). Polarity is uniform via
    // mathvVerdictToExit (RED→1=bite; still-green→0 = NO-BITE list visibility).
    MathvCase c = affineCase("core-bug-leg");
    c.maxEvidence = 1;
    bool pass = mathv::runMathvFuzz(c, /*injectBug=*/true);
    return mathv::mathvVerdictToExit(pass, true, "core");
  }

  ParityReport rep("selftest-mathv-core");
  const float kNaN = std::nanf("");
  const float kInf = HUGE_VALF;

  // Class polarity, GREEN side: an identical synthetic pair must pass each class's gates.
  rep.expectTrue("exact-identical(passes)", mathv::runMathvFuzz(affineCase("core-exact-green"), false), 1.0);
  rep.expectTrue("trans-fp-jitter(passes)",
                 mathv::runMathvFuzz(sinCase("core-trans-green", 512, 4e-3f, false), false), 1.0);
  rep.expectTrue("branchy-knife-edge(passes)",
                 mathv::runMathvFuzz(floorCase("core-branchy-green", 3e-5f), false), 1.0);

  // Class polarity, RED side: the deliberately perturbed twin must FAIL (y=2x+1 vs y=2x+1+微差).
  {
    MathvCase c = affineCase("core-exact-red");
    c.gpu = [](const std::vector<float>& P, const std::vector<float>& in,
               std::vector<float>& out) {
      out.resize(in.size());
      for (size_t i = 0; i < in.size(); ++i) out[i] = affineEval(P, in[i]) + 1e-3f;  // 微差版
      return true;
    };
    rep.expectTrue("exact-microdiff(fails)", !mathv::runMathvFuzz(c, false), 1.0);
  }
  rep.expectTrue("trans-frac-3pct(fails)",
                 !mathv::runMathvFuzz(sinCase("core-trans-red-frac", 32, 4e-3f, false), false), 1.0);
  rep.expectTrue("trans-beyond-wide(fails)",
                 !mathv::runMathvFuzz(sinCase("core-trans-red-wide", 512, 4e-3f, true), false), 1.0);
  rep.expectTrue("branchy-shifted-threshold(fails)",
                 !mathv::runMathvFuzz(floorCase("core-branchy-red", 0.25f), false), 1.0);

  // Gate arithmetic pokes: transcendental ≥99% tight boundary; branchy ≤1% exemption boundary.
  rep.expectTrue("trans-gate(100/101 passes)", pokeTightFrac(100, 1), 1.0);
  rep.expectTrue("trans-gate(100/102 fails)", !pokeTightFrac(100, 2), 1.0);
  rep.expectTrue("branchy-rate(0.5% exempt passes)", pokeExemptRate(209, 1), 1.0);
  rep.expectTrue("branchy-rate(4.8% exempt fails)", !pokeExemptRate(200, 10), 1.0);
  rep.expectTrue("branchy-far-mismatch(fails)",
                 !pokeScalar(EpsSpec::branchy(), 0.0f, 1.0f, /*dist=*/0.5f), 1.0);

  // Transcendental ill-conditioned-lookup exemption (mathv_compare.h §A) — polarity BOTH ways.
  rep.expectTrue("trans-illcond-exempt(0.5%, envelope-ok, passes)",
                 pokeIllConditionedExempt(199, 1, /*envelopeOk=*/true), 1.0);
  rep.expectTrue("trans-illcond-exempt(5.5% over-cap, fails)",
                 !pokeIllConditionedExempt(189, 11, /*envelopeOk=*/true), 1.0);
  rep.expectTrue("trans-illcond-exempt(envelope-fails, fails)",
                 !pokeIllConditionedExempt(199, 1, /*envelopeOk=*/false), 1.0);

  // Special-value semantics (§2, uniform across classes — poked on the Exact comparator).
  rep.expectTrue("nan-vs-nan(match)", pokeScalar(EpsSpec::exact(), kNaN, kNaN), 1.0);
  rep.expectTrue("nan-vs-finite(mismatch)", !pokeScalar(EpsSpec::exact(), kNaN, 1.0f), 1.0);
  rep.expectTrue("inf-same-sign(match)", pokeScalar(EpsSpec::exact(), kInf, kInf), 1.0);
  rep.expectTrue("inf-opposite-sign(mismatch)", !pokeScalar(EpsSpec::exact(), kInf, -kInf), 1.0);
  rep.expectTrue("inf-vs-finite(mismatch)", !pokeScalar(EpsSpec::exact(), kInf, 1.0f), 1.0);
  rep.expectTrue("pos0-vs-neg0(match)", pokeScalar(EpsSpec::exact(), 0.0f, -0.0f), 1.0);
  rep.expectTrue("denormal-ftz-vs-zero(match)", pokeScalar(EpsSpec::exact(), 1e-40f, 0.0f), 1.0);
  rep.expectTrue("denormal-vs-normal(mismatch)", !pokeScalar(EpsSpec::exact(), 1e-40f, 1e-3f), 1.0);

  // Mid-segment liveness self-check (§3): constant output (variance 0) and identity output
  // (non-identity < 90%) must BOTH go red even though gpu==ref compares clean.
  {
    MathvCase c = affineCase("core-liveness-const");
    c.identityParams.clear();
    c.noIdentityReason = "synthetic liveness probe (const op)";
    auto constFn = [](const std::vector<float>&, const float*, float* out) { out[0] = 3.0f; };
    c.ref = constFn;
    c.gpu = [](const std::vector<float>&, const std::vector<float>& in, std::vector<float>& out) {
      out.assign(in.size(), 3.0f);
      return true;
    };
    rep.expectTrue("liveness-const-op(fails)", !mathv::runMathvFuzz(c, false), 1.0);
  }
  {
    MathvCase c = affineCase("core-liveness-identity");
    c.identityParams.clear();
    c.noIdentityReason = "synthetic liveness probe (identity op)";
    c.ref = [](const std::vector<float>&, const float* in, float* out) { out[0] = in[0]; };
    c.gpu = [](const std::vector<float>&, const std::vector<float>& in, std::vector<float>& out) {
      out = in;
      return true;
    };
    rep.expectTrue("liveness-identity-op(fails)", !mathv::runMathvFuzz(c, false), 1.0);
  }

  // MathvCase.minLivenessFrac override (Part C, XS verdict 2026-07-10): a per-op floor lower than
  // the 0.90 default must (a) PASS an op whose actual nonIdentityFrac sits below 0.90 but at/above
  // the custom floor, and (b) still FAIL the same custom floor when the actual fraction undercuts
  // IT too — proving the override changes the gate's threshold, not just disables it.
  {
    MathvCase c = thresholdIdentityCase("core-liveness-customfloor-pass", 0.0f);  // ~0.5 actual
    c.minLivenessFrac = 0.30f;
    rep.expectTrue("liveness-customfloor(0.5>=0.30 passes)", mathv::runMathvFuzz(c, false), 1.0);
  }
  {
    MathvCase c = thresholdIdentityCase("core-liveness-customfloor-fail", -3.2f);  // ~0.1 actual
    c.minLivenessFrac = 0.30f;
    rep.expectTrue("liveness-customfloor(0.1<0.30 fails)", !mathv::runMathvFuzz(c, false), 1.0);
  }

  // Identity sentinel (§3 layer 3): an op whose kernel IGNORES its params compares clean against a
  // ref with the same bake, but must go RED at the declared identity (params never reached it).
  {
    MathvCase c = affineCase("core-sentinel-dead-params");
    c.ref = [](const std::vector<float>&, const float* in, float* out) { out[0] = in[0] + 1.0f; };
    c.gpu = [](const std::vector<float>&, const std::vector<float>& in, std::vector<float>& out) {
      out.resize(in.size());
      for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] + 1.0f;
      return true;
    };
    rep.expectTrue("identity-sentinel(fails)", !mathv::runMathvFuzz(c, false), 1.0);
  }

  // injectBug meta-check: the §1.5 gpu-side perturbation must flip a HEALTHY pair RED. (The same
  // path is this tooth's real -bug leg above — this row measures it inside the green leg too.)
  rep.expectTrue("injectBug-flips-healthy(red)",
                 !mathv::runMathvFuzz(affineCase("core-injectbug-meta"), true), 1.0);

  return rep.finish();
}

// High orderBase → appends at the end of --selftest-list (max in use 960..962; registry sorts).
REGISTER_SELFTESTS(/*orderBase=*/1000, {"mathv-core", runMathvCoreSelfTest});

}  // namespace sw
