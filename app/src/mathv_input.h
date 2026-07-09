#pragma once
// mathv_input — deterministic input generation for the mathv (math-verify) fuzz harness
// (docs/agent/MATH_VERIFY_WORKFLOW.md §3). Header-only, shell tier (app/src/ root, same home as
// parity_golden_harness.h). Pure host-side sampling — no Metal / runtime / platform dependency.
//
// THE THREE LAYERS the harness (mathv_harness.h) draws from here — anti-hollow discipline per
// [[replay-golden-pins-must-sample-diverging-middle]] (pins at endpoints/identity are structurally
// blind to a wrong op formula):
//   1. SPECIAL-VALUE GRID — each param scalar takes {0, ±1e-4, ±0.5, ±1, ±domain-edge, ±1e6,
//      denormal, NaN, ±Inf} in turn while every OTHER param sits at a NON-IDENTITY mid value
//      (midNonIdentity below — never 0, never 1, so the rest of the formula stays live).
//   2. IN-DOMAIN UNIFORM RANDOM — ranges come from the TU's ParamDomain table. The lo/hi constants
//      are BAKED at authoring time from external/tixl .t3ui Min/Max (cited per row in `cite`);
//      the binary never reads external/ at runtime. Enum/int domains are swept exhaustively in the
//      grid layer and sampled as integers here.
//   3. IDENTITY SENTINELS — the TU supplies ≥1 known-identity param vector (Amount=0 / Scale=1);
//      expected output == input, proving the params ACTUALLY REACH the kernel (a kernel that
//      ignores its param buffer passes any self-consistent compare; it cannot pass this).
// Plus the MID-SEGMENT LIVENESS self-check (measureLiveness): on the random layer the GPU output
// must have variance > 0 and ≥90% non-identity samples, or the run is RED — the mechanical guard
// against a corpus sitting on a fixed point / saturation plateau.
//
// DETERMINISM: seed = splitmix64(fnv1a(opName)) — printed every run so any red is replayable;
// SW_MATHV_SEED env var overrides (the refuter's change-the-seed attack). Same seed → identical corpus.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace sw {
namespace mathv {

// One scalar parameter's fuzz domain. `cite` is REQUIRED provenance: where lo/hi came from
// (external/tixl .t3ui Min/Max path+field, .t3 DefaultValue neighborhood, or HLSL-implied range).
struct ParamDomain {
  const char* name;
  float lo;
  float hi;
  enum Kind { Linear, Log, Enum, Int } kind;
  const char* cite;  // e.g. "external/tixl .../WrapPointPosition.t3ui Min/Max" — baked, not runtime-read
};

// ── deterministic seed ──────────────────────────────────────────────────────────────────────────
inline uint64_t fnv1a64(const char* s) {
  uint64_t h = 1469598103934665603ull;
  for (; s && *s; ++s) h = (h ^ (uint8_t)*s) * 1099511628211ull;
  return h;
}
inline uint64_t splitmix64(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
// seed = splitmix64(fnv1a(opName)); SW_MATHV_SEED overrides. Caller (harness) prints it.
inline uint64_t mathvSeed(const char* opName) {
  if (const char* e = std::getenv("SW_MATHV_SEED")) {
    if (*e) return std::strtoull(e, nullptr, 0);
  }
  uint64_t h = fnv1a64(opName);
  return splitmix64(h);
}

// Deterministic PRNG (splitmix64 stream). float in [0,1) uses the top 24 bits → exact float grid.
struct Rng {
  uint64_t state;
  explicit Rng(uint64_t seed) : state(seed) {}
  uint64_t next() { return splitmix64(state); }
  float uniform01() { return (float)(next() >> 40) * (1.0f / 16777216.0f); }
  float uniform(float lo, float hi) { return lo + (hi - lo) * uniform01(); }
};

// ── special-value grid ──────────────────────────────────────────────────────────────────────────
// FINITE specials (full numeric compare): zero, tiny, mid, unit, the domain edges, huge, denormal.
// For Enum/Int domains the "specials" are instead EVERY integer value in [lo,hi] (§3 enum 逐值全掃),
// capped at 64 to bound runtime.
inline std::vector<float> finiteSpecials(const ParamDomain& d) {
  if (d.kind == ParamDomain::Enum || d.kind == ParamDomain::Int) {
    std::vector<float> v;
    float lo = std::ceil(d.lo), hi = std::floor(d.hi);
    for (float x = lo; x <= hi && v.size() < 64; x += 1.0f) v.push_back(x);
    return v;
  }
  return {0.0f,  1e-4f,  -1e-4f, 0.5f,   -0.5f,  1.0f,   -1.0f,
          d.lo,  d.hi,   1e6f,   -1e6f,  1e-40f, -1e-40f};
}
// NON-FINITE specials (NaN / ±Inf): under fast-math (xcrun metal, no -fno-fast-math — see
// MATH_VERIFY_WORKFLOW §0/§2) these probes are DOWNGRADED to "no crash + output classifiable";
// no numeric compare (unless an op's HLSL handles them explicitly — that op adds its own tooth).
inline std::vector<float> nonFiniteSpecials() {
  return {std::nanf(""), HUGE_VALF, -HUGE_VALF};
}

// A NON-IDENTITY mid-segment value for "the other params" while one param walks the grid: never 0,
// never 1, off the endpoints — keeps the rest of the formula live (P2 identity-point anti-pattern).
inline float midNonIdentity(const ParamDomain& d) {
  if (d.kind == ParamDomain::Enum || d.kind == ParamDomain::Int) {
    float m = std::floor(d.lo + 0.5f * (d.hi - d.lo));
    return m;  // enums have no identity convention; mid slot is fine
  }
  float m = d.lo + 0.37f * (d.hi - d.lo);
  if (std::fabs(m) < 1e-3f || std::fabs(m - 1.0f) < 1e-3f) m = d.lo + 0.61f * (d.hi - d.lo);
  if (std::fabs(m) < 1e-3f || std::fabs(m - 1.0f) < 1e-3f) m += 0.13f * (d.hi - d.lo) + 1e-2f;
  return m;
}

// In-domain uniform sample honoring the domain kind (log = geometric when lo>0, else linear;
// int/enum = integer-valued).
inline float sampleUniform(Rng& rng, const ParamDomain& d) {
  switch (d.kind) {
    case ParamDomain::Log:
      if (d.lo > 0.0f && d.hi > d.lo)
        return std::exp(rng.uniform(std::log(d.lo), std::log(d.hi)));
      return rng.uniform(d.lo, d.hi);  // non-positive lo: log scale undefined → linear fallback
    case ParamDomain::Enum:
    case ParamDomain::Int: {
      float x = std::floor(rng.uniform(d.lo, d.hi + 1.0f));
      return x > d.hi ? d.hi : x;
    }
    case ParamDomain::Linear:
    default:
      return rng.uniform(d.lo, d.hi);
  }
}

// ── mid-segment liveness self-check (§3 機械防空心) ─────────────────────────────────────────────
// On the RANDOM layer: (a) the GPU output must vary across elements (variance > 0 — not a constant /
// saturated plateau), and (b) when inDim==outDim, ≥90% of elements must differ from their input
// (the corpus is not parked on an identity fixed point). Non-finite lanes are skipped.
struct Liveness {
  double sum = 0.0, sumSq = 0.0;
  uint64_t nOut = 0;          // finite output scalars accumulated
  uint64_t elems = 0;         // elements checked for identity
  uint64_t nonIdentity = 0;   // elements where any lane differs from input beyond atol
  bool dimsMatch = true;

  void add(const float* in, int inDim, const float* out, int outDim, float atol = 1e-6f) {
    dimsMatch = dimsMatch && (inDim == outDim);
    for (int k = 0; k < outDim; ++k) {
      if (!std::isfinite(out[k])) continue;
      sum += out[k];
      sumSq += (double)out[k] * out[k];
      ++nOut;
    }
    ++elems;
    if (inDim == outDim) {
      for (int k = 0; k < outDim; ++k) {
        if (std::isfinite(out[k]) && std::isfinite(in[k]) && std::fabs(out[k] - in[k]) > atol) {
          ++nonIdentity;
          break;
        }
      }
    } else {
      ++nonIdentity;  // dims differ → identity is not a representable failure mode here
    }
  }
  double variance() const {
    if (nOut < 2) return 0.0;
    double mean = sum / (double)nOut;
    double v = sumSq / (double)nOut - mean * mean;
    return v > 0.0 ? v : 0.0;
  }
  double nonIdentityFrac() const { return elems ? (double)nonIdentity / (double)elems : 0.0; }
  bool pass(double minFrac = 0.90) const {
    return variance() > 0.0 && (!dimsMatch || nonIdentityFrac() >= minFrac);
  }
};

}  // namespace mathv
}  // namespace sw
