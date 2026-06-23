// runtime/stateful_value_ops_selftest_transport2 — golden for the transport-time exposures
// ClipTime / LastFrameDuration / GetBpm (Phase C numbers/anim mining). Split out of the
// grandfathered stateful_value_ops_selftest.cpp monolith so that file does not grow past its
// line-count ratchet (ARCHITECTURE rule 4 / RATCHET only-decrease). Called from
// runStatefulValueSelfTest (one line) under --selftest-statefulvalue; injectBug = the --bite tooth.
//
// These 3 ops have NO pure evaluate() — their value depends on the per-frame TransportSnapshot the
// `in`-map cannot carry (same reason as RunTime/ConvertTime), so they are cooked via the public
// cookStatefulValueOp API and driven here through explicit snapshots.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"  // cookStatefulValueOp + TransportSnapshot/StatefulValueState

namespace sw {

int runTransportTime2SelfTest(bool injectBug) {
  bool ok = true;
  const float dt60 = 1.0f / 60.0f;
  auto trSnap = [](double runSecs, double bpm, double rate) {
    TransportSnapshot tr; tr.runTimeSecs = runSecs; tr.bpm = bpm; tr.rate = rate; return tr;
  };

  // ===== ClipTime (TiXL anim/time/ClipTime.cs) — Time = context.LocalTime (playhead, SECONDS) =====
  // The playhead is carried in BARS (tr.localTimeBars); ClipTime exposes it in SECONDS = bars*240/bpm.
  // localTimeBars=1 bar @ bpm=120 → 1*240/120 = 2s ; @ bpm=240 → 1*240/240 = 1s (live-bpm proof).
  // 0 state. injectBug corrupts the 240 constant → wrong seconds.
  {
    StatefulValueState st;  // 0 state
    float out[3] = {0, 0, 0};
    TransportSnapshot tr120 = trSnap(0.0, 120.0, 1.0); tr120.localTimeBars = 1.0;
    cookStatefulValueOp("ClipTime", {}, dt60, 0.0f, st, out, tr120);
    const float wantClip120 = injectBug ? (float)(1.0 * 120.0 / 240.0) : 2.0f;  // good = 1*240/120 = 2s
    bool pa = std::fabs(out[0] - wantClip120) < 1e-5f;
    printf("[selftest-statefulvalue] ClipTime bars=1 bpm120 -> %.5f(want %.5f) -> %s\n",
           out[0], wantClip120, pa ? "PASS" : "FAIL");

    TransportSnapshot tr240 = trSnap(0.0, 240.0, 1.0); tr240.localTimeBars = 1.0;
    cookStatefulValueOp("ClipTime", {}, dt60, 0.0f, st, out, tr240);
    bool pb = std::fabs(out[0] - 1.0f) < 1e-5f;  // 1*240/240 = 1s — live-bpm proof
    printf("[selftest-statefulvalue] ClipTime bars=1 bpm240 -> %.5f(want 1.00000, live-bpm proof) -> %s\n",
           out[0], pb ? "PASS" : "FAIL");

    // bars=0 → 0s (origin).
    TransportSnapshot tr0 = trSnap(0.0, 120.0, 1.0); tr0.localTimeBars = 0.0;
    cookStatefulValueOp("ClipTime", {}, dt60, 0.0f, st, out, tr0);
    bool pc = std::fabs(out[0] - 0.0f) < 1e-5f;
    printf("[selftest-statefulvalue] ClipTime bars=0 -> %.5f(want 0.00000) -> %s\n",
           out[0], pc ? "PASS" : "FAIL");

    ok = ok && pa && pb && pc;
  }

  // ===== LastFrameDuration (TiXL anim/time/LastFrameDuration.cs) — Duration = Playback.LastFrameDuration
  // The seam hands the RAW wall frame delta in seconds as `dt`. Duration == dt exactly. Drive dt = 1/60,
  // 1/30, 0.25 → Duration tracks it (independent of bpm/rate/playhead). injectBug offsets the want.
  {
    StatefulValueState st;  // 0 state
    float out[3] = {0, 0, 0};
    const float dts[3] = {1.0f / 60.0f, 1.0f / 30.0f, 0.25f};
    bool pass = true;
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("LastFrameDuration", {}, dts[i], 0.0f, st, out, trSnap(0.0, 120.0, 1.0));
      const float want = injectBug ? dts[i] + 0.01f : dts[i];
      bool p = std::fabs(out[0] - want) < 1e-6f;
      pass = pass && p;
      printf("[selftest-statefulvalue] LastFrameDuration dt=%.5f -> %.5f(want %.5f) -> %s\n",
             dts[i], out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  // ===== GetBpm (TiXL anim/vj/GetBpm.cs) — Result = Playback.Current.Bpm (the live tempo) =====
  // Pure exposure of tr.bpm. Drive bpm=120/140/240 → Result tracks it (independent of dt/playhead).
  // injectBug offsets the want.
  {
    StatefulValueState st;  // 0 state
    float out[3] = {0, 0, 0};
    const double bpms[3] = {120.0, 140.0, 240.0};
    bool pass = true;
    for (int i = 0; i < 3; ++i) {
      cookStatefulValueOp("GetBpm", {}, dt60, 0.0f, st, out, trSnap(0.0, bpms[i], 1.0));
      const float want = injectBug ? (float)bpms[i] + 1.0f : (float)bpms[i];
      bool p = std::fabs(out[0] - want) < 1e-4f;
      pass = pass && p;
      printf("[selftest-statefulvalue] GetBpm bpm=%.1f -> %.5f(want %.5f) -> %s\n",
             bpms[i], out[0], want, p ? "PASS" : "FAIL");
    }
    ok = ok && pass;
  }

  return ok ? 0 : 1;
}

}  // namespace sw
