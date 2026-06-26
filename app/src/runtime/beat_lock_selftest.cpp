// runtime/beat_lock_selftest — G2+G3 golden for the sliding_average orphan接上 (--selftest-beatlock).
//
//   G2: SlidingAverage<10> de-jitter — a fixed value sequence, hand-computed 10-slot rolling mean.
//       Pure arithmetic, no audio; proves the ring buffer the audio-lock branch leans on is correct.
//   G3: beat_timing audio-lock integration — enable EnableAudioBeatLocking + resync, drive a stable
//       BeatSynchronizer.BarProgress stream, and assert beatTimingBeatTime() == the SlidingAverage<10>
//       smoothed BarProgress (+0 offset). The -bug variant expects RAW (unsmoothed) BarProgress so it
//       RED-flags the moment the average is bypassed — i.e. the orphan being disconnected again.
//       Plus the禁改區 guard: with locking OFF the manual path is byte-for-byte unchanged.
#include "runtime/beat_synchronizer.h"
#include "runtime/beat_timing.h"
#include "runtime/sliding_average.h"

#include <cmath>
#include <cstdio>

namespace sw {
using namespace sw::runtime;  // beatTiming* / beatSync* live in sw::runtime; pull them in here.
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; std::printf("  [beatlock] FAIL %s\n", what); }
  else std::printf("  [beatlock] ok   %s\n", what);
}
void expectNear(const char* what, double got, double want, double eps) {
  bool ok = std::abs(got - want) <= eps;
  if (!ok) { ++g_fail; std::printf("  [beatlock] FAIL %s got=%.8f want=%.8f\n", what, got, want); }
  else std::printf("  [beatlock] ok   %s = %.8f\n", what, got);
}

}  // namespace

int runBeatLockSelfTest(bool injectBug) {
  g_fail = 0;
  std::printf("[selftest] beatlock (sliding_average orphan接上 audio-lock)\n");

  // ===== G2: SlidingAverage<10> de-jitter — hand-computed rolling mean. =====
  {
    SlidingAverage<10> avg;
    expect("G2: empty mean is 0", avg.mean() == 0.0f);

    // Push 1..10: after each push the mean is the average of all values so far.
    // sum after k pushes (values 1..k) = k(k+1)/2 ; mean = (k+1)/2.
    for (int k = 1; k <= 10; ++k) {
      float m = avg.pushAndMean((float)k);
      expectNear("G2: rolling mean of 1..k while filling", m, (k + 1) / 2.0, 1e-6);
    }
    // Buffer full (10 slots = values 1..10). mean = 55/10 = 5.5.
    expectNear("G2: full-buffer mean of 1..10", avg.mean(), 5.5, 1e-6);

    // Push 11: evicts 1, buffer = 2..11. sum = 65, mean = 6.5.
    expectNear("G2: after evicting oldest, mean of 2..11", avg.pushAndMean(11.0f), 6.5, 1e-6);
    // Push 12: evicts 2, buffer = 3..12. sum = 75, mean = 7.5.
    expectNear("G2: after another evict, mean of 3..12", avg.pushAndMean(12.0f), 7.5, 1e-6);

    // A constant stream collapses to that constant (de-jitter: noise around a mean → the mean).
    SlidingAverage<10> flat;
    for (int i = 0; i < 25; ++i) flat.push(3.0f);
    expectNear("G2: constant stream → constant mean (jitter killed)", flat.mean(), 3.0, 1e-6);
  }

  // ===== G3: beat_timing audio-lock integration. =====
  {
    // --- 禁改區 guard FIRST: with locking OFF the manual path is unchanged. ---
    beatTimingResetForTest();
    expect("G3: lock defaults OFF", !beatTimingAudioBeatLockingEnabled());
    // 120 BPM default, run a frame at t=1.0s with no taps: pure manual advance.
    beatTimingUpdate(1.0);
    // Manual path: measureDuration = 0.5*16 = 8.0; tInMeasure = (1.0-0)/8.0 = 0.125;
    // BeatTime = (0 + 0.125 + 0) * 4 = 0.5. This MUST be exactly the original manual value.
    expectNear("G3: locking-OFF manual BeatTime unchanged (禁改區 intact)", beatTimingBeatTime(), 0.5, 1e-9);

    // --- now the locked path. ---
    beatTimingResetForTest();
    beatTimingSetAudioBeatLocking(true);
    // Trigger a measure-resync so beat_timing calls beatSyncResync(bpm) and sets _resynced.
    beatTimingTriggerResyncMeasure();
    beatTimingUpdate(0.0);  // processes the deferred resync flag → s_resynced = true
    expect("G3: audio-lock branch is now active (resynced + enabled)", beatTimingAudioBeatLockingEnabled());

    // Reference: replay the EXACT BarProgress sequence beat_timing's internal s_barTimeAverage will
    // see, through our own SlidingAverage<10> — beatTimingBeatTime() must equal this each frame.
    // The resync frame above ALREADY pushed one sample (BarProgress==0 at that instant) into
    // beat_timing's average, so we prime ref with the same 0 to stay in lock-step.
    SlidingAverage<10> ref;
    ref.push(0.0f);  // mirror the resync-frame push beat_timing made at t=0
    // Re-resync the synchronizer to a clean anchor (beat_timing already did one at t=0; pin BPM).
    beatSyncResync(120.0);

    // Feed a steady bass-onset stream so BeatSynchronizer.BarProgress advances deterministically,
    // and step beat_timing each frame. dt = 100ms.
    SpectrumSnapshot bassHit{};
    for (int b = 0; b <= 7; ++b) bassHit.onsets[b] = 1.0f;
    SpectrumSnapshot silence{};

    double t = 0.0;
    bool tracked = true;
    bool everNonZero = false;
    bool smoothingEverMattered = false;  // proves the average isn't a no-op (mean ≠ raw at least once)
    for (int frame = 0; frame < 40; ++frame) {
      t += 0.1;
      // hit every 5th frame (>=50ms apart) so onsets clear the pause gate; silence between.
      beatSyncUpdate((frame % 5) == 0 ? bassHit : silence, t, 0.1);
      double barProgress = beatSyncBarProgress();
      double refMean = ref.pushAndMean((float)barProgress);  // what beat_timing's average holds
      beatTimingUpdate(t);
      double got = beatTimingBeatTime();
      // ONE faithful expectation: BeatTime == the SlidingAverage<10> SMOOTHED BarProgress.
      // injectBug expects RAW (unsmoothed) BarProgress instead; since the stream rises monotonically
      // the running mean lags the raw value, so the production (smoothed) BeatTime DIVERGES from the
      // raw expectation → this assertion FAILs (RED). That's the tooth: the orphan being bypassed.
      double want = injectBug ? barProgress : refMean;
      if (std::abs(got - want) > 1e-4) tracked = false;
      if (barProgress > 1e-6) everNonZero = true;
      if (std::abs(refMean - barProgress) > 1e-4) smoothingEverMattered = true;
    }
    expect("G3: BarProgress actually advanced (stream drove the synchronizer)", everNonZero);
    expect("G3: the SlidingAverage<10> actually smoothed (mean lagged raw at least once)",
           smoothingEverMattered);
    expect("G3: BeatTime == SlidingAverage<10> smoothed BarProgress (orphan接通; -bug expects raw → RED)",
           tracked);
  }

  beatTimingResetForTest();  // leave global state clean for any later test in the same process.
  std::printf("[selftest] beatlock %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw
