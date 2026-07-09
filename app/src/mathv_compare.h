#pragma once
// mathv_compare — the GPU-vs-CPU-ref comparator for the mathv (math-verify) fuzz harness
// (docs/agent/MATH_VERIFY_WORKFLOW.md §2). Header-only, shell tier (app/src/ root). Pure host math.
//
// THREE EPS CLASSES (every mathv op declares one; loosening a knob beyond the class default
// REQUIRES a same-line `// measured: ...` derivation note — golden_lint.sh --audit screens this;
// the calibration-note template is turbulence_parity_golden.cpp:227-233):
//   • Exact          (add/sub/mul/mod/select/swizzle/affine): |a−b| ≤ atol + rtol·max(|a|,|b|),
//                    atol=1e-6 rtol=1e-5, 100% of samples must pass.
//   • Transcendental (sin/exp/pow/noise/normalize/rsqrt — fast-math is ON, see §0): DOUBLE-LAYER
//                    fraction gate: ≥99% of samples inside the TIGHT gate (rtolTight=1e-3) AND
//                    100% inside the WIDE gate (10× tight). One fp-physics straggler cannot hide a
//                    formula error (a wrong formula decorrelates ~every sample), and one true fork
//                    cannot hide inside the 1% either — it must still fit the wide gate.
//   • Branchy        (floor/step/simplex-cell/threshold): a mismatch is EXEMPT only if the input
//                    sits closer than deltaBranch (~1e-4) to the nearest branch boundary (distance
//                    supplied by the TU's branchDist callback) AND total exemptions stay ≤1% —
//                    fp jitter flipping a cell at the knife edge is physics, a shifted threshold
//                    is a bug. CPU refs for this class are FLOAT not double (a double oracle can
//                    land on a DIFFERENT cell than the float GPU — tixl_noise_oracle.h:13-15).
//
// SPECIAL-VALUE SEMANTICS (uniform across all classes, §2): NaN vs NaN = MATCH (class, not
// payload); ±Inf must match in sign; +0 == −0; denormals are FTZ'd to the zero class on BOTH
// sides before comparing (the GPU flushes; the ref may not) — denormal INPUTS are still generated
// (mathv_input.h) to probe downstream divergence.
//
// EVIDENCE PACK: the first maxEvidence mismatches print input vector / both outputs / abs+rel
// error / distance-to-branch-boundary — plus the run seed printed by the harness — everything the
// failure-routing table (§7) needs to classify ref-bug vs MSL-bug vs eps-too-tight.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sw {
namespace mathv {

struct EpsSpec {
  enum class Cls { Exact, Transcendental, Branchy };
  Cls cls = Cls::Exact;
  // Exact / Branchy numeric gate (also the atol floor for Transcendental near zero).
  float atol = 1e-6f;
  float rtol = 1e-5f;
  // Transcendental double-layer fraction gate.
  float rtolTight = 1e-3f;
  float wideMul = 10.0f;
  float tightFrac = 0.99f;
  // Branchy exemption: mismatch forgiven iff branchDist < deltaBranch, total exempt ≤ exemptMax.
  float deltaBranch = 1e-4f;
  float exemptMax = 0.01f;

  static EpsSpec exact() { return EpsSpec{}; }
  static EpsSpec transcendental() {
    EpsSpec e;
    e.cls = Cls::Transcendental;
    return e;
  }
  static EpsSpec branchy() {
    EpsSpec e;
    e.cls = Cls::Branchy;
    return e;
  }
};

// fp classification with FTZ: denormals collapse into the zero class BEFORE classification.
enum class FpClass { Zero, Finite, PosInf, NegInf, NaN };
inline float ftz(float x) {
  return (x != 0.0f && std::fabs(x) < 1.17549435e-38f /*FLT_MIN*/) ? (x < 0.0f ? -0.0f : 0.0f) : x;
}
inline FpClass classify(float x) {
  if (std::isnan(x)) return FpClass::NaN;
  if (std::isinf(x)) return x > 0.0f ? FpClass::PosInf : FpClass::NegInf;
  if (x == 0.0f) return FpClass::Zero;  // ±0 both land here (== ignores the sign bit)
  return FpClass::Finite;
}

// One recorded mismatch (evidence pack row).
struct Evidence {
  std::vector<float> input;  // the element's input vector
  int lane;                  // which output scalar
  float gpu, ref;
  float absErr, relErr;
  float branchDist;  // <0 = unknown / not a branchy op
  const char* batch; // which sampling layer/batch produced it
};

// Accumulates scalar-lane comparisons across batches; verdict() applies the class gates.
class Comparator {
 public:
  Comparator(const char* tag, const EpsSpec& eps, int maxEvidence = 5)
      : tag_(tag), eps_(eps), maxEvidence_(maxEvidence) {}

  // Compare one output scalar lane. `in`/`inDim` = the element's input vector (evidence only).
  // `branchDist` = distance from the nearest branch boundary (<0 = unknown), from the TU callback.
  void add(float gpu, float ref, const float* in, int inDim, int lane, float branchDist,
           const char* batch) {
    ++total_;
    float g = ftz(gpu), r = ftz(ref);
    FpClass cg = classify(g), cr = classify(r);
    if (cg == FpClass::NaN || cr == FpClass::NaN) {
      if (cg == cr) return;  // NaN class match (payload not compared)
      recordMiss(g, r, in, inDim, lane, branchDist, batch);
      return;
    }
    if (cg == FpClass::PosInf || cg == FpClass::NegInf || cr == FpClass::PosInf ||
        cr == FpClass::NegInf) {
      if (cg == cr) return;  // same-signed infinity
      recordMiss(g, r, in, inDim, lane, branchDist, batch);
      return;
    }
    // finite (Zero class compares as 0.0 after FTZ — +0 == −0, denormal == 0)
    float err = std::fabs(g - r);
    float scale = std::fmax(std::fabs(g), std::fabs(r));
    switch (eps_.cls) {
      case EpsSpec::Cls::Exact:
        if (err <= eps_.atol + eps_.rtol * scale) return;
        recordMiss(g, r, in, inDim, lane, branchDist, batch);
        return;
      case EpsSpec::Cls::Transcendental: {
        float tight = eps_.atol + eps_.rtolTight * scale;
        if (err <= tight) {
          ++tightOk_;
          return;
        }
        ++beyondTight_;
        if (err <= eps_.wideMul * tight) return;  // inside wide → counted against tightFrac only
        recordMiss(g, r, in, inDim, lane, branchDist, batch);
        return;
      }
      case EpsSpec::Cls::Branchy:
        if (err <= eps_.atol + eps_.rtol * scale) return;
        if (branchDist >= 0.0f && branchDist < eps_.deltaBranch) {
          ++exempt_;  // knife-edge cell flip — physics, if rare (≤ exemptMax)
          return;
        }
        recordMiss(g, r, in, inDim, lane, branchDist, batch);
        return;
    }
  }

  uint64_t total() const { return total_; }
  uint64_t misses() const { return miss_; }
  uint64_t exempt() const { return exempt_; }

  bool verdict() const {
    if (total_ == 0) return false;  // an empty comparator proved nothing — hollow, not green
    if (miss_ > 0) return false;    // hard misses fail EVERY class (wide gate / non-exempt)
    if (eps_.cls == EpsSpec::Cls::Transcendental) {
      uint64_t denom = tightOk_ + beyondTight_;
      double frac = denom ? (double)tightOk_ / (double)denom : 1.0;
      return frac >= (double)eps_.tightFrac;
    }
    if (eps_.cls == EpsSpec::Cls::Branchy)
      return (double)exempt_ / (double)total_ <= (double)eps_.exemptMax;
    return true;  // Exact: miss_ == 0 is the whole gate (100%)
  }

  void print() const {
    printf("[%s] compared=%llu miss=%llu", tag_.c_str(), (unsigned long long)total_,
           (unsigned long long)miss_);
    if (eps_.cls == EpsSpec::Cls::Transcendental) {
      uint64_t denom = tightOk_ + beyondTight_;
      printf(" tightOk=%.5f (gate>=%.2f)", denom ? (double)tightOk_ / denom : 0.0,
             (double)eps_.tightFrac);
    }
    if (eps_.cls == EpsSpec::Cls::Branchy)
      printf(" exempt=%.5f (gate<=%.3f)", total_ ? (double)exempt_ / total_ : 0.0,
             (double)eps_.exemptMax);
    printf(" -> %s\n", verdict() ? "ok" : "RED");
    for (const Evidence& e : evidence_) {
      printf("[%s]   MISMATCH batch=%s lane=%d gpu=%.9g ref=%.9g absErr=%.3g relErr=%.3g "
             "branchDist=%.3g in=(",
             tag_.c_str(), e.batch, e.lane, e.gpu, e.ref, e.absErr, e.relErr, e.branchDist);
      for (size_t k = 0; k < e.input.size(); ++k)
        printf("%s%.9g", k ? "," : "", e.input[k]);
      printf(")\n");
    }
    if (miss_ > (uint64_t)evidence_.size())
      printf("[%s]   ... %llu more mismatch(es) not printed\n", tag_.c_str(),
             (unsigned long long)(miss_ - evidence_.size()));
  }

 private:
  void recordMiss(float g, float r, const float* in, int inDim, int lane, float branchDist,
                  const char* batch) {
    ++miss_;
    if ((int)evidence_.size() >= maxEvidence_) return;
    Evidence e;
    e.input.assign(in, in + inDim);
    e.lane = lane;
    e.gpu = g;
    e.ref = r;
    e.absErr = std::fabs(g - r);
    float scale = std::fmax(std::fabs(g), std::fabs(r));
    e.relErr = scale > 0.0f ? e.absErr / scale : 0.0f;
    e.branchDist = branchDist;
    e.batch = batch;
    evidence_.push_back(std::move(e));
  }

  std::string tag_;
  EpsSpec eps_;
  int maxEvidence_;
  uint64_t total_ = 0, miss_ = 0, tightOk_ = 0, beyondTight_ = 0, exempt_ = 0;
  std::vector<Evidence> evidence_;
};

}  // namespace mathv
}  // namespace sw
