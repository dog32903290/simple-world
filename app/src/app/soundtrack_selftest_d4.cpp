// app/soundtrack_selftest_d4 — leg ⑪ of --selftest-soundtrack (the D4-E3 playhead-isolation
// invariant), split mechanically out of soundtrack_selftest.cpp (ARCHITECTURE rule 4: <400;
// = the runAnimGuiS6Legs precedent). Called by runSoundtrackSelfTest; returns its failure
// count so the parent aggregates one verdict.
//
// ⑪ playhead-isolation invariant (D4-E3 咬帳 #1 sub-window-speed + #2 EOF-jump made executable).
// The worldview (soundtrack.h top note): the WALL CLOCK is master, the audio FOLLOWS, and
// position is NEVER written by the audio. D4's two live bites are both about the playhead being
// perturbed by the soundtrack — at sub-window speed (the audible-window fork) the playhead must
// keep advancing at the chosen rate while the stream pauses, and past the file's EOF the
// playhead must sail straight through (audio pauses, position untouched, no 突跳). This leg runs
// the SAME two-step the frame cook runs every frame (transport.advance THEN the follow rule) and
// asserts the follow rule moves the playhead by exactly ZERO bars. No audio device needed: the
// contract states are all Pause/None (the stream is never started), so positionSeconds() is a
// pure paused readback. The teeth: a regression that wired the audio clock back into the
// playhead (the bug D4 reported) would make followFrame's net Δposition nonzero -> FAIL here.
#include "app/soundtrack.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "platform/audio_playback.h"
#include "runtime/transport.h"

namespace sw::soundtrack {
namespace {

// Test-local helpers, duplicated on purpose (the two TUs stay independently compilable —
// same seam as animation_commands_selftest_s6.cpp's finder).
int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [soundtrack] FAIL %s\n", what); }
  else printf("  [soundtrack] ok   %s\n", what);
}
void expectNear(const char* what, double got, double want, double eps = 1e-9) {
  bool ok = std::fabs(got - want) <= eps;
  if (!ok) { ++g_fail; printf("  [soundtrack] FAIL %s got=%.9f want=%.9f\n", what, got, want); }
  else printf("  [soundtrack] ok   %s = %.9f\n", what, got);
}

// Minimal mono PCM16 WAV (44-byte canonical header + silence) — enough for AVAudioFile to open
// and report an EXACT length.
bool writeTinyWav(const std::string& path, uint32_t sampleRate, uint32_t frames) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t dataBytes = frames * 2;  // mono, 16-bit
  const uint32_t riffSize = 36 + dataBytes;
  const uint32_t byteRate = sampleRate * 2;
  const uint16_t fmtPcm = 1, channels = 1, blockAlign = 2, bits = 16;
  const uint32_t fmtSize = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); fwrite(&fmtSize, 4, 1, f);
  fwrite(&fmtPcm, 2, 1, f); fwrite(&channels, 2, 1, f);
  fwrite(&sampleRate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
  fwrite(&blockAlign, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
  for (uint32_t i = 0; i < frames; ++i) { int16_t z = 0; fwrite(&z, 2, 1, f); }
  fclose(f);
  return true;
}

}  // namespace

int runSoundtrackPlayheadIsoLeg(bool injectBug) {
  g_fail = 0;

  const std::string wav = "/tmp/sw_soundtrack_playhead_iso.wav";
  expect("fixture wav (3s) written", writeTinyWav(wav, 48000, 48000 * 3));  // EOF at 3.0s
  AudioPlayback p;
  expect("load(iso wav) succeeds", p.load(wav));
  const double durIso = p.durationSeconds();
  FollowState st;

  // The pure invariant: followFrame(...) must not touch the caller's playhead, whatever it
  // decides for the audio. We model the frame cook's two steps over a transport and assert the
  // ONLY thing that ever moves `position` is transport.advance — the follow rule's net effect on
  // it is zero. injectBug simulates the D4 regression: the follow rule reads the audio clock back
  // onto the playhead (target := audioPos), so a sub-window/EOF Pause would drag the playhead.
  auto frame = [&](Transport& t, double dtSecs) {
    const double before = t.position;
    t.advance(dtSecs);                         // wall clock moves the playhead (the ONLY writer)
    const double advanced = t.position;        // playhead after the legitimate advance
    const double targetSecs = t.secondsFromBars(t.position);
    followFrame(st, p, t.playing(), t.rate, targetSecs);   // audio follows — must NOT write t
    if (injectBug) {
      // The regression D4 caught: audio position bleeds back onto the playhead. With a paused
      // stream (sub-window / past-EOF) audioPos sticks at its last readback, so the playhead
      // would freeze (#1) or snap to the clamped EOF readback (#2) — exactly the two bites.
      t.position = p.positionSeconds() < 0.0 ? t.position : p.positionSeconds() * t.bpm / 240.0;
    }
    return advanced - before;  // how far the LEGIT advance moved it (the expected Δ)
  };

  // A1 — sub-window speed (0.1 < kAudibleSpeedMin 0.25): stream pauses, playhead runs at 0.1.
  {
    Transport t; t.bpm = 120.0; t.setRate(0.1); t.play();  // 0.1 stays (> eps, not revived)
    st = FollowState{};
    double sumExpected = 0.0, sumActual = 0.0;
    for (int i = 0; i < 30; ++i) {              // 0.5s of frames at 60Hz, all inside [0,EOF)
      const double before = t.position;
      const double legit = frame(t, 1.0 / 60.0);
      sumExpected += legit;
      sumActual += t.position - before;
    }
    expect("A1: sub-window speed advances the playhead (not frozen)", t.position > 0.0);
    expectNear("A1: playhead Δ == transport.advance Δ (audio added nothing)",
               sumActual, sumExpected, 1e-9);
    expectNear("A1: playhead == 0.1×barsFromSeconds(0.5s) exactly",
               t.position, t.barsFromSeconds(0.5) * 0.1, 1e-9);
    (void)durIso;
  }

  // A2 — playhead sails through soundtrack EOF (1x): the stream pauses at the end, position
  // never jumps. Start just before EOF, cross it, and assert the playhead is the pure integral
  // of the wall dt across the whole crossing (no 突跳 at the boundary).
  {
    Transport t; t.bpm = 120.0; t.setRate(1.0); t.play();
    t.scrub(t.barsFromSeconds(durIso - 0.05));  // 50ms of audio left
    t.advance(0.0);                              // consume the scrub (fxTime snaps; pos held)
    st = FollowState{};
    const double startPos = t.position;
    double sumExpected = 0.0, sumActual = 0.0;
    double maxStep = 0.0;
    for (int i = 0; i < 30; ++i) {              // 0.5s — well past the 50ms to EOF
      const double before = t.position;
      const double legit = frame(t, 1.0 / 60.0);
      const double step = t.position - before;
      if (step > maxStep) maxStep = step;
      sumExpected += legit;
      sumActual += step;
    }
    expect("A2: playhead crossed past EOF (kept advancing through the boundary)",
           t.secondsFromBars(t.position) > durIso);
    expectNear("A2: playhead Δ across EOF == pure wall-dt integral (no audio writeback)",
               sumActual, sumExpected, 1e-9);
    expectNear("A2: total advance == barsFromSeconds(0.5s) at 1x (smooth, no 突跳)",
               t.position - startPos, t.barsFromSeconds(0.5), 1e-9);
    // No single frame jumped: every step is one 1/60s worth of bars, never a boundary spike.
    expect("A2: no per-frame 突跳 at the EOF boundary (max step == one frame's worth)",
           maxStep <= t.barsFromSeconds(1.0 / 60.0) + 1e-9);
  }

  p.unload();
  std::remove(wav.c_str());
  return g_fail;
}

}  // namespace sw::soundtrack
