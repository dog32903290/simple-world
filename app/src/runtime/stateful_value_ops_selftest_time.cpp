// runtime/stateful_value_ops_selftest_time — golden for Time and GetFrameSpeedFactor
// (transport-YELLOW consumers, Phase C numbers/anim mining). Called from runStatefulValueSelfTest
// (one line) under --selftest-statefulvalue; injectBug = the --bite tooth.
//
// Both ops have NO pure evaluate() — their value depends on the per-frame TransportSnapshot the
// `in`-map cannot carry (same pattern as ClipTime/GetBpm/ConvertTime). Cooked via the public
// cookStatefulValueOp API and driven through explicit TransportSnapshot fixtures.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"  // cookStatefulValueOp + TransportSnapshot/StatefulValueState

namespace sw {

int runTimeSelfTest(bool injectBug) {
  bool ok = true;
  const float dt60 = 1.0f / 60.0f;

  // Helper: build a TransportSnapshot with full fields set.
  // Mode 0 LocalIdleMotionFxTime reads localFxTimeBars; Mode 1 LocalTime reads localTimeBars;
  // Mode 2 PlaybackTime reads playbackTimeBars; Mode 3 Runtime reads runTimeSecs*bpm/240;
  // Mode 4 Frozen → 0.
  auto mkTr = [](double runSecs, double localTimeBars, double localFxTimeBars,
                 double playbackTimeBars, double bpm, double rate = 1.0) {
    TransportSnapshot tr;
    tr.runTimeSecs       = runSecs;
    tr.localTimeBars     = localTimeBars;
    tr.localFxTimeBars   = localFxTimeBars;
    tr.playbackTimeBars  = playbackTimeBars;
    tr.bpm               = bpm;
    tr.rate              = rate;
    tr.frameSpeedFactor  = 1.0;
    return tr;
  };

  // ===== Time (TiXL numbers/anim/time/Time.cs) =====
  // Non-trivial probe: localFxTimeBars=2.5 @ bpm=120, SpeedFactor=1.0, Mode=0, Units=0 (Bars)
  //   → out = localFxTimeBars * speedFactor = 2.5 * 1.0 = 2.5
  // injectBug: apply wrong bars→secs conversion to corrupt the Bars-unit result.
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 1.0, 2.5, 3.0, 120.0);
    cookStatefulValueOp("Time", {{"Mode", 0.0f}, {"SpeedFactor", 1.0f}, {"Units", 0.0f}},
                        dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 2.5f * 240.0f / 120.0f  // bug: convert-to-secs even when Units=Bars
                                 : 2.5f;
    bool pa = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] Time.mode0.bars fxTimeBars=2.5 bpm120 SpF=1 -> %.5f(want %.5f) -> %s\n",
           out[0], want, pa ? "PASS" : "FAIL");
    ok = ok && pa;
  }

  // Mode=0 (LocalIdleMotionFxTime), Units=0 (Bars), SpeedFactor=2.0
  //   → out = localFxTimeBars * speedFactor = 1.5 * 2.0 = 3.0
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 0.0, 1.5, 0.0, 120.0);
    cookStatefulValueOp("Time", {{"Mode", 0.0f}, {"SpeedFactor", 2.0f}, {"Units", 0.0f}},
                        dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 1.5f : 3.0f;  // bug: ignores speedFactor
    bool pb = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] Time.mode0.speedfactor2 fxTimeBars=1.5 -> %.5f(want %.5f) -> %s\n",
           out[0], want, pb ? "PASS" : "FAIL");
    ok = ok && pb;
  }

  // Mode=1 (LocalTime), Units=0 (Bars), SpeedFactor=1.0
  //   → out = localTimeBars * 1.0 = 4.0
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 4.0, 99.0, 99.0, 120.0);  // localFxTimeBars≠localTimeBars
    cookStatefulValueOp("Time", {{"Mode", 1.0f}, {"SpeedFactor", 1.0f}, {"Units", 0.0f}},
                        dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 99.0f : 4.0f;  // bug: reads localFxTimeBars instead
    bool pc = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] Time.mode1.localtime timeBars=4 fxBars=99 -> %.5f(want %.5f) -> %s\n",
           out[0], want, pc ? "PASS" : "FAIL");
    ok = ok && pc;
  }

  // Mode=2 (PlaybackTime), Units=1 (Secs), SpeedFactor=1.0, bpm=120
  //   → time_bars = playbackTimeBars * 1.0 = 2.0
  //   → Secs: time_bars * 240 / bpm = 2.0 * 240 / 120 = 4.0
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 0.0, 0.0, 2.0, 120.0);
    cookStatefulValueOp("Time", {{"Mode", 2.0f}, {"SpeedFactor", 1.0f}, {"Units", 1.0f}},
                        dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 2.0f : 4.0f;  // bug: forgets SecondsFromBars, returns raw bars
    bool pd = std::fabs(out[0] - want) < 1e-4f;
    printf("[selftest-statefulvalue] Time.mode2.secs playbackBars=2 bpm120 -> %.5f(want %.5f) -> %s\n",
           out[0], want, pd ? "PASS" : "FAIL");
    ok = ok && pd;
  }

  // Mode=3 (Runtime = BarsFromSeconds(RunTimeInSecs)), Units=0 (Bars), SpeedFactor=1.0, bpm=120
  //   → time_bars = runTimeSecs * bpm / 240 = 3.0 * 120 / 240 = 1.5
  //   → out = 1.5 * 1.0 = 1.5
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(3.0, 0.0, 0.0, 0.0, 120.0);
    cookStatefulValueOp("Time", {{"Mode", 3.0f}, {"SpeedFactor", 1.0f}, {"Units", 0.0f}},
                        dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 3.0f : 1.5f;  // bug: returns raw runTimeSecs instead of BarsFromSeconds
    bool pe = std::fabs(out[0] - want) < 1e-4f;
    printf("[selftest-statefulvalue] Time.mode3.runtime runSecs=3 bpm120 -> %.5f(want %.5f) -> %s\n",
           out[0], want, pe ? "PASS" : "FAIL");
    ok = ok && pe;
  }

  // Mode=4 (Frozen) — regardless of all transport state, output is 0.
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(99.0, 99.0, 99.0, 99.0, 240.0);
    cookStatefulValueOp("Time", {{"Mode", 4.0f}, {"SpeedFactor", 3.0f}, {"Units", 0.0f}},
                        dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 99.0f * 3.0f : 0.0f;  // bug: applies speedFactor to non-frozen fxTime
    bool pf = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] Time.mode4.frozen -> %.5f(want %.5f) -> %s\n",
           out[0], want, pf ? "PASS" : "FAIL");
    ok = ok && pf;
  }

  // ===== GetFrameSpeedFactor (TiXL numbers/anim/time/GetFrameSpeedFactor.cs) =====
  // In simple_world, tr.frameSpeedFactor is always 1.0 (interactive, no render-to-file).
  // Test isValid path (|val|>0.0001 → pass through) and fallback (|val|≤0.0001 → return 1.0).
  // Non-trivial probe: frameSpeedFactor = 0.5 (a hypothetical 30fps render with 60fps ref)
  //   → isValid (0.5 > 0.0001) → out = 0.5
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 0.0, 0.0, 0.0, 120.0);
    tr.frameSpeedFactor = 0.5;
    cookStatefulValueOp("GetFrameSpeedFactor", {}, dt60, 0.0f, st, out, tr);
    const float want = injectBug ? 1.0f : 0.5f;  // bug: returns the fallback 1.0 even when valid
    bool pg = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] GetFrameSpeedFactor fsf=0.5 -> %.5f(want %.5f) -> %s\n",
           out[0], want, pg ? "PASS" : "FAIL");
    ok = ok && pg;
  }

  // isValid fallback: frameSpeedFactor = 0.00005 (below 0.0001 threshold) → out = 1.0
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 0.0, 0.0, 0.0, 120.0);
    tr.frameSpeedFactor = 0.00005;
    cookStatefulValueOp("GetFrameSpeedFactor", {}, dt60, 0.0f, st, out, tr);
    const float want = 1.0f;  // bug and non-bug both give 1.0 here (threshold branch)
    bool ph = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] GetFrameSpeedFactor fsf=0.00005(below-threshold) -> %.5f(want 1.0) -> %s\n",
           out[0], ph ? "PASS" : "FAIL");
    ok = ok && ph;
  }

  // Standard interactive case: frameSpeedFactor = 1.0 → out = 1.0
  {
    StatefulValueState st;
    float out[8] = {};
    TransportSnapshot tr = mkTr(0.0, 0.0, 0.0, 0.0, 120.0);
    tr.frameSpeedFactor = 1.0;
    cookStatefulValueOp("GetFrameSpeedFactor", {}, dt60, 0.0f, st, out, tr);
    const float want = 1.0f;
    bool pi = std::fabs(out[0] - want) < 1e-5f;
    printf("[selftest-statefulvalue] GetFrameSpeedFactor fsf=1.0(interactive) -> %.5f(want 1.0) -> %s\n",
           out[0], pi ? "PASS" : "FAIL");
    ok = ok && pi;
  }

  return ok ? 0 : 1;
}

}  // namespace sw
