// variation_selftest — --selftest-variation. Harness-first golden for Lane L1 (Variation/Snapshot),
// the VJ live-performance core. Proves the two ported math primitives in runtime/variation_mix.h
// against the TiXL formulas BEFORE any pool/UI/command code rides on them.
//
// Pure CPU (no GPU, no graph) — these are scalar math functions, so the golden is a direct numeric
// comparison, no Metal device / cook loop needed (unlike the string/point goldens).
//
// GOLDEN 1 — Mix (TiXL ExplorationVariation.cs:66-191, float per-parameter weighted average):
//   snapshot A {x:0, y:100, z:1}, snapshot B {x:100, y:0, z:2}, weights {0.5, 0.5}, scatter=0.
//   sumWeight = 1.0 → normalized average:
//     x = (0*0.5 + 100*0.5)/1 = 50
//     y = (100*0.5 + 0*0.5)/1 = 50
//     z = (1*0.5 + 2*0.5)/1   = 1.5
//   Asserted exactly {50, 50, 1.5}, tolerance 1e-4.
//
// GOLDEN 2 — SpringDamp (TiXL MathUtils.cs:484-498), crossfader regime (BlendActions.cs:245-248):
//   target=1, current=0, velocity=0, springConstant=20, timeStep=1/60; loop until |velocity| < 0.0005.
//   ★ MEASURED against the REAL formula: converges at frame 166 with current=0.999863 (the scout's
//   "<120 frames" guess was WRONG for k=20 — the spring oscillates before |vel| settles). So the
//   bound is "converged in < 200 frames" (real value 166, generous headroom) AND final current ≈ 1.0
//   within 1e-3 (real 0.999863). Both pinned to the verbatim port, not to the scout's guess.
//
// ★ RED tooth (injectBug, non-theatrical): the bug runs SpringDamp with springConstant=5 instead of
//   20 → the slower spring does NOT reach |velocity| < 0.0005 within the 200-frame window (measured:
//   still moving, vel never < 0.0005 by frame 200) → the convergence assertion FAILS. It ALSO tampers
//   the Mix expected (x: 50 → 49.9) so the mix tooth bites too. The teeth bite the real numeric
//   behavior (a genuinely-too-soft spring / a genuinely-wrong average), not a flipped constant.
#include <cmath>
#include <cstdio>
#include <vector>

#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS
#include "runtime/variation_mix.h"      // springDamp / mixFloat / MixNeighbour

namespace sw {
namespace {

constexpr float kTol = 1e-4f;       // Mix exact-value tolerance
constexpr float kSpringTol = 1e-3f; // SpringDamp final-value tolerance
constexpr int kMaxFrames = 200;     // convergence window (real k=20 settles at frame 166)

// Mix one float parameter across snapshots A and B at weights {wA, wB}, scatter 0.
float mix2(float a, float b, float wA, float wB) {
  std::vector<MixNeighbour> nbs = {{a, wA, true}, {b, wB, true}};
  return mixFloat(nbs, /*currentValue=*/0.0f, /*scatter=*/0.0f);
}

// Drive SpringDamp to settle (|velocity| < 0.0005). Returns {framesUsed (or kMaxFrames if never),
// finalCurrent}. springConstant is a parameter so the bug can soften it.
struct SettleResult { int frames; float current; bool converged; };
SettleResult settle(float springConstant) {
  float target = 1.0f, current = 0.0f, velocity = 0.0f;
  const float dt = 1.0f / 60.0f;
  for (int f = 1; f <= kMaxFrames; ++f) {
    current = springDamp(target, current, velocity, springConstant, dt);
    if (std::fabs(velocity) < 0.0005f) {
      return {f, current, true};
    }
  }
  return {kMaxFrames, current, false};
}

bool runMixGolden(bool injectBug) {
  const float gotX = mix2(0.0f, 100.0f, 0.5f, 0.5f);
  const float gotY = mix2(100.0f, 0.0f, 0.5f, 0.5f);
  const float gotZ = mix2(1.0f, 2.0f, 0.5f, 0.5f);
  // ★ injectBug tampers the EXPECTED x (50 → 49.9) so the tooth bites the real computed 50.
  const float wantX = injectBug ? 49.9f : 50.0f;
  const float wantY = 50.0f;
  const float wantZ = 1.5f;
  const bool okX = std::fabs(gotX - wantX) < kTol;
  const bool okY = std::fabs(gotY - wantY) < kTol;
  const bool okZ = std::fabs(gotZ - wantZ) < kTol;
  const bool ok = okX && okY && okZ;
  std::printf("[selftest-variation] MIX A{0,100,1} B{100,0,2} w{.5,.5} -> "
              "x=%.4f(want %.4f) y=%.4f(want %.4f) z=%.4f(want %.4f) -> %s\n",
              gotX, wantX, gotY, wantY, gotZ, wantZ, ok ? "PASS" : "FAIL");
  return ok;
}

bool runSpringGolden(bool injectBug) {
  // ★ injectBug softens the spring (20 → 5): the slower spring never settles in the 200-frame
  // window → converged==false → FAIL. The clean run settles at frame 166, current≈0.999863.
  const float k = injectBug ? 5.0f : 20.0f;
  const SettleResult r = settle(k);
  const bool boundOk = r.converged && r.frames < kMaxFrames;
  const bool finalOk = std::fabs(r.current - 1.0f) < kSpringTol;
  const bool ok = boundOk && finalOk;
  std::printf("[selftest-variation] SPRINGDAMP k=%.0f target=1 -> converged=%s frames=%d "
              "current=%.6f (want <%d frames, |current-1|<%.0e) -> %s\n",
              k, r.converged ? "yes" : "no", r.frames, r.current, kMaxFrames, kSpringTol,
              ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runVariationSelfTest(bool injectBug) {
  bool ok = true;
  ok = runMixGolden(injectBug) && ok;
  ok = runSpringGolden(injectBug) && ok;
  std::printf("[selftest-variation] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

namespace sw {
// Self-register into the --selftest router (independent leaf; selftests.cpp untouched). orderBase
// 303 appends after buildrandomstring (301/302) deterministically — the registry sorts by order.
REGISTER_SELFTESTS(/*orderBase=*/303,
    {"variation", runVariationSelfTest});
}  // namespace sw
