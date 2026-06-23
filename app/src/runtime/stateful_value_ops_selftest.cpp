// runtime/stateful_value_ops_selftest — the embedded golden for the stateful value ops
// (--selftest-statefulvalue). Drives Damp/Spring/Ease/... frame-by-frame against hand-computed TiXL
// trajectories via the PUBLIC cook API (cookStatefulValueOp); injectBug corrupts an expected step so
// the live assertions FAIL (the --bite tooth). Extracted VERBATIM from the old stateful_value_ops.cpp
// monolith (debt sprint, zero behavior change).
//
// GRANDFATHERED >400 (test file, allowed per the split directive): a golden's frame-by-frame fixtures
// do not subdivide cleanly by subfamily without duplicating per-op setup, so it stays one file. The
// op LEAVES are all <400; this is the single allowed exception.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"  // cookStatefulValueOp + runStatefulValueSelfTest decl

namespace sw {
int runTransportTime2SelfTest(bool injectBug);  // helper TU: ClipTime/LastFrameDuration/GetBpm golden
// --- Isolated proof (frame-driven; hand-computed TiXL trajectories) ---
int runStatefulValueSelfTest(bool injectBug) {
  bool ok = true;
  const float eps = 1e-4f;
  const float dt60 = 1.0f / 60.0f;

  // ----- Damp, LinearInterpolation (Method 0), Damping=0.8 -----
  // First cook seeds dampedValue=Value (here 0). Then drive Value=1:
  //   d_{n+1} = Lerp(1, d_n, 0.8) = 1 + (d_n - 1)*0.8.
  //   d1 = 0.2, d2 = 0.36, d3 = 0.488.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("Damp", {{"Value", 0.0f}, {"Damping", 0.8f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
    bool pass0 = std::fabs(out[0] - 0.0f) < eps;  // init seeded to the input (0)
    ok = ok && pass0;
    printf("[selftest-statefulvalue] Damp.lin init=%.4f want=0.0000 -> %s\n", out[0], pass0 ? "PASS" : "FAIL");
    const float want[3] = {0.2f, 0.36f, 0.488f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("Damp", {{"Value", 1.0f}, {"Damping", 0.8f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
      // injectBug: a swapped-arg lerp (Lerp(current,target,d)) would give 0.8 on step 1.
      float w = (injectBug && i == 0) ? 0.8f : want[i];
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Damp.lin step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- Damp, DampedSpring (Method 1), Damping=0.5, dt fed HUGE (10s) -----
  // From rest one step: d1 = (k*ts)*ts with ts CLAMPED to 1/60 (proves the clamp — an unclamped
  // 10s step would explode). k = 0.5/(0.5+0.001) = 0.998004; d1 = 0.998004 * (1/60)^2 = 0.00027722.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("Damp", {{"Value", 0.0f}, {"Damping", 0.5f}, {"Method", 1.0f}}, 10.0f, 0.0f, st, out);  // init -> 0
    cookStatefulValueOp("Damp", {{"Value", 1.0f}, {"Damping", 0.5f}, {"Method", 1.0f}}, 10.0f, 0.0f, st, out);
    const float k = 0.5f / 0.501f;
    float want = injectBug ? 0.05f : (k * dt60 * dt60);  // ~0.00027722; injectBug = unclamped-ish wrong
    bool pass = std::fabs(out[0] - want) < eps;
    ok = ok && pass;
    printf("[selftest-statefulvalue] Damp.spring step1(dt=10 clamped)=%.6f want=%.6f -> %s\n", out[0], want, pass ? "PASS" : "FAIL");
  }

  // ----- Spring, Tension=0.5, Strength=1, Value=1 (overshoot then settle) -----
  //   springed = Lerp(springed, (1-result)*1, 0.5); result += springed.
  //   result: 0.5, 1.0, 1.25 (overshoot), 1.25, 1.125.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float want[5] = {0.5f, 1.0f, 1.25f, 1.25f, 1.125f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("Spring", {{"Value", 1.0f}, {"Tension", 0.5f}, {"Strength", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 1.0f : want[i];  // bug: no overshoot
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Spring step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- DampAngle, Linear (Method 0), Damping=0.5, Value=350° (shortest-path wrap) -----
  // No init seeding (damped=0). DeltaAngle(0,350) = -10 (short way), targetAngle=-10;
  //   d1 = Lerp(-10,0,0.5) = -5. Then DeltaAngle(-5,350)=-5, targetAngle=-10; d2 = Lerp(-10,-5,0.5)=-7.5.
  // A naive damp (no wrap) would chase +350 — so -5/-7.5 proves the angular re-target.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float want[2] = {-5.0f, -7.5f};
    for (int i = 0; i < 2; ++i) {
      cookStatefulValueOp("DampAngle", {{"Value", 350.0f}, {"Damping", 0.5f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 0) ? 175.0f : want[i];  // bug: no wrap → chases +350
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] DampAngle step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- DeltaSinceLastFrame: Value 5,8,8 → delta 5,3,0 (lastValue init 0) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float vals[3] = {5.0f, 8.0f, 8.0f};
    const float want[3] = {5.0f, 3.0f, 0.0f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("DeltaSinceLastFrame", {{"Value", vals[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 8.0f : want[i];  // bug: returns raw value, not the delta
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Delta step%d=%.4f want=%.4f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- FreezeValue, Mode 0 (FreezeWhileTrue): track while Freeze=0, hold while Freeze=1 -----
  // frames (Value,Freeze): (2,0)(5,1)(9,1)(9,0) → Result 2,2,2,9 ; DeltaSinceFreeze 0,3,7,0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {2.0f, 5.0f, 9.0f, 9.0f};
    const float frz[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float wantR[4] = {2.0f, 2.0f, 2.0f, 9.0f};
    const float wantD[4] = {0.0f, 3.0f, 7.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("FreezeValue", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 0.0f}}, dt60, 0.0f, st, out);
      float wr = (injectBug && i == 2) ? 9.0f : wantR[i];  // bug: doesn't hold (tracks while frozen)
      bool pass = std::fabs(out[0] - wr) < eps && std::fabs(out[1] - wantD[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Freeze0 step%d R=%.4f(want %.4f) D=%.4f -> %s\n", i + 1, out[0], wr, out[1], pass ? "PASS" : "FAIL");
    }
  }

  // ----- FreezeValue, Mode 1 (UpdateWhenSwitchingToTrue): sample only on the Freeze rising edge -----
  // frames (Value,Freeze): (2,0)(5,1)(8,1) → Result 0,5,5 (samples 5 when Freeze goes 0→1).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[3] = {2.0f, 5.0f, 8.0f};
    const float frz[3] = {0.0f, 1.0f, 1.0f};
    const float wantR[3] = {0.0f, 5.0f, 5.0f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("FreezeValue", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 1.0f}}, dt60, 0.0f, st, out);
      bool pass = std::fabs(out[0] - wantR[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Freeze1 step%d R=%.4f want=%.4f -> %s\n", i + 1, out[0], wantR[i], pass ? "PASS" : "FAIL");
    }
  }

  // ----- DampVec3, Linear, Damping=0.5, Value=(1,2,4) — component-wise, no bleed, damps from 0 -----
  //   step1 = (0.5, 1.0, 2.0); step2 = (0.75, 1.5, 3.0).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float w1[3] = {0.5f, 1.0f, 2.0f};
    const float w2[3] = {0.75f, 1.5f, 3.0f};
    cookStatefulValueOp("DampVec3", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Value.z", 4.0f}, {"Damping", 0.5f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
    bool p1 = std::fabs(out[0] - w1[0]) < eps && std::fabs(out[1] - w1[1]) < eps && std::fabs(out[2] - (injectBug ? 9.9f : w1[2])) < eps;
    ok = ok && p1;
    printf("[selftest-statefulvalue] DampVec3 step1=(%.3f,%.3f,%.3f) -> %s\n", out[0], out[1], out[2], p1 ? "PASS" : "FAIL");
    cookStatefulValueOp("DampVec3", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Value.z", 4.0f}, {"Damping", 0.5f}, {"Method", 0.0f}}, dt60, 0.0f, st, out);
    bool p2 = std::fabs(out[0] - w2[0]) < eps && std::fabs(out[1] - w2[1]) < eps && std::fabs(out[2] - w2[2]) < eps;
    ok = ok && p2;
    printf("[selftest-statefulvalue] DampVec3 step2=(%.3f,%.3f,%.3f) -> %s\n", out[0], out[1], out[2], p2 ? "PASS" : "FAIL");
  }

  // ----- SpringVec2, Tension=0.5, Strength=1, Value=(1,2) — component-wise, no bleed -----
  //   step1 = (0.5, 1.0); step2 = (1.0, 2.0).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float w1[2] = {0.5f, 1.0f};
    const float w2[2] = {1.0f, 2.0f};
    cookStatefulValueOp("SpringVec2", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Tension", 0.5f}, {"Strength", 1.0f}}, dt60, 0.0f, st, out);
    bool p1 = std::fabs(out[0] - w1[0]) < eps && std::fabs(out[1] - w1[1]) < eps;
    ok = ok && p1;
    printf("[selftest-statefulvalue] SpringVec2 step1=(%.3f,%.3f) -> %s\n", out[0], out[1], p1 ? "PASS" : "FAIL");
    cookStatefulValueOp("SpringVec2", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Tension", 0.5f}, {"Strength", 1.0f}}, dt60, 0.0f, st, out);
    bool p2 = std::fabs(out[0] - w2[0]) < eps && std::fabs(out[1] - w2[1]) < eps;
    ok = ok && p2;
    printf("[selftest-statefulvalue] SpringVec2 step2=(%.3f,%.3f) -> %s\n", out[0], out[1], p2 ? "PASS" : "FAIL");
  }

  // ----- Ease, Linear (Interpolation 0, Direction In), Duration=1.0 -----
  // Cook1 time=0 input=0: seeds (no restart, dist=0) → 0. Cook2 time=0 input=1: dist=1>0.001 →
  // RESTART startTime=0, initial=0, target=1; progress=0 → 0. Then sample the SAME animation as
  // wall time advances: t=0.25/0.5/1.0 → progress=0.25/0.5/1.0 → Result=0.25/0.5/1.0 (linear).
  // Proves elapsed/duration progress + restart capture.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("Ease", {{"Value", val}, {"Duration", 1.0f}, {"Direction", 0.0f}, {"Interpolation", 0.0f}}, dt60, time, st, out);
    };
    cook(0.0f, 0.0f);  // seed prevInput=0
    cook(0.0f, 1.0f);  // restart at t=0
    bool p0 = std::fabs(out[0] - 0.0f) < eps;  // progress 0 at restart
    ok = ok && p0;
    printf("[selftest-statefulvalue] Ease.lin restart=%.4f want=0.0000 -> %s\n", out[0], p0 ? "PASS" : "FAIL");
    const float t[3] = {0.25f, 0.5f, 1.0f};
    const float want[3] = {0.25f, 0.5f, 1.0f};
    for (int i = 0; i < 3; ++i) {
      cook(t[i], 1.0f);
      float w = (injectBug && i == 0) ? 0.5f : want[i];  // bug: progress not driven by elapsed/duration
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Ease.lin t=%.2f=%.4f want=%.4f -> %s\n", t[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- Ease, Quad + Out (Interpolation 2, Direction 1), Duration=1.0, at progress=0.5 -----
  // Restart toward 1 at t=0, then sample t=0.5 → OutQuad(0.5)=0.5*(2-0.5)=0.75 → Result=0.75.
  // A linear curve would give 0.5 here — so 0.75 PROVES the EasingFunctions dispatch is wired.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("Ease", {{"Value", val}, {"Duration", 1.0f}, {"Direction", 1.0f}, {"Interpolation", 2.0f}}, dt60, time, st, out);
    };
    cook(0.0f, 0.0f);
    cook(0.0f, 1.0f);          // restart
    cook(0.5f, 1.0f);          // progress 0.5 → OutQuad(0.5)=0.75
    float w = injectBug ? 0.5f : 0.75f;  // bug: linear (ignores the easing curve)
    bool pass = std::fabs(out[0] - w) < eps;
    ok = ok && pass;
    printf("[selftest-statefulvalue] Ease.outQuad p=0.5=%.4f want=%.4f -> %s\n", out[0], w, pass ? "PASS" : "FAIL");
  }

  // ----- EaseVec3, Linear, Duration=1.0, input (1,2,4): per-channel, NO cross-channel bleed -----
  // Cook1 t=0 restart: initial=(0,0,0), target=(1,2,4), progress=0 → (0,0,0). Cook2 t=0.5: progress
  // 0.5 → (0.5,1.0,2.0). Each component eases on its OWN initial/target (proves the s[12] layout).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time) {
      cookStatefulValueOp("EaseVec3", {{"Value.x", 1.0f}, {"Value.y", 2.0f}, {"Value.z", 4.0f}, {"Duration", 1.0f}, {"Direction", 0.0f}, {"Interpolation", 0.0f}}, dt60, time, st, out);
    };
    cook(0.0f);  // restart, progress 0 → (0,0,0)
    cook(0.5f);  // progress 0.5 → (0.5,1.0,2.0)
    // injectBug: a channel-bleed bug (e.g. y using x's target) would break the .y expectation.
    float wy = injectBug ? 0.5f : 1.0f;
    bool pass = std::fabs(out[0] - 0.5f) < eps && std::fabs(out[1] - wy) < eps && std::fabs(out[2] - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-statefulvalue] EaseVec3 p=0.5=(%.3f,%.3f,%.3f) want=(0.500,%.3f,2.000) -> %s\n", out[0], out[1], out[2], wy, pass ? "PASS" : "FAIL");
  }

  // ----- HasValueIncreased, Threshold=0.5: Value 1,3,3.2,2 → flag 1,1,0,0 -----
  // lastValue inits 0. f1: 1>0+0.5 → 1. f2: 3>1+0.5 → 1. f3: 3.2>3+0.5? (3.5) no → 0. f4: 2>3.2+0.5 no → 0.
  // (f3 proves the Threshold band — a +0.2 rise under the 0.5 threshold does NOT trigger.)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {1.0f, 3.0f, 3.2f, 2.0f};
    const float want[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasValueIncreased", {{"Value", val[i]}, {"Threshold", 0.5f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 1.0f : want[i];  // bug: ignores Threshold → +0.2 falsely fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasIncreased step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- HasValueDecreased, Threshold=0.5: Value 5,2,1.9,3 → flag 1,1,0,0 -----
  // lastValue inits 0. f1: 5<0-0.5 no → 0. Wait: f1 5<-0.5 false → 0. So expect 0,1,0,0.
  // f1: 5 < 0-0.5? no → 0. f2: 2 < 5-0.5 (4.5)? yes → 1. f3: 1.9 < 2-0.5 (1.5)? no → 0. f4: 3 < 1.9-0.5 no → 0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {5.0f, 2.0f, 1.9f, 3.0f};
    const float want[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasValueDecreased", {{"Value", val[i]}, {"Threshold", 0.5f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 0.0f : want[i];  // bug: never fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasDecreased step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== HasValueChanged (TiXL float/logic/HasValueChanged.cs) =====
  // Helper to cook one HasValueChanged frame; checks HasChanged(out0), Delta(out1), DeltaOnHit(out2).
  // ----- (A) Mode=Increased, Threshold=0.5, time=0, MinTime=0, Prevent=0: cross/no-cross + Delta -----
  // lastValue inits 0. f1 V=1: 1>0+0.5 → HC=1, Delta=1, DeltaOnHit=|1-0|=1. f2 V=1.2: 1.2>1+0.5(1.5)?
  // no → HC=0, Delta=0.2. f3 V=2: 2>1.2+0.5(1.7)? yes → HC=1, Delta=0.8, DeltaOnHit=|2-1.2|=0.8.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float val) {
      cookStatefulValueOp("HasValueChanged",
                          {{"Value", val}, {"Threshold", 0.5f}, {"Mode", 1.0f},
                           {"MinTimeBetweenHits", 0.0f}, {"PreventContinuedChanges", 0.0f}},
                          dt60, 0.0f, st, out);
    };
    const float val[3] = {1.0f, 1.2f, 2.0f};
    const float wHC[3] = {1.0f, 0.0f, 1.0f};
    const float wDelta[3] = {1.0f, 0.2f, 0.8f};
    const float wHit[3] = {1.0f, 1.0f, 0.8f};
    for (int i = 0; i < 3; ++i) {
      cook(val[i]);
      float hc = (injectBug && i == 1) ? 1.0f : wHC[i];  // bug: ignores Threshold → +0.2 falsely fires
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - wDelta[i]) < eps &&
                  std::fabs(out[2] - wHit[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasChanged.inc step%d HC=%.1f(want %.1f) D=%.3f Hit=%.3f -> %s\n",
             i + 1, out[0], hc, out[1], out[2], pass ? "PASS" : "FAIL");
    }
  }

  // ----- (B) MinTimeBetweenHits gate, Mode=Changed, Threshold=0.5, MinTime=2.0, Prevent=0 -----
  // Two qualifying changes within < MinTime → the 2nd is suppressed (HC forced 0); after the gate
  // elapses it fires again. A false frame between rising edges re-arms wasHit so wasTriggered fires.
  // f1 t=10 V=3: absΔ3>0.5 T, edge(prev F)→T, gate|10-0|=10≥2 → HIT, lastHitTime=10, lastHitDelta=3, HC=1.
  // f2 t=10.5 V=3: absΔ0 → HC=0 (re-arm). f3 t=10.5 V=6: absΔ3 T, edge T, gate|10.5-10|=0.5≥2? no →
  //   HC FORCED 0 (suppressed), DeltaOnHit stays 3. f4 t=13 V=6: absΔ0 → HC=0 (re-arm).
  // f5 t=13 V=9: absΔ3 T, edge T, gate|13-10|=3≥2 → HIT, lastHitTime=13, lastHitDelta=3, HC=1.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("HasValueChanged",
                          {{"Value", val}, {"Threshold", 0.5f}, {"Mode", 0.0f},
                           {"MinTimeBetweenHits", 2.0f}, {"PreventContinuedChanges", 0.0f}},
                          dt60, time, st, out);
    };
    const float tm[5] = {10.0f, 10.5f, 10.5f, 13.0f, 13.0f};
    const float val[5] = {3.0f, 3.0f, 6.0f, 6.0f, 9.0f};
    const float wHC[5] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const float wHit[5] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f};  // DeltaOnHit holds last accepted absDelta
    for (int i = 0; i < 5; ++i) {
      cook(tm[i], val[i]);
      float hc = (injectBug && i == 2) ? 1.0f : wHC[i];  // bug: gate not enforced → 2nd hit fires early
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[2] - wHit[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasChanged.gate step%d HC=%.1f(want %.1f) Hit=%.3f -> %s\n",
             i + 1, out[0], hc, out[2], pass ? "PASS" : "FAIL");
    }
  }

  // ----- (C) WasTriggered edge + PreventContinuedChanges semantics (FAITHFUL TiXL, NOT the brief) -----
  // Sustained increase, Mode=Increased, Threshold=0, MinTime=2.0. The gate body runs only when
  // `hasChanged && (prevent || wasTriggered)`. So:
  //   Prevent=0: a CONTINUED change (wasTriggered=F) SKIPS the gate → HC passes through raw=1 each
  //     frame. Sequence HC = 1,1,1,1 (continued raw changes get through).
  //   Prevent=1: EVERY qualifying frame enters the gate → continued frames within MinTime are FORCED
  //     0 (this is what "prevent continued changes" means); only frames past MinTime fire.
  //     Sequence HC = 1,0,0,1. (NOTE: the brief described these two inverted — TiXL is authority;
  //     flagged in the dossier. _wasHit stores the raw hasChanged BEFORE the gate, per TiXL order.)
  {
    const float tm[4] = {10.0f, 10.5f, 11.0f, 13.0f};
    const float val[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    // Prevent=0 → all raw changes pass.
    {
      StatefulValueState st;
      float out[3] = {0, 0, 0};
      const float wHC[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      for (int i = 0; i < 4; ++i) {
        cookStatefulValueOp("HasValueChanged",
                            {{"Value", val[i]}, {"Threshold", 0.0f}, {"Mode", 1.0f},
                             {"MinTimeBetweenHits", 2.0f}, {"PreventContinuedChanges", 0.0f}},
                            dt60, tm[i], st, out);
        bool pass = std::fabs(out[0] - wHC[i]) < eps;
        ok = ok && pass;
        printf("[selftest-statefulvalue] HasChanged.prevent0 step%d HC=%.1f want=%.1f -> %s\n",
               i + 1, out[0], wHC[i], pass ? "PASS" : "FAIL");
      }
    }
    // Prevent=1 → continued changes within MinTime suppressed; fires only after gate elapses.
    {
      StatefulValueState st;
      float out[3] = {0, 0, 0};
      const float wHC[4] = {1.0f, 0.0f, 0.0f, 1.0f};
      for (int i = 0; i < 4; ++i) {
        cookStatefulValueOp("HasValueChanged",
                            {{"Value", val[i]}, {"Threshold", 0.0f}, {"Mode", 1.0f},
                             {"MinTimeBetweenHits", 2.0f}, {"PreventContinuedChanges", 1.0f}},
                            dt60, tm[i], st, out);
        float hc = (injectBug && i == 3) ? 0.0f : wHC[i];  // bug: gate never re-fires after suppression
        bool pass = std::fabs(out[0] - hc) < eps;
        ok = ok && pass;
        printf("[selftest-statefulvalue] HasChanged.prevent1 step%d HC=%.1f want=%.1f -> %s\n",
               i + 1, out[0], hc, pass ? "PASS" : "FAIL");
      }
    }
  }

  // ===== DetectPulse (TiXL float/process/DetectPulse.cs) =====
  // Damping=0.5, Threshold=0.5, MinTimeBetweenHits=2.0. dampedValue inits 0, lastHitTime=−∞, wasHit=F.
  // Hand-computed trajectory (delta=dampedPre−new; damped'=Lerp(new,dampedPre,0.5)=new+(dampedPre−new)*0.5):
  //   f1 t=10  V=10: delta=0−10=−10  (no exceed); damped→Lerp(10,0,.5)=5 ;            HC=0 Dbg=−10
  //   f2 t=10.5 V=0 : delta=5−0=5    exceed,edge(prevHit F)→T; gate huge≥2 → HIT;
  //                     damped→Lerp(0,5,.5)=2.5 ; lastHit=10.5 ;                        HC=1 Dbg=5
  //   f3 t=10.5 V=0 : delta=2.5      exceed but edge=F (wasHit T) → no fire (rising-edge only);
  //                     damped→Lerp(0,2.5,.5)=1.25 ;                                    HC=0 Dbg=2.5
  //   f4 t=11   V=5 : delta=1.25−5=−3.75 (no exceed, re-arm wasHit→F);
  //                     damped→Lerp(5,1.25,.5)=3.125 ;                                  HC=0 Dbg=−3.75
  //   f5 t=11.5 V=0 : delta=3.125    exceed,edge T; gate |11.5−10.5|=1<2 → SUPPRESSED;
  //                     damped→Lerp(0,3.125,.5)=1.5625 ;                                HC=0 Dbg=3.125
  //   f6 t=11.5 V=5 : delta=1.5625−5=−3.4375 (no exceed, re-arm);
  //                     damped→Lerp(5,1.5625,.5)=3.28125 ;                              HC=0 Dbg=−3.4375
  //   f7 t=13   V=0 : delta=3.28125  exceed,edge T; gate |13−10.5|=2.5≥2 → HIT;
  //                     damped→Lerp(0,3.28125,.5)=1.640625 ; lastHit=13 ;              HC=1 Dbg=3.28125
  // f3 proves rising-edge-only (sustained exceed fires once); f5 proves the MinTime gate; f7 proves it
  // re-fires after the window. DebugValue is asserted = pre-update damped − new on every frame.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    auto cook = [&](float time, float val) {
      cookStatefulValueOp("DetectPulse",
                          {{"Value", val}, {"Threshold", 0.5f}, {"Damping", 0.5f},
                           {"MinTimeBetweenHits", 2.0f}},
                          dt60, time, st, out);
    };
    const float tm[7]  = {10.0f, 10.5f, 10.5f, 11.0f,  11.5f, 11.5f,   13.0f};
    const float val[7] = {10.0f,  0.0f,  0.0f,  5.0f,   0.0f,  5.0f,    0.0f};
    const float wHC[7] = { 0.0f,  1.0f,  0.0f,  0.0f,   0.0f,  0.0f,    1.0f};
    const float wDbg[7]= {-10.0f, 5.0f,  2.5f, -3.75f,  3.125f,-3.4375f,3.28125f};
    for (int i = 0; i < 7; ++i) {
      cook(tm[i], val[i]);
      // injectBug on f5: corrupts the MinTime-gate expectation so the gate-suppression frame is
      // asserted to FIRE — the live (correct) HC=0 then mismatches and FAILS.
      float hc = (injectBug && i == 4) ? 1.0f : wHC[i];
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - wDbg[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] DetectPulse step%d HC=%.1f(want %.1f) Dbg=%.4f(want %.4f) -> %s\n",
             i + 1, out[0], hc, out[1], wDbg[i], pass ? "PASS" : "FAIL");
    }
  }

  // ----- Accumulator PerFrame, Increment=1, Running=1: v=0→ 1,2,3 -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Accumulate", 0.0f}, {"Running", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 5.0f : (float)(i + 1);  // bug: wrong running total
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Accum.perFrame step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- Accumulator Running=0 freezes at 0; then ResetTrigger reloads StartValue=10 then +1 → 11 -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Running", 0.0f}}, dt60, 0.0f, st, out);
    bool p0 = std::fabs(out[0] - 0.0f) < eps;  // not running → stays 0
    cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Running", 1.0f}, {"StartValue", 10.0f}, {"ResetTrigger", 1.0f}}, dt60, 0.0f, st, out);
    float wR = injectBug ? 1.0f : 11.0f;  // bug: reset ignored → 0+1
    bool pR = std::fabs(out[0] - wR) < eps;  // reset→10 then +1 (same frame) → 11
    ok = ok && p0 && pR;
    printf("[selftest-statefulvalue] Accum.freeze=%.1f(want 0) reset+inc=%.1f(want %.1f) -> %s\n", 0.0f, out[0], wR, (p0 && pR) ? "PASS" : "FAIL");
  }
  // ----- Accumulator Modulo=3, +1/frame: v 1,2,3,4 → fmod(.,3) = 1,2,0,1 -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float want[4] = {1.0f, 2.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("Accumulator", {{"Increment", 1.0f}, {"Running", 1.0f}, {"Modulo", 3.0f}}, dt60, 0.0f, st, out);
      bool pass = std::fabs(out[0] - want[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Accum.mod3 step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], want[i], pass ? "PASS" : "FAIL");
    }
  }
  // ----- Accumulator PerSeconds, Increment=60, dt=1/60 → +1/frame: 1,2 (proves dt path) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    for (int i = 0; i < 2; ++i) {
      cookStatefulValueOp("Accumulator", {{"Increment", 60.0f}, {"Accumulate", 1.0f}, {"Running", 1.0f}}, dt60, 0.0f, st, out);
      float w = (float)(i + 1);
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Accum.perSec step%d=%.2f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ----- HasVec2Changed, Threshold=0.5: moves (0,0)→(1,0)→(1,0)→(1,1) → HasChanged 0,1,0,1 -----
  // Delta(signed) = newValue-lastValue: (0,0),(1,0),(0,0),(0,1). (Euclidean dist > 0.5 fires.)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float vx[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    const float vy[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float wHC[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const float wDy[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasVec2Changed", {{"Value.x", vx[i]}, {"Value.y", vy[i]}, {"Threshold", 0.5f}}, dt60, 0.0f, st, out);
      float whc = (injectBug && i == 1) ? 0.0f : wHC[i];  // bug: misses the move
      bool pass = std::fabs(out[0] - whc) < eps && std::fabs(out[2] - wDy[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasVec2Changed step%d HC=%.1f(want %.1f) Dy=%.1f -> %s\n", i + 1, out[0], whc, out[2], pass ? "PASS" : "FAIL");
    }
  }

  // ----- HasVec3Changed (7-output, proves >3-out seam), Threshold=0.5, Mode=Changed -----
  // (0,0,0)→(0,3,0)→(0,3,0): HasChanged 0,1,0; Delta.y 0,3,0; DeltaOnHit.y captured 0,3,3 (holds).
  {
    StatefulValueState st;
    float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float vy[3] = {0.0f, 3.0f, 3.0f};
    const float wHC[3] = {0.0f, 1.0f, 0.0f};
    const float wDy[3] = {0.0f, 3.0f, 0.0f};   // Delta.y (signed) = out[2]
    const float wDoh[3] = {0.0f, 3.0f, 3.0f};  // DeltaOnHit.y = out[5] (held after the hit)
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("HasVec3Changed", {{"Value.x", 0.0f}, {"Value.y", vy[i]}, {"Value.z", 0.0f}, {"Threshold", 0.5f}, {"Mode", 0.0f}}, dt60, 0.0f, st, out);
      float whc = (injectBug && i == 1) ? 0.0f : wHC[i];
      bool pass = std::fabs(out[0] - whc) < eps && std::fabs(out[2] - wDy[i]) < eps && std::fabs(out[5] - wDoh[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasVec3Changed step%d HC=%.1f(want %.1f) Dy=%.1f DoH.y=%.1f -> %s\n", i + 1, out[0], whc, out[2], out[5], pass ? "PASS" : "FAIL");
    }
  }
  // ----- PeakLevel (4-output, proves out[3] seam): Threshold=0.5. value 0,1,1.2,2.5 (time 1,2,2.5,3) -----
  // increase 0,1,0.2,1.3 → FoundPeak 0,1,0,1; MovingSum 0,1,1.2,2.5 (accumulator at out[3]).
  {
    StatefulValueState st;
    float out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 1.2f, 2.5f};
    const float tm[4] = {1.0f, 2.0f, 2.5f, 3.0f};
    const float wFP[4] = {0.0f, 1.0f, 0.0f, 1.0f};   // FoundPeak = out[1]
    const float wMS[4] = {0.0f, 1.0f, 1.2f, 2.5f};   // MovingSum = out[3]
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("PeakLevel", {{"Value", val[i]}, {"Threshold", 0.5f}, {"MinTimeBetweenPeaks", 0.0f}}, dt60, tm[i], st, out);
      float wms = (injectBug && i == 3) ? 1.3f : wMS[i];  // bug: MovingSum forgets to accumulate
      bool pass = std::fabs(out[1] - wFP[i]) < eps && std::fabs(out[3] - wms) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] PeakLevel step%d FoundPeak=%.1f(want %.1f) MovingSum=%.2f(want %.2f) -> %s\n", i + 1, out[1], wFP[i], out[3], wms, pass ? "PASS" : "FAIL");
    }
  }

  // ----- CountInt DEFAULT held-true free-running: counts EVERY evaluated frame → 1,2,3,4 -----
  // The load-bearing case (the BLOCK). With .t3 defaults (TriggerIncrement=true, OnlyCountChanges=false,
  // Delta=1) CountInt is a free-running per-frame counter: TiXL CountInt.cs:36 `if (triggeredIncrement)
  // Result.Value += delta;` fires on the raw LEVEL every evaluated frame. The FIRST cook reloads
  // DefaultValue=0 (CountInt.cs:45 !_initialized, AFTER the +=1), so it emits 0; then each held-true
  // cook adds 1 → 1,2,3,4. (A rising-edge port would stick at 0 forever — exactly the BLOCK.)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // init cook → 0
    bool p0 = std::fabs(out[0] - 0.0f) < eps;
    const float want[4] = {1.0f, 2.0f, 3.0f, 4.0f};  // free-running per-frame count
    bool seqOk = p0;
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 2.0f : want[i];  // bug: held-true stops counting (rising-edge regression) → stuck at 2
      bool pass = std::fabs(out[0] - w) < eps;
      seqOk = seqOk && pass;
      printf("[selftest-statefulvalue] CountInt.level step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
    ok = ok && seqOk;
  }
  // ----- CountInt OnlyCountChanges=true: counts ONLY on frames where the trigger value CHANGED -----
  // The gate (CountInt.cs:28-30) skips the WHOLE step on frames where neither trigger changed since
  // last frame. The init cook (inc=true, CHANGED from lastInc=false) is NOT gated → +=1 then the
  // !_initialized reload clobbers to default 0, sets initialized + stores lastInc=true.
  // Then TriggerIncrement = false,false,true,true,false,true (held OnlyCountChanges=true):
  //   f1 inc=false (CHANGED true→false → step: inc false so no add, hold 0, store prev=false)
  //   f2 inc=false (==prev false, notChanged → GATED, hold 0)
  //   f3 inc=true  (CHANGED false→true → step: +=1 → 1, store prev=true)
  //   f4 inc=true  (==prev true, notChanged → GATED, hold 1)   ← LEVEL would give 2; gate blocks it
  //   f5 inc=false (CHANGED true→false → step: no add, hold 1, store prev=false)
  //   f6 inc=true  (CHANGED false→true → step: +=1 → 2)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"OnlyCountChanges", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // init → 0, prev=true
    bool p0 = std::fabs(out[0] - 0.0f) < eps;
    const float trig[6] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[6] = {0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 2.0f};
    bool seqOk = p0;
    for (int i = 0; i < 6; ++i) {
      cookStatefulValueOp("CountInt", {{"TriggerIncrement", trig[i]}, {"OnlyCountChanges", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 3) ? 2.0f : want[i];  // bug: gate fails to skip held-true → counts again → 2
      bool pass = std::fabs(out[0] - w) < eps;
      seqOk = seqOk && pass;
      printf("[selftest-statefulvalue] CountInt.onlychg step%d(t=%.0f)=%.1f want=%.1f -> %s\n", i + 1, trig[i], out[0], w, pass ? "PASS" : "FAIL");
    }
    ok = ok && seqOk;
  }
  // ----- CountInt first-cook inits to DefaultValue, then LEVEL decrement, then ResetTrigger→DefaultValue -----
  // TiXL reloads DefaultValue on the FIRST cook (!_initialized), AFTER the step — so frame 1 = default(0)
  // regardless of any trigger. Then a held decrement → −1 (level, one frame). Then TriggerReset → 10.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 0.0f}, {"TriggerDecrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // first cook → default 0
    bool p1 = std::fabs(out[0] - 0.0f) < eps;  // !_initialized reload (any trigger overridden)
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 0.0f}, {"TriggerDecrement", 1.0f}, {"Delta", 1.0f}}, dt60, 0.0f, st, out);  // level dec → −1
    bool p2 = std::fabs(out[0] - (-1.0f)) < eps;
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 0.0f}, {"TriggerReset", 1.0f}, {"DefaultValue", 10.0f}}, dt60, 0.0f, st, out);  // reset → 10
    float wR = injectBug ? -1.0f : 10.0f;  // bug: reset ignored
    bool pR = std::fabs(out[0] - wR) < eps;
    ok = ok && p1 && p2 && pR;
    printf("[selftest-statefulvalue] CountInt.dec/reset init=%.1f dec=%.1f reset=%.1f(want %.1f) -> %s\n",
           0.0f, -1.0f, out[0], wR, (p1 && p2 && pR) ? "PASS" : "FAIL");
  }
  // ----- CountInt Modulo=3 with held-true level inc: count 1,2,3,4,5 → 1,2,0,1,2 (C# truncated %) -----
  // Init cook reloads default 0; then held TriggerIncrement counts 1,2,3,4,5 each wrapped %3.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}, {"Modulo", 3.0f}}, dt60, 0.0f, st, out);  // init → 0
    const float want[5] = {1.0f, 2.0f, 0.0f, 1.0f, 2.0f};  // raw 1,2,3,4,5 %3
    bool seqOk = true;
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("CountInt", {{"TriggerIncrement", 1.0f}, {"Delta", 1.0f}, {"Modulo", 3.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 3.0f : want[i];  // bug: modulo dropped → 3 instead of 0
      bool pass = std::fabs(out[0] - w) < eps;
      seqOk = seqOk && pass;
      printf("[selftest-statefulvalue] CountInt.mod3 step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
    ok = ok && seqOk;
  }

  // ----- FlipBool rising-edge toggle: Trigger false→true→false→true → bool 0,1,1,0 -----
  // Toggles only on the rising edge; the held-true frame (step3) must NOT re-toggle.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float trig[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("FlipBool", {{"Trigger", trig[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: held-true re-toggles
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] FlipBool.toggle step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- FlipBool two clean rising edges 0→1→0→1 + ResetTrigger→DefaultValue(=0) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    // edge1: false→true → 1 ; arm low ; edge2: false→true → 0 ; arm low ; edge3: false→true → 1
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // 0→1
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("FlipBool", {{"Trigger", 0.0f}}, dt60, 0.0f, st, out);  // arm low (holds 1)
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // 1→0
    bool p2 = std::fabs(out[0] - 0.0f) < eps;
    cookStatefulValueOp("FlipBool", {{"Trigger", 0.0f}}, dt60, 0.0f, st, out);  // arm low (holds 0)
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // 0→1
    bool p3 = std::fabs(out[0] - 1.0f) < eps;
    // reset → DefaultValue(0), even though a rising edge is ALSO present (reset wins over the toggle)
    cookStatefulValueOp("FlipBool", {{"Trigger", 0.0f}}, dt60, 0.0f, st, out);  // arm low (holds 1)
    cookStatefulValueOp("FlipBool", {{"Trigger", 1.0f}, {"ResetTrigger", 1.0f}, {"DefaultValue", 0.0f}}, dt60, 0.0f, st, out);
    float wR = injectBug ? 1.0f : 0.0f;  // bug: reset loses to toggle → 1→0... err 1 (toggle of held-1)
    bool pR = std::fabs(out[0] - wR) < eps;
    bool flips = p1 && p2 && p3;
    ok = ok && flips && pR;
    printf("[selftest-statefulvalue] FlipBool.seq 0->1=%.1f 1->0=%.1f 0->1=%.1f reset(wins)=%.1f(want %.1f) -> %s\n",
           1.0f, 0.0f, 1.0f, out[0], wR, (flips && pR) ? "PASS" : "FAIL");
  }

  // ----- HasIntChanged (Mode=Changed=3): feed 5,5,7,7,3 → changed 0,0,1,0,1 -----
  // First frame compares against the zero-init lastValue (5≠0 → changed=1). To assert the brief's
  // "first/0" we seed with a leading 0 frame so the 5,5,7,7,3 run reads against a known prior.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasIntChanged", {{"Value", 0.0f}, {"ReturnTrueIf", 3.0f}}, dt60, 0.0f, st, out);  // seed lastValue=0
    const float val[5] = {5.0f, 5.0f, 7.0f, 7.0f, 3.0f};
    const float want[5] = {1.0f, 0.0f, 1.0f, 0.0f, 1.0f};  // 0→5 changes; 5,5 no; 5→7 yes; 7,7 no; 7→3 yes
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("HasIntChanged", {{"Value", val[i]}, {"ReturnTrueIf", 3.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 1) ? 1.0f : want[i];  // bug: stale-value compare → false-fire on 5,5
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasIntChanged.changed step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- HasIntChanged Increased(1)/Decreased(2) modes + int-truncate (4.9 → 4) -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasIntChanged", {{"Value", 5.0f}, {"ReturnTrueIf", 1.0f}}, dt60, 0.0f, st, out);  // seed last=5 (0→5 increased=1)
    cookStatefulValueOp("HasIntChanged", {{"Value", 4.9f}, {"ReturnTrueIf", 1.0f}}, dt60, 0.0f, st, out);  // 4.9→4 (int); 4<5 NOT increased → 0
    bool pInc = std::fabs(out[0] - 0.0f) < eps;
    cookStatefulValueOp("HasIntChanged", {{"Value", 4.0f}, {"ReturnTrueIf", 2.0f}}, dt60, 0.0f, st, out);  // last int=4; 4<4 false (Decreased) → 0
    bool pDec0 = std::fabs(out[0] - 0.0f) < eps;
    cookStatefulValueOp("HasIntChanged", {{"Value", 2.0f}, {"ReturnTrueIf", 2.0f}}, dt60, 0.0f, st, out);  // 2<4 → Decreased=1
    float wDec = injectBug ? 0.0f : 1.0f;  // bug: never fires
    bool pDec1 = std::fabs(out[0] - wDec) < eps;
    ok = ok && pInc && pDec0 && pDec1;
    printf("[selftest-statefulvalue] HasIntChanged.modes inc(4.9->4)=%.1f dec(4)=%.1f dec(2)=%.1f(want %.1f) -> %s\n",
           0.0f, 0.0f, out[0], wDec, (pInc && pDec0 && pDec1) ? "PASS" : "FAIL");
  }

  // ----- ToggleBoolean rising-edge flip: TriggerToggle 0,1,1,0,1 → active 0,1,1,1,0 -----
  // Flips ONLY on the rising edge (the SetTypedInputValue(false) self-clear == an edge debounce). The
  // HELD-true frame (step3) must NOT re-flip — exactly the held-input case (the CountInt-blood lesson).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float trig[5] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[5] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f};  // edge flips: f2 0→1, f5 0→1 (back to 0)
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("ToggleBoolean", {{"TriggerToggle", trig[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: held-true re-flips (level, not edge)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] ToggleBoolean.edge step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- ToggleBoolean reset leg: flip on, then TriggerReset edge clears to false -----
  // edge-toggle to 1, then a rising-edge TriggerReset forces false (checked AFTER toggle in TiXL).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("ToggleBoolean", {{"TriggerToggle", 1.0f}}, dt60, 0.0f, st, out);  // 0→1
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("ToggleBoolean", {{"TriggerToggle", 0.0f}, {"TriggerReset", 1.0f}}, dt60, 0.0f, st, out);  // reset edge → 0
    float wR = injectBug ? 1.0f : 0.0f;  // bug: reset ignored
    bool pR = std::fabs(out[0] - wR) < eps;
    ok = ok && p1 && pR;
    printf("[selftest-statefulvalue] ToggleBoolean.reset on=%.1f reset=%.1f(want %.1f) -> %s\n", 1.0f, out[0], wR, (p1 && pR) ? "PASS" : "FAIL");
  }

  // ----- FlipFlop LEVEL set + HOLD: Trigger 0,1,1,0 → result 0,1,1,1 (holds after Trigger drops) -----
  // Trigger only ever SETS to true (no edge, no clear). step4 Trigger=0 must HOLD the prior 1 (no else
  // branch in TiXL) — the load-bearing held/hold case. A clear-on-low bug would drop step4 to 0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float trig[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 1.0f, 1.0f};  // set on f2, holds through f3 (level) and f4 (drop)
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("FlipFlop", {{"Trigger", trig[i]}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 3) ? 0.0f : want[i];  // bug: Trigger-low clears (loses the latch)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] FlipFlop.setHold step%d=%.1f want=%.1f -> %s\n", i + 1, out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- FlipFlop reset leg: set true, then LEVEL ResetTrigger → DefaultValue(0); reset wins -----
  // After latching true, ResetTrigger=1 reloads DefaultValue (here 0) even though Trigger=1 also held
  // (reset is checked first → wins over the set).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("FlipFlop", {{"Trigger", 1.0f}}, dt60, 0.0f, st, out);  // set → 1
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("FlipFlop", {{"Trigger", 1.0f}, {"ResetTrigger", 1.0f}, {"DefaultValue", 0.0f}}, dt60, 0.0f, st, out);  // reset wins → 0
    float wR = injectBug ? 1.0f : 0.0f;  // bug: trigger wins over reset → stays 1
    bool pR = std::fabs(out[0] - wR) < eps;
    ok = ok && p1 && pR;
    printf("[selftest-statefulvalue] FlipFlop.reset set=%.1f reset(wins)=%.1f(want %.1f) -> %s\n", 1.0f, out[0], wR, (p1 && pR) ? "PASS" : "FAIL");
  }

  // ----- HasBooleanChanged DEFAULT Mode=Increased(1): Value 0,1,1,0,1 → changed 0,1,0,0,1 -----
  // The .t3 default is Increased (NOT Changed) — fires ONLY on False→True. lastValue inits false.
  // f1 0 (0!=0? no)→0; f2 1 (1!=0 && 1)→1; f3 1 (held, 1==1)→0 [held case]; f4 0 (Decreased, &&new=F)→0;
  // f5 1 (False→True again)→1. step3 (held-true) and step4 (True→False) both 0 prove the Increased gate.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[5] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[5] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("HasBooleanChanged", {{"Value", val[i]}, {"Mode", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 3) ? 1.0f : want[i];  // bug: treats Increased as Changed → True→False false-fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasBoolChanged.inc step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // ----- HasBooleanChanged Changed(0) + Decreased(2) modes -----
  // Changed: fires on EITHER edge. Decreased: fires ONLY True→False.
  {
    // Changed(0): Value 0,1,1,0 → 0,1,0,1 (both edges fire; held step3 → 0).
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasBooleanChanged", {{"Value", val[i]}, {"Mode", 0.0f}}, dt60, 0.0f, st, out);
      bool pass = std::fabs(out[0] - want[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasBoolChanged.changed step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], want[i], pass ? "PASS" : "FAIL");
    }
  }
  {
    // Decreased(2): Value 0,1,0,0 → 0,0,1,0 (only True→False at f3 fires).
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float want[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("HasBooleanChanged", {{"Value", val[i]}, {"Mode", 2.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: Decreased never fires
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasBoolChanged.dec step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== Trigger (TiXL bool/logic/Trigger.cs) =====
  // (A) OnlyOnDown=true (.t3 default): a held-true BoolValue pulses EXACTLY ONCE on the rising edge.
  // BoolValue 0,1,1,0,1 → Result 0,1,0,0,1 (f2 rising edge fires; f3 held-true does NOT re-fire; f5
  // re-fires after the input dropped at f4). _isSet inits false → frame-1 true would itself be an edge.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[5] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const float want[5] = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("Trigger", {{"BoolValue", val[i]}, {"OnlyOnDown", 1.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 1.0f : want[i];  // bug: held-true re-fires (level, not edge)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Trigger.down step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }
  // (B) OnlyOnDown=false: transparent pass-through of the raw level. BoolValue 0,1,1,0 → Result 0,1,1,0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float want[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("Trigger", {{"BoolValue", val[i]}, {"OnlyOnDown", 0.0f}}, dt60, 0.0f, st, out);
      float w = (injectBug && i == 2) ? 0.0f : want[i];  // bug: pass-through edge-gates (drops held-true)
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] Trigger.thru step%d(v=%.0f)=%.1f want=%.1f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== KeepBoolean (TiXL bool/process/KeepBoolean.cs) — bool twin of FreezeValue + TimeSinceFreeze =====
  // (A) Mode=FreezeWhileTrue(0): track Value while !Freeze, HOLD while Freeze. TimeSinceFreeze counts
  // up from each rising Freeze edge (LocalTime → seam `time`). frames (time,Value,Freeze):
  //   (1,1,0): not frozen → Result=1; no freeze edge, freezeTime=0 → TSF=1.
  //   (2,1,1): freeze rising edge → freezeTime=2, hold Result=1 → TSF=0.
  //   (3,0,1): still frozen → hold Result=1 → TSF=3-2=1. (Value=0 ignored while frozen — proves HOLD.)
  //   (4,0,0): unfrozen → track Result=0 → TSF=4-2=2 (freezeTime unchanged → clock keeps running).
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float tm[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const float val[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    const float frz[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float wR[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float wT[4] = {1.0f, 0.0f, 1.0f, 2.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("KeepBoolean", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 0.0f}}, dt60, tm[i], st, out);
      float wr = (injectBug && i == 2) ? 0.0f : wR[i];  // bug: tracks Value while frozen (no hold)
      bool pass = std::fabs(out[0] - wr) < eps && std::fabs(out[1] - wT[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] KeepBool.while step%d R=%.1f(want %.1f) TSF=%.2f -> %s\n", i + 1, out[0], wr, out[1], pass ? "PASS" : "FAIL");
    }
  }
  // (B) Mode=UpdateWhenSwitchingToTrue(1): sample Value ONLY on the rising Freeze edge, hold otherwise.
  // frames (Value,Freeze): (1,0)(1,1)(0,1) → Result 0,1,1 (f1 no edge → init 0; f2 edge samples Value=1;
  //   f3 no edge → holds 1, ignoring Value=0). Proves edge-sampling, not level-tracking.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[3] = {1.0f, 1.0f, 0.0f};
    const float frz[3] = {0.0f, 1.0f, 1.0f};
    const float wR[3] = {0.0f, 1.0f, 1.0f};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("KeepBoolean", {{"Value", val[i]}, {"Freeze", frz[i]}, {"Mode", 1.0f}}, dt60, 0.0f, st, out);
      float wr = (injectBug && i == 2) ? 0.0f : wR[i];  // bug: re-samples Value every frozen frame
      bool pass = std::fabs(out[0] - wr) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] KeepBool.switch step%d R=%.1f want=%.1f -> %s\n", i + 1, out[0], wr, pass ? "PASS" : "FAIL");
    }
  }

  // ===== DampPeakDecay (TiXL floats/process/DampPeakDecay.cs) — snap UP, ease DOWN by Decay =====
  // Decay=0.5: Value 0,4,2,1 → Result 0,4,3,2.
  //   f1: 0>0? no → snap 0.            f2: 0>4? no → snap 4 (instant attack).
  //   f3: 4>2? yes → Lerp(4,2,0.5)=3.  f4: 3>1? yes → Lerp(3,1,0.5)=2.
  // f2 proves instant rise; f3/f4 prove the asymmetric ease-down. A symmetric damp would lag the rise.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float val[4] = {0.0f, 4.0f, 2.0f, 1.0f};
    const float want[4] = {0.0f, 4.0f, 3.0f, 2.0f};
    for (int i = 0; i < 4; ++i) {
      cookStatefulValueOp("DampPeakDecay", {{"Value", val[i]}, {"Decay", 0.5f}}, dt60, 0.0f, st, out);
      // injectBug on f2: a symmetric-damp bug would lag the rise (Lerp(0,4,0.5)=2 instead of snapping to 4).
      float w = (injectBug && i == 1) ? 2.0f : want[i];
      bool pass = std::fabs(out[0] - w) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] DampPeakDecay step%d(v=%.0f)=%.3f want=%.3f -> %s\n", i + 1, val[i], out[0], w, pass ? "PASS" : "FAIL");
    }
  }

  // ===== HasTimeChanged (TiXL anim/time/HasTimeChanged.cs) — time-edge detector, ADVANCING clock =====
  // Multi-frame: `time` MUST vary per cook (that is the input). lastTime inits 0. DeltaTime=time−lastTime,
  // wasAdvanced=time>lastTime+thr, wasRewind=time<lastTime−thr. The .t3 default Mode=DidChange(2).
  // (A) Mode=DidChange(2), Threshold=0 — advance, paused frame, rewind. HasChanged + DeltaTime asserted.
  //   f1 t=0    : adv 0>0? no, rew? no            → HC=0, DT=0      ; lastTime→0
  //   f2 t=0.5  : adv 0.5>0 yes                    → HC=1, DT=0.5    ; lastTime→0.5
  //   f3 t=0.5  : adv no, rew no (paused)          → HC=0, DT=0      ; lastTime→0.5  (proves a still frame)
  //   f4 t=1.0  : adv yes                          → HC=1, DT=0.5    ; lastTime→1.0
  //   f5 t=0.3  : rew 0.3<1.0 yes (DidChange)      → HC=1, DT=-0.7   ; lastTime→0.3  (DidChange catches rewind)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const float tm[5]  = {0.0f, 0.5f, 0.5f, 1.0f, 0.3f};
    const float wHC[5] = {0.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    const float wDT[5] = {0.0f, 0.5f, 0.0f, 0.5f, -0.7f};
    for (int i = 0; i < 5; ++i) {
      cookStatefulValueOp("HasTimeChanged", {{"Mode", 2.0f}, {"Threshold", 0.0f}, {"WhichTime", 1.0f}}, dt60, tm[i], st, out);
      // injectBug on f5: a no-rewind bug (DidChange treated as DidAdvanced) would miss the rewind → HC=0.
      float hc = (injectBug && i == 4) ? 0.0f : wHC[i];
      bool pass = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - wDT[i]) < eps;
      ok = ok && pass;
      printf("[selftest-statefulvalue] HasTimeChanged.change step%d(t=%.1f) HC=%.1f(want %.1f) DT=%.3f(want %.3f) -> %s\n",
             i + 1, tm[i], out[0], hc, out[1], wDT[i], pass ? "PASS" : "FAIL");
    }
  }
  // (B) Mode=DidAdvanced(1): a rewind frame must NOT fire (proves the mode split vs DidChange above).
  //   f1 t=0.5 : adv yes                 → HC=1, DT=0.5 ; lastTime→0.5
  //   f2 t=0.2 : adv 0.2>0.5? no (rewind)→ HC=0, DT=-0.3; lastTime→0.2   (DidAdvanced ignores the rewind)
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}}, dt60, 0.5f, st, out);
    bool p1 = std::fabs(out[0] - 1.0f) < eps && std::fabs(out[1] - 0.5f) < eps;
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}}, dt60, 0.2f, st, out);
    float hc = injectBug ? 1.0f : 0.0f;  // bug: DidAdvanced falsely fires on a rewind
    bool p2 = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - (-0.3f)) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.adv adv(t=0.5)=%.1f rewind(t=0.2)HC=%.1f(want %.1f) DT=%.3f -> %s\n",
           1.0f, out[0], hc, out[1], (p1 && p2) ? "PASS" : "FAIL");
  }
  // (C) Mode=DidRewind(0): the SAME rewind frame DOES fire (the mirror of (B)).
  //   f1 t=0.5 : rew no  → HC=0, DT=0.5 ; lastTime→0.5
  //   f2 t=0.2 : rew 0.2<0.5 yes → HC=1, DT=-0.3 ; lastTime→0.2
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 0.0f}}, dt60, 0.5f, st, out);
    bool p1 = std::fabs(out[0] - 0.0f) < eps;  // forward advance is NOT a rewind
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 0.0f}}, dt60, 0.2f, st, out);
    float hc = injectBug ? 0.0f : 1.0f;  // bug: DidRewind misses the rewind
    bool p2 = std::fabs(out[0] - hc) < eps && std::fabs(out[1] - (-0.3f)) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.rewind adv(t=0.5)HC=%.1f rewind(t=0.2)HC=%.1f(want %.1f) -> %s\n",
           0.0f, out[0], hc, (p1 && p2) ? "PASS" : "FAIL");
  }
  // (D) Threshold band, Mode=DidAdvanced(1), Threshold=0.25: a small advance UNDER threshold must NOT fire.
  //   f1 t=0.1 : adv 0.1>0+0.25(0.25)? no  → HC=0, DT=0.1 ; lastTime→0.1   (under-threshold advance)
  //   f2 t=0.5 : adv 0.5>0.1+0.25(0.35)? yes→ HC=1, DT=0.4 ; lastTime→0.5
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}, {"Threshold", 0.25f}}, dt60, 0.1f, st, out);
    float hc1 = injectBug ? 1.0f : 0.0f;  // bug: ignores Threshold → tiny advance falsely fires
    bool p1 = std::fabs(out[0] - hc1) < eps && std::fabs(out[1] - 0.1f) < eps;
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 1.0f}, {"Threshold", 0.25f}}, dt60, 0.5f, st, out);
    bool p2 = std::fabs(out[0] - 1.0f) < eps && std::fabs(out[1] - 0.4f) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.thresh small(t=0.1)HC=%.1f(want %.1f) big(t=0.5)HC=%.1f -> %s\n",
           out[0], hc1, 1.0f, (p1 && p2) ? "PASS" : "FAIL");
  }
  // (E) Mode=DidAdvancedWithMotionBlur(3), no __MotionBlurPass var (seam has no var-map) → DEGRADES to
  //   DidAdvanced: fires on advance, NOT on rewind. f1 t=0.4 adv → HC=1; f2 t=0.1 rewind → HC=0.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 3.0f}}, dt60, 0.4f, st, out);
    bool p1 = std::fabs(out[0] - 1.0f) < eps;
    cookStatefulValueOp("HasTimeChanged", {{"Mode", 3.0f}}, dt60, 0.1f, st, out);
    float hc = injectBug ? 1.0f : 0.0f;  // bug: mode 3 falsely behaves as DidChange (fires on rewind)
    bool p2 = std::fabs(out[0] - hc) < eps;
    ok = ok && p1 && p2;
    printf("[selftest-statefulvalue] HasTimeChanged.mb(varAbsent) adv(t=0.4)=%.1f rewind(t=0.1)HC=%.1f(want %.1f) -> %s\n",
           1.0f, out[0], hc, (p1 && p2) ? "PASS" : "FAIL");
  }

  // ===== StopWatch (TiXL anim/time/StopWatch.cs) — run-clock stopwatch, TRANSPORT-fed =====
  // Driven through an explicit TransportSnapshot (the playback-transport seam). The op reads the run
  // clock from tr.runTimeSecs (NOT dt/time), bpm/rate from tr. dt60/time args are inert for this op.
  auto trSnap = [](double runSecs, double bpm, double rate) {
    TransportSnapshot tr; tr.runTimeSecs = runSecs; tr.bpm = bpm; tr.rate = rate; return tr;
  };

  // ----- A: TimeInSecs Delta accumulation. No reset, rate=1, mode=TimeInSecs(0). _startTime stays 0,
  //   so Delta == runTime == Σdt. Drive runTime = 0.1, 0.2, .. 0.5 → Delta == that. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    bool pass = true;
    for (int i = 1; i <= 5; ++i) {
      const double runT = 0.1 * i;
      cookStatefulValueOp("StopWatch", {{"ResetTrigger", 0.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                          dt60, 0.0f, st, out, trSnap(runT, 120.0, 1.0));
      const float want = (float)runT;  // Delta = runTime - 0
      bool p = std::fabs(out[0] - want) < 1e-5f;
      pass = pass && p;
      printf("[selftest-statefulvalue] StopWatch.A.secs step%d(rt=%.1f) Delta=%.5f(want %.5f) -> %s\n",
             i, runT, out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  // ----- B: BeatTime mode, bpm=120. Delta == secs*120/240 == secs*0.5 bars. injectBug corrupts the
  //   bars constant (240→120) so the expected becomes secs*120/120==secs, which mismatches the correct
  //   secs*0.5 op output → Golden B flips RED. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    bool pass = true;
    for (int i = 1; i <= 4; ++i) {
      const double runT = 0.1 * i;
      cookStatefulValueOp("StopWatch", {{"ResetTrigger", 0.0f}, {"DurationIn", 1.0f}, {"PauseWithPlayback", 0.0f}},
                          dt60, 0.0f, st, out, trSnap(runT, 120.0, 1.0));
      const float wantGood = (float)(runT * 120.0 / 240.0);          // secs * 0.5 bars
      const float wantBug = (float)(runT * 120.0 / 120.0);           // 240→120 corruption (== secs)
      const float want = injectBug ? wantBug : wantGood;
      bool p = std::fabs(out[0] - want) < 1e-5f;
      pass = pass && p;
      printf("[selftest-statefulvalue] StopWatch.B.beat step%d(rt=%.1f) Delta=%.5f(want %.5f) -> %s\n",
             i, runT, out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  // ----- C: reset edge. Run rt=0.1,0.2,0.3 (no reset) → Delta=rt. Then rt=0.4 with ResetTrigger rising
  //   → LastDuration = 0.4-0 = 0.4 captured, baseline resets so Delta = 0.4-0.4 = 0. Hold ResetTrigger
  //   high at rt=0.5 (NO new edge, WasTriggered) → Delta = 0.5-0.4 = 0.1, LastDuration holds 0.4. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    bool pass = true;
    const double pre[3] = {0.1, 0.2, 0.3};
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("StopWatch", {{"ResetTrigger", 0.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                          dt60, 0.0f, st, out, trSnap(pre[i], 120.0, 1.0));
      bool p = std::fabs(out[0] - (float)pre[i]) < 1e-5f && std::fabs(out[1] - 0.0f) < 1e-5f;
      pass = pass && p;
    }
    // rising edge at rt=0.4
    cookStatefulValueOp("StopWatch", {{"ResetTrigger", 1.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                        dt60, 0.0f, st, out, trSnap(0.4, 120.0, 1.0));
    bool pEdge = std::fabs(out[1] - 0.4f) < 1e-5f && std::fabs(out[0] - 0.0f) < 1e-5f;  // LastDuration=0.4, Delta=0
    // held high at rt=0.5: no new edge → Delta = 0.5-0.4 = 0.1, LastDuration still 0.4
    cookStatefulValueOp("StopWatch", {{"ResetTrigger", 1.0f}, {"DurationIn", 0.0f}, {"PauseWithPlayback", 0.0f}},
                        dt60, 0.0f, st, out, trSnap(0.5, 120.0, 1.0));
    bool pHold = std::fabs(out[0] - 0.1f) < 1e-5f && std::fabs(out[1] - 0.4f) < 1e-5f;
    pass = pass && pEdge && pHold;
    ok = ok && pass;
    printf("[selftest-statefulvalue] StopWatch.C.reset edge(rt=0.4)LastDur=%.5f Delta=%.5f hold(rt=0.5)Delta=%.5f LastDur=%.5f -> %s\n",
           0.4f, 0.0f, out[0], out[1], pass ? "PASS" : "FAIL");
  }

  // ----- D: PauseWithPlayback, rate threading. PauseWithPlayback=true → Delta = _accumulatedDuration,
  //   which advances ONLY when rate != 0. rt=0.1,0.2 (rate=1): accum 0.1,0.2. rt=0.3,0.4 (rate=0):
  //   accum FROZEN at 0.2. Proves rate=0 freezes the accumulated clock (the pause-detect seam). -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    // rate=1 frames: accumulated grows
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.1, 120.0, 1.0));
    bool p1 = std::fabs(out[0] - 0.1f) < 1e-5f;
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.2, 120.0, 1.0));
    bool p2 = std::fabs(out[0] - 0.2f) < 1e-5f;
    // rate=0 frames: accumulated FROZEN at 0.2 (run clock still advances, accum does not)
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.3, 120.0, 0.0));
    bool p3 = std::fabs(out[0] - 0.2f) < 1e-5f;
    cookStatefulValueOp("StopWatch", {{"DurationIn", 0.0f}, {"PauseWithPlayback", 1.0f}},
                        dt60, 0.0f, st, out, trSnap(0.4, 120.0, 0.0));
    bool p4 = std::fabs(out[0] - 0.2f) < 1e-5f;
    bool pass = p1 && p2 && p3 && p4;
    ok = ok && pass;
    printf("[selftest-statefulvalue] StopWatch.D.pause rate1(0.1,0.2)=%.3f,%.3f rate0(0.3,0.4)frozen=%.3f,%.3f(want 0.200) -> %s\n",
           0.1f, 0.2f, 0.2f, out[0], pass ? "PASS" : "FAIL");
  }

  // ===== ConvertTime (TiXL anim/time/ConvertTime.cs) — bpm bars<->secs, transport-fed =====
  // BarsToSeconds(0) = time*240/bpm ; SecondsToBars(1) = time*bpm/240. The live bpm read is the point:
  // bpm=120 → B2S(1 bar)=2s, S2B(2s)=1 bar; bpm=240 → B2S(1 bar)=1s (proves it reads tr.bpm, not const).
  {
    StatefulValueState st;  // 0 state; reused, never written
    float out[3] = {0, 0, 0};
    cookStatefulValueOp("ConvertTime", {{"Time", 1.0f}, {"Mode", 0.0f}}, dt60, 0.0f, st, out, trSnap(0, 120.0, 1.0));
    // injectBug: corrupt the 240 constant to 120 → B2S(1)@120 becomes 1*240/240=... no; inject swaps to *bpm/240
    const float wantB2S120 = injectBug ? (float)(1.0 * 120.0 / 240.0) : 2.0f;  // good = 1*240/120 = 2
    bool pa = std::fabs(out[0] - wantB2S120) < 1e-5f;
    printf("[selftest-statefulvalue] ConvertTime.B2S bpm120 time=1 -> %.5f(want %.5f) -> %s\n", out[0], wantB2S120, pa ? "PASS" : "FAIL");

    cookStatefulValueOp("ConvertTime", {{"Time", 2.0f}, {"Mode", 1.0f}}, dt60, 0.0f, st, out, trSnap(0, 120.0, 1.0));
    bool pb = std::fabs(out[0] - 1.0f) < 1e-5f;  // S2B(2s)@120 = 2*120/240 = 1 bar
    printf("[selftest-statefulvalue] ConvertTime.S2B bpm120 time=2 -> %.5f(want 1.00000) -> %s\n", out[0], pb ? "PASS" : "FAIL");

    cookStatefulValueOp("ConvertTime", {{"Time", 1.0f}, {"Mode", 0.0f}}, dt60, 0.0f, st, out, trSnap(0, 240.0, 1.0));
    bool pc = std::fabs(out[0] - 1.0f) < 1e-5f;  // B2S(1 bar)@240 = 1*240/240 = 1s — live-bpm proof
    printf("[selftest-statefulvalue] ConvertTime.B2S bpm240 time=1 -> %.5f(want 1.00000, live-bpm proof) -> %s\n", out[0], pc ? "PASS" : "FAIL");

    bool pass = pa && pb && pc;
    ok = ok && pass;
  }

  // ===== RunTime (TiXL anim/time/RunTime.cs) — TimeInSeconds = Playback.RunTimeInSecs (wall run clock) =====
  // Drive the run clock 0.5/1.0/1.5 (frame_cook accumulates dt) → out exactly tracks it, independent of
  // scrub/pause (rate=0, playhead frozen at bars=9.9 — RunTime ignores all of it). R-1 origin fork.
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    const double runs[3] = {0.5, 1.0, 1.5};
    bool pass = true;
    for (int i = 0; i < 3; ++i) {
      TransportSnapshot tr = trSnap(runs[i], 120.0, 0.0);  // rate=0 (paused), playhead irrelevant
      tr.localTimeBars = 9.9; tr.playbackTimeBars = 9.9;   // frozen playhead — RunTime must ignore it
      cookStatefulValueOp("RunTime", {}, dt60, 0.0f, st, out, tr);
      const float want = injectBug ? (float)runs[i] + 0.01f : (float)runs[i];
      bool p = std::fabs(out[0] - want) < 1e-5f;
      pass = pass && p;
      printf("[selftest-statefulvalue] RunTime step%d(runClk=%.1f,paused) -> %.5f(want %.5f) -> %s\n",
             i + 1, runs[i], out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }
  ok = ok && (runTransportTime2SelfTest(injectBug) == 0);  // ClipTime/LastFrameDuration/GetBpm (helper TU)
  // ===== DelayTriggerChange (TiXL bool/process/DelayTriggerChange.cs:30-95) — two-edge change detector =====
  // currentTime is fed via the chosen TimeMode (AppRunTime/LocalFxTime); we drive tr to advance it.
  // Helper: cook one frame at a given run-clock with a given trigger.
  auto dtc = [&](StatefulValueState& st, float out[3], bool trig, double runClk, float dur,
                 int mode, int timeMode, double bpm) {
    TransportSnapshot tr = trSnap(runClk, bpm, 1.0);
    tr.localFxTimeBars = runClk; tr.localTimeBars = runClk; tr.playbackTimeBars = runClk;  // for bars/secs modes
    cookStatefulValueOp("DelayTriggerChange",
                        {{"Trigger", trig ? 1.0f : 0.0f}, {"DelayDuration", dur},
                         {"Mode", (float)mode}, {"TimeMode", (float)timeMode}},
                        dt60, 0.0f, st, out, tr);
  };

  // ----- A: DelayTrue, dur=1, AppRunTime(6). FAITHFUL first-second: s0 init=0 so before any edge
  //   remainingTime = 0 - currentTime + 1 > 0 while currentTime<1 → delayed=true even with Trigger=false.
  //   Then rising edge at t=2 (Trigger true): _lastTrueTime=2, remaining = 2-2+1 = 1>0 → delayed=true,
  //   passthrough is also true. At t=2.5 hold trigger: remaining = 2-2.5+1 = 0.5>0 → true. At t=3.5:
  //   remaining = 2-3.5+1 = -0.5 → not delayed → passthrough _triggered(true) → still true (held high). -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    dtc(st, out, false, 0.5, 1.0f, 0, 6, 120.0);  // t=0.5, Trigger=false
    bool pFaithful = (out[0] > 0.5f) && std::fabs(out[1] - 0.5f) < 1e-5f;  // delayed=true(faithful), remaining=0-0.5+1=0.5
    printf("[selftest-statefulvalue] DTC.A faithful t=0.5 Trig=F -> delayed=%.0f(want 1) remain=%.5f(want 0.50000) -> %s\n",
           out[0], out[1], pFaithful ? "PASS" : "FAIL");
    dtc(st, out, false, 1.5, 1.0f, 0, 6, 120.0);  // t=1.5, still false: remaining=0-1.5+1=-0.5<0 → passthrough false
    bool pAfter = (out[0] < 0.5f) && std::fabs(out[1] - (-0.5f)) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.A faithful-end t=1.5 Trig=F -> delayed=%.0f(want 0) remain=%.5f(want -0.50000) -> %s\n",
           out[0], out[1], pAfter ? "PASS" : "FAIL");
    dtc(st, out, true, 2.0, 1.0f, 0, 6, 120.0);  // rising edge t=2: lastTrue=2, remaining=2-2+1=1 → delayed true
    const float wantRemA = injectBug ? 0.0f : 1.0f;  // injectBug: drop +delayDuration → remaining=0
    bool pRise = (out[0] > 0.5f) && std::fabs(out[1] - wantRemA) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.A rise t=2 Trig=T -> delayed=%.0f(want 1) remain=%.5f(want %.5f) -> %s\n",
           out[0], out[1], wantRemA, pRise ? "PASS" : "FAIL");
    // t=3.5 held true: _lastTrueTime UPDATES every frame while triggered (cs:55-58 runs unconditionally
    // inside if(isTriggered), not just on the edge), so refTime=3.5, remaining=3.5-3.5+1=1>0 → delayed=true.
    dtc(st, out, true, 3.5, 1.0f, 0, 6, 120.0);
    bool pHold = (out[0] > 0.5f) && std::fabs(out[1] - 1.0f) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.A hold t=3.5 Trig=T -> delayed=%.0f(want 1) remain=%.5f(want 1.00000, lastTrue tracks) -> %s\n",
           out[0], out[1], pHold ? "PASS" : "FAIL");
    ok = ok && pFaithful && pAfter && pRise && pHold;
  }

  // ----- B: DelayFalse(1), dur=1, AppRunTime(6). stateIfDelayed=false, refTime=_lastFalseTime. Start
  //   Trigger=TRUE at t=0.5: no _lastFalseTime yet (s1=0), remaining=0-0.5+1=0.5>0 → delayed=stateIfDelayed=FALSE
  //   (DelayFalse holds the OFF state into the trigger). Drop to false (edge) at t=1.0: lastFalse=1,
  //   remaining=1-1+1=1>0 → delayed=false. At t=2.5: remaining=1-2.5+1=-0.5 → passthrough _triggered(false)=false.
  //   To see a TRUE we keep trigger high long enough: re-rise t=3 then t=5 (remaining=lastFalse(1)-5+1<0) → passthrough true. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    dtc(st, out, true, 0.5, 1.0f, 1, 6, 120.0);   // Trigger=T but DelayFalse holds false (remaining=0.5>0)
    bool p1 = (out[0] < 0.5f);
    dtc(st, out, false, 1.0, 1.0f, 1, 6, 120.0);  // edge to false: lastFalse=1, remaining=1-1+1=1>0 → false
    bool p2 = (out[0] < 0.5f) && std::fabs(out[1] - 1.0f) < 1e-5f;
    dtc(st, out, true, 5.0, 1.0f, 1, 6, 120.0);   // edge to true t=5: refTime=lastFalse=1, remaining=1-5+1=-3<0 → passthrough true
    const float wantB = injectBug ? 0.0f : 1.0f;  // injectBug breaks passthrough → 0
    bool p3 = std::fabs(out[0] - wantB) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.B DelayFalse: holdFalse@T=%.0f edgeFalse remain=%.5f passthroughTrue@t5=%.0f(want %.0f) -> %s\n",
           p1 ? 0.0f : 1.0f, out[1], out[0], wantB, (p1 && p2 && p3) ? "PASS" : "FAIL");
    ok = ok && p1 && p2 && p3;
  }

  // ----- C: bpm bars<->secs conversion of currentTime via TimeMode=1 (LocalFxTime_InSecs). DelayTrue, dur=1.
  //   We set localFxTimeBars=2 (bars), bpm=120 → currentTime = 2*240/120 = 4 secs. Rising edge there:
  //   lastTrue=4, remaining = 4-4+1 = 1 > 0 → delayed=true. Proves the bars→secs path uses tr.bpm. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    // prime _triggered=false at currentTime far back, then rising edge with the bars-secs time.
    {
      TransportSnapshot tr = trSnap(0, 120.0, 1.0); tr.localFxTimeBars = 0;
      cookStatefulValueOp("DelayTriggerChange",
                          {{"Trigger", 0.0f}, {"DelayDuration", 1.0f}, {"Mode", 0.0f}, {"TimeMode", 1.0f}},
                          dt60, 0.0f, st, out, tr);
    }
    TransportSnapshot tr = trSnap(0, 120.0, 1.0); tr.localFxTimeBars = 2.0;  // 2 bars @120 = 4 secs
    cookStatefulValueOp("DelayTriggerChange",
                        {{"Trigger", 1.0f}, {"DelayDuration", 1.0f}, {"Mode", 0.0f}, {"TimeMode", 1.0f}},
                        dt60, 0.0f, st, out, tr);
    const float wantRemC = injectBug ? (float)(2.0 * 120.0 / 240.0 - 0.0 + 1.0) : 1.0f;  // good: currentTime=4 → remaining=1; bug: wrong bars*bpm/240=1+1=2... distinct
    bool pc = (out[0] > 0.5f) && std::fabs(out[1] - wantRemC) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.C LocalFxSecs 2bars@120=4s rising -> delayed=%.0f(want 1) remain=%.5f(want %.5f) -> %s\n",
           out[0], out[1], wantRemC, pc ? "PASS" : "FAIL");
    ok = ok && pc;
  }

  // ----- D: DelayBoth(2), dur=1, AppRunTime(6). Reconstruct held _stateBeforeChange across an edge.
  //   DelayBoth: refTime=_lastChangeTime, stateIfDelayed=_stateBeforeChange (= the DelayedTrigger value
  //   just BEFORE this edge, carried via s[5]). Sequence:
  //   t=0.5 Trig=F: first frame, no edge (prev _triggered=0 == false → hasBeenChanged=false). delayed:
  //     refTime=_lastChangeTime=0, remaining=0-0.5+1=0.5>0 → stateIfDelayed=_stateBeforeChange=0 → false.
  //   t=1.0 Trig=T (EDGE): _stateBeforeChange = prior DelayedTrigger (false=0). lastChange=1.
  //     remaining=1-1+1=1>0 → delayed=stateIfDelayed=false (holds the PRE-edge state). passthrough would be true,
  //     but delayed wins → STILL FALSE (this is the "hold previous state during delay" behavior).
  //   t=2.5 Trig=T: no edge, remaining=1-2.5+1=-0.5<0 → not delayed → passthrough _triggered(true) → TRUE. -----
  {
    StatefulValueState st;
    float out[3] = {0, 0, 0};
    dtc(st, out, false, 0.5, 1.0f, 2, 6, 120.0);  // no edge, delayed holds stateBeforeChange=0 → false
    bool p1 = (out[0] < 0.5f);
    dtc(st, out, true, 1.0, 1.0f, 2, 6, 120.0);   // EDGE: stateBeforeChange=prior false → delayed holds false despite Trigger=T
    bool p2 = (out[0] < 0.5f) && std::fabs(out[1] - 1.0f) < 1e-5f;
    dtc(st, out, true, 2.5, 1.0f, 2, 6, 120.0);   // delay elapsed → passthrough true
    const float wantD = injectBug ? 0.0f : 1.0f;
    bool p3 = std::fabs(out[0] - wantD) < 1e-5f;
    printf("[selftest-statefulvalue] DTC.D DelayBoth: preEdge=%.0f edge-holds-prev(false)=%.0f remain=%.5f postDelay=%.0f(want %.0f) -> %s\n",
           p1 ? 0.0f : 1.0f, p2 ? 0.0f : 1.0f, out[1], out[0], wantD, (p1 && p2 && p3) ? "PASS" : "FAIL");
    ok = ok && p1 && p2 && p3;
  }

  printf("[selftest-statefulvalue] %s%s\n", ok ? "PASS" : "FAIL", injectBug ? " (injectBug)" : "");
  return ok ? 0 : 1;
}

}  // namespace sw
