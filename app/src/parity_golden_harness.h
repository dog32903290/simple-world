// parity_golden_harness.h — reusable scaffold for "stateful/heavy node" PARITY goldens.
//
// WHY THIS EXISTS (PARITY_GATE_PLAN.md): stateful sim/render nodes (RadialPoints, TurbulenceForce,
// ParticleSystem, ...) were only smoke-tested ("moved > 0.1", "non-black") — threshold asserts that
// are STRUCTURALLY BLIND to multiplier/phase drift (Amount=15 and Amount=1 both pass a "moved" smoke).
// This harness factors out the shared shape every parity golden needs so Stage-3 fan-out adds a node by
// writing ONLY its fixed-scene cook + its closed-form TiXL assertions — not the boilerplate.
//
// THE SHAPE (3 reusable pieces):
//   1. ParityHarness  — RAII Metal device/queue/metallib bootstrap (every GPU golden needs this).
//   2. ParityReport   — a named-assertion accumulator. Each `expect(label, actual, expected, tol)`
//                       records expected-vs-actual; `pass()` is the AND of all checks. This is the
//                       RED-FIRST organ: a parity golden cooks with PRODUCTION DEFAULTS and asserts
//                       against TiXL CONSTANTS, so a default that diverges from TiXL flips a check RED.
//   3. injectBug      — every selftest takes `bool injectBug`; the bug LEG re-runs the same scene with
//                       a real degeneracy so the assertion FAILS, proving the check has teeth (the
//                       --bite refuter diffs the two legs). Goldens own their bug; the harness just
//                       threads the flag and prints the structured expected/actual table.
//
// ANCHOR DISCIPLINE (rule 2): expected values passed to expect() MUST come from TiXL source/formula
// (.cs/.hlsl/.t3 — cite the line in the call site), NEVER from an sw readback snapshot. The harness
// cannot enforce that mechanically; it makes the expected value a NAMED, cited argument so a snapshot
// stands out in review.
//
// ZONE: shell tier (app/src/ root, like movepointstosdf_golden.cpp). Header-only so each golden TU
// includes it without a CMake/library edit. Pure verify/test scaffolding — no production node code.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// RAII Metal bootstrap shared by every GPU parity golden. Owns device/queue/lib + the AutoreleasePool.
// `ok()` is false if the metallib failed to load (the golden prints FAIL and returns 1, like the
// existing goldens). Destruction releases everything in reverse order.
struct ParityHarness {
  NS::AutoreleasePool* pool = nullptr;
  MTL::Device* dev = nullptr;
  MTL::CommandQueue* queue = nullptr;
  MTL::Library* lib = nullptr;

  explicit ParityHarness() {
    pool = NS::AutoreleasePool::alloc()->init();
    dev = MTL::CreateSystemDefaultDevice();
    queue = dev ? dev->newCommandQueue() : nullptr;
    if (dev) {
      NS::Error* err = nullptr;
      lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
    }
  }
  ~ParityHarness() {
    if (lib) lib->release();
    if (queue) queue->release();
    if (dev) dev->release();
    if (pool) pool->release();
  }
  bool ok() const { return dev && queue && lib; }

  ParityHarness(const ParityHarness&) = delete;
  ParityHarness& operator=(const ParityHarness&) = delete;
};

// A named-assertion accumulator. Each check records actual vs (TiXL-cited) expected with a tolerance.
// pass() = AND of every check. print() emits a structured expected/actual table so a RED golden SHOWS
// the divergence number (e.g. "count expected=100 actual=2048 -> RED"), which is the evidence the
// PARITY_GATE plan asks every red-first proof to paste.
class ParityReport {
 public:
  explicit ParityReport(const char* tag) : tag_(tag) {}

  // Scalar near-equality (|actual-expected| <= tol).
  void expect(const std::string& label, double actual, double expected, double tol) {
    bool ok = std::fabs(actual - expected) <= tol;
    rows_.push_back({label, actual, expected, tol, ok});
    all_ = all_ && ok;
  }
  // Exact integer/count equality (tol 0; printed without a tolerance column noise).
  void expectExact(const std::string& label, double actual, double expected) {
    bool ok = (actual == expected);
    rows_.push_back({label, actual, expected, 0.0, ok});
    all_ = all_ && ok;
  }
  // A pre-computed boolean predicate (use when the natural form isn't a scalar compare). `detail`
  // is a number printed for context (e.g. a ratio or a dot product).
  void expectTrue(const std::string& label, bool ok, double detail) {
    rows_.push_back({label, detail, detail, 0.0, ok});
    all_ = all_ && ok;
  }

  bool pass() const { return all_; }

  void print() const {
    for (const Row& r : rows_) {
      if (r.tol > 0.0)
        printf("[%s]   %-28s actual=%.5f expected=%.5f tol=%.5f -> %s\n", tag_.c_str(),
               r.label.c_str(), r.actual, r.expected, r.tol, r.ok ? "ok" : "RED");
      else
        printf("[%s]   %-28s actual=%.5f expected=%.5f -> %s\n", tag_.c_str(), r.label.c_str(),
               r.actual, r.expected, r.ok ? "ok" : "RED");
    }
    printf("[%s] -> %s\n", tag_.c_str(), all_ ? "PASS" : "FAIL");
  }

  // Convenience: print + return the selftest exit code (0 pass / 1 fail).
  int finish() const {
    print();
    return all_ ? 0 : 1;
  }

 private:
  struct Row {
    std::string label;
    double actual, expected, tol;
    bool ok;
  };
  std::string tag_;
  std::vector<Row> rows_;
  bool all_ = true;
};

}  // namespace sw
