// io_video_timing_golden — PlayVideo / PlayVideoClip TIMING-CORE golden (--selftest-io-video-timing).
//
// Layer (b) of the io/video family: FILE PLAYBACK. The native decode (AVFoundation AVAssetReader →
// MTL::Texture) is a deferred platform seam; what this golden verifies is the CLOSED-FORM per-frame
// timing math that decides seek/play — the frame-accurate-deterministic承重 for MV export. Every
// expected value is hand-derived from the TiXL .cs (line-cited), independent of any decoder.
//
// TiXL authority:
//   PlayVideo.cs (PlaybackController.HandleGettingFrames)  — quantize / loop-clamp / seek / complete
//   PlayVideoClip.cs (Update)                              — timeclip bars→secs mapping / play gate
//
// injectBug drives runtime/video_playback.h's setVideoPlaybackBug (a real module switch that corrupts
// ONE cook term). It is NOT a want-flip: the WANT stays pinned to the TiXL value; the CODE path is
// what changes. did-not-trip → return 0 (GOLDEN_STANDARD P1: NO-BITE list surfaces a dead tooth).
#include <cmath>
#include <cstdio>

#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS
#include "runtime/video_playback.h"

namespace sw {

namespace {
bool approx(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
}  // namespace

int runIoVideoTimingSelfTest(bool injectBug) {
  using namespace sw::video;
  bool ok = true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1. PlayVideo frame quantization (PlayVideo.cs:126). requestedTime = floor(t*60)/60.
  //     Probe at a DIVERGING MIDDLE: 0.512s is inside frame floor(30.72)=30 → 30/60 = 0.5, NOT 0.512.
  //     A dropped snap (bug 1) returns 0.512 → RED. (Two nearby wall times snapping to the SAME frame
  //     is the determinism guarantee; here 0.500 and 0.516 both → 0.5.)
  setVideoPlaybackBug(injectBug ? 1 : 0);
  ok = ok && approx(quantizeRequestedTime(0.512), 0.5);
  ok = ok && approx(quantizeRequestedTime(0.516), 0.5);   // same 1/60 bucket → same frame
  ok = ok && approx(quantizeRequestedTime(0.5),   0.5);
  // 0.5167*60 = 31.002 → floor 31 → 31/60 (the next frame boundary). Pinned to the true frame.
  ok = ok && approx(quantizeRequestedTime(0.5167), 31.0 / 60.0);
  setVideoPlaybackBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G2. Loop / clamp to duration (PlayVideo.cs:260-265). dur = 4.0s.
  //     No-loop clamps: -1 → 0, 5 → 4, 2.5 → 2.5 (middle, unclamped).
  ok = ok && approx(loopOrClampToDuration(-1.0, 4.0, /*loop=*/false), 0.0);
  ok = ok && approx(loopOrClampToDuration(5.0, 4.0, false), 4.0);
  ok = ok && approx(loopOrClampToDuration(2.5, 4.0, false), 2.5);  // DIVERGING MIDDLE
  //     Loop wraps: 5.0 % 4 = 1.0 ; 9.5 % 4 = 1.5.
  ok = ok && approx(loopOrClampToDuration(5.0, 4.0, /*loop=*/true), 1.0);
  ok = ok && approx(loopOrClampToDuration(9.5, 4.0, true), 1.5);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G3. Loop SHORTEST-PATH seek delta (PlayVideo.cs:176-181). dur=4.0.
  //     Playhead near the loop seam: should=0.2s, video=3.8s. A NAIVE delta is 0.2-3.8 = -3.6 (would
  //     rewind almost the whole clip). The shortest path wraps FORWARD across the seam: +0.4s.
  //       Fmod(-3.6 + 2, 4) - 2 = Fmod(-1.6,4) - 2 = (2.4) - 2 = 0.4.
  //     Bug 2 drops the wrap → -3.6 → RED. This is THE closed-form anchor for the loop math.
  setVideoPlaybackBug(injectBug ? 2 : 0);
  ok = ok && approx(seekDelta(/*seek=*/0.2, /*video=*/3.8, /*dur=*/4.0, /*loop=*/true), 0.4);
  //     Non-loop delta is just the raw difference (no wrap): 0.2-3.8 = -3.6.
  ok = ok && approx(seekDelta(0.2, 3.8, 4.0, /*loop=*/false), -3.6);
  //     Loop, a normal mid-clip delta stays unwrapped: should=2.5, video=1.0 → +1.5 (|1.5|<dur/2=2,
  //     so Fmod(1.5+2,4)-2 = 3.5-2 = 1.5, same as the naive delta — no wrap needed mid-clip).
  ok = ok && approx(seekDelta(2.5, 1.0, 4.0, true), 1.5);
  setVideoPlaybackBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G4. Seek threshold + decision (PlayVideo.cs:189-197).
  //     Playing forward → threshold = ResyncThreshold (0.1). Paused → 0.5/60 ≈ 0.008333.
  ok = ok && approx(seekThreshold(0.1, /*playingForward=*/true), 0.1);
  ok = ok && approx(seekThreshold(0.1, /*playingForward=*/false), 0.5 / 60.0);
  //     shouldSeek: |delta|=0.4 > thr 0.1, not completed, no seek in flight → SEEK.
  ok = ok && shouldSeek(0.4, 0.1, /*loop=*/true, /*completed=*/false, false, false) == true;
  //     Within threshold (|delta|=0.05 ≤ 0.1) → NO seek (frame-locked, no micro-seek).
  ok = ok && shouldSeek(0.05, 0.1, true, false, false, false) == false;
  //     Non-loop completed clip → never seek even with a big delta.
  ok = ok && shouldSeek(3.0, 0.1, /*loop=*/false, /*completed=*/true, false, false) == false;
  //     Seek already in flight → no new seek.
  ok = ok && shouldSeek(3.0, 0.1, true, false, /*isSeeking=*/true, false) == false;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G5. Completion (PlayVideo.cs:143-145). dur=4.0 → margin 3.984. requested 3.99 > margin → done.
  ok = ok && hasPlaybackCompleted(/*cur=*/2.0, /*req=*/3.99, /*dur=*/4.0, /*loop=*/false) == true;
  //     Requested 3.0 < margin, current 3.0 < margin → NOT done (middle).
  ok = ok && hasPlaybackCompleted(3.0, 3.0, 4.0, false) == false;
  //     Current 3.99 > margin (even if requested is early) → done (either term trips it).
  ok = ok && hasPlaybackCompleted(3.99, 1.0, 4.0, false) == true;
  //     Looping never completes.
  ok = ok && hasPlaybackCompleted(3.999, 3.999, 4.0, /*loop=*/true) == false;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G6. Seek-target look-ahead (PlayVideo.cs:202-204). Playing → +0.05, scrubbing → +0.
  ok = ok && approx(seekTargetTime(1.0, /*playingForward=*/true), 1.05);
  ok = ok && approx(seekTargetTime(1.0, /*playingForward=*/false), 1.0);
  //     Precise-pause offset (cs:172): 2/60 when precise, else 0.
  ok = ok && approx(precisePlaybackOffset(true), 2.0 / 60.0);
  ok = ok && approx(precisePlaybackOffset(false), 0.0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G7. PlayVideoClip timeline mapping (PlayVideoClip.cs:88-104). Timeline range [2,6] bars maps the
  //     source range [10,14] bars → rate = (14-10)/(6-2) = 1.0. At localTime=4 bars (middle of the
  //     timeline range), sourceBars = (4-2)*1 + 10 = 12.
  TimeRange timeR{2.0, 6.0};
  TimeRange srcR{10.0, 14.0};
  ok = ok && approx(clipPlaybackRate(srcR, timeR), 1.0);
  ok = ok && approx(clipSourceBars(4.0, srcR, timeR), 12.0);   // DIVERGING MIDDLE
  //     A STRETCHED clip: source [10,12] over timeline [2,6] → rate 0.5 (source plays half-speed).
  //     localTime=4 → (4-2)*0.5 + 10 = 11.  A dropped rate multiply would give 12 → this is the anchor
  //     that the rate is really applied (not identity).
  TimeRange srcHalf{10.0, 12.0};
  ok = ok && approx(clipPlaybackRate(srcHalf, timeR), 0.5);
  ok = ok && approx(clipSourceBars(4.0, srcHalf, timeR), 11.0);
  //     Degenerate timeline range (End==Start) → rate defaults to 1.0, no scaling applied.
  TimeRange degen{3.0, 3.0};
  ok = ok && approx(clipPlaybackRate(srcR, degen), 1.0);
  ok = ok && approx(clipSourceBars(5.0, srcR, degen), (5.0 - 3.0) + 10.0);  // no *rate leg

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G8. PlayVideoClip window + clamp (PlayVideoClip.cs:99-108). Source seconds [1,3], dur=5 → window
  //     [1,3]. shouldBe=2 (inside) → clampedTime 2. shouldBe=4 (past end) → clamped to 3.
  ClipWindow w = clipVideoWindow(/*srcStartSecs=*/1.0, /*srcEndSecs=*/3.0, /*dur=*/5.0);
  ok = ok && approx(w.start, 1.0) && approx(w.end, 3.0);
  ok = ok && approx(clipClampedTime(2.0, w), 2.0);   // inside window (middle)
  ok = ok && approx(clipClampedTime(4.0, w), 3.0);   // clamped to end
  ok = ok && approx(clipClampedTime(0.0, w), 1.0);   // clamped to start
  //     Reversed source range still yields an ordered window (min/max): [3,1] → [1,3].
  ClipWindow wr = clipVideoWindow(3.0, 1.0, 5.0);
  ok = ok && approx(wr.start, 1.0) && approx(wr.end, 3.0);
  //     Export threshold is a fixed 0.01 regardless of the caller's resync setting.
  ok = ok && approx(clipSeekThreshold(0.2, /*exporting=*/true), 0.01);
  ok = ok && approx(clipSeekThreshold(0.2, /*exporting=*/false), 0.2);
  //     clipShouldSeek: |delta|=0.05 > thr 0.01 → seek; reloadedPath forces seek regardless.
  ok = ok && clipShouldSeek(0.05, 0.01, /*reloaded=*/false, /*engineSeeking=*/false) == true;
  ok = ok && clipShouldSeek(0.005, 0.01, false, false) == false;  // within threshold
  ok = ok && clipShouldSeek(0.0, 0.01, /*reloaded=*/true, false) == true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G9. PlayVideoClip play gate (PlayVideoClip.cs:117-118). Plays only when NOT clamped off an end
  //     AND time advanced since last frame. shouldBe=2 == clamped 2, lastUpdate=1.5 → advanced → PLAY.
  setVideoPlaybackBug(injectBug ? 3 : 0);
  ok = ok && clipShouldPlay(/*shouldBe=*/2.0, /*clamped=*/2.0, /*lastUpdate=*/1.5,
                            /*exporting=*/false, /*reloaded=*/false) == true;
  //     Time did NOT advance (lastUpdate == clamped) → NO play. Bug 3 drops this guard → PLAY → RED.
  ok = ok && clipShouldPlay(2.0, 2.0, 2.0, false, false) == false;
  //     shouldBe clamped off the end (shouldBe 4 != clamped 3) → NO play (paused at the boundary).
  ok = ok && clipShouldPlay(4.0, 3.0, 2.0, false, false) == false;
  //     Exporting → never auto-play (deterministic export drives frames explicitly).
  ok = ok && clipShouldPlay(2.0, 2.0, 1.5, /*exporting=*/true, false) == false;
  //     Reloaded path → always play (kick the engine after a source switch).
  ok = ok && clipShouldPlay(4.0, 3.0, 5.0, false, /*reloaded=*/true) == true;
  setVideoPlaybackBug(0);

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-video-timing] injectBug did NOT trip (a timing tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-video-timing] injectBug correctly RED (corrupted timing cook path)\n");
    return 1;
  }
  std::printf("[selftest-io-video-timing] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-video-timing (order 710, after the io-socket family at 700-704).
REGISTER_SELFTESTS(/*orderBase=*/710, {"io-video-timing", sw::runIoVideoTimingSelfTest});
