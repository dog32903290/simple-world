// app/soundtrack_selftest — soundtrack<->transport follow rule, headless (--selftest-soundtrack).
// Legs:
//   ① decide() drift core: free-run inside the threshold, hard-seek past it (both sides of 0.04)
//   ①b-e speed input: drift gate speed-invariant (TiXL's ×speed factors cancel); speed 0 = pause
//      (the refuter-C rate=0+Playing trap); negative & out-of-window [0.25,4] pause (named forks)
//   ② play/pause/scrub sequence: pause = stream pause NO seek; scrub-while-paused stays silent;
//      play re-enters at the target (TiXL UpdateSoundtrackTime:159-228 semantics)
//   ③ out-of-bounds target (past the file end / negative) -> stream pauses, never dangles
//   ④ bars->secs targeting: the TARGET moves with BPM, the audio domain stays seconds
//      (BPM change must never touch audio rate — only the mapping)
//   ⑤ platform load() failure: missing file -> false, instance stays empty, controls no-op (no crash)
//   ⑥ real WAV roundtrip: tiny PCM16 file -> load -> exact duration -> paused seek -> position
//      readback -> clamp past the end -> unload (no audio device needed: never play()ed)
//   ⑦ live engine legs (refuter-C 修1/修3 — needs a default output device; SKIPs gracefully
//      without one): a simulated device-config change mid-play recovers on the next schedule
//      (engine restarted, still playing); seek(duration) while playing drops the playing flag
//      (the [player stop] early-out used to leave it wedged true); play() after that resurrects
//   ⑦b varispeed race (live, SKIPs without a device): rate 4 position outruns the wall clock;
//      rate clamp [0.25,4] itself is headless in leg ⑥
//   ⑧ failure-cache retry seam (refuter-C 修4): a missing path fails once, is NOT retried when
//      the file appears (named fork vs TiXL per-frame retry); an EXPLICIT same-path re-pick loads
//   ⑨ ceiling hysteresis (refuter-E3 修3, headless): excursion above 4.0 locks the stream out
//      until 3.8; a 3.99↔4.01 flutter = ONE Pause, zero re-plays; exact 4.0 never locks out
//   ⑩ closed-loop chase harness (refuter-E3 修1, probe_c made permanent — live, SKIPs without
//      an output device): a REAL AudioPlayback chased through the production followFrame at
//      60Hz × 2.5s per rate {1,1.5,2,4}; hard-seek rate < 5% of frames, audible, drift bounded
//   ⑪ playhead-isolation invariant (D4-E3 咬帳 made executable; full description + body in
//      soundtrack_selftest_d4.cpp — mechanical split, ARCHITECTURE rule 4)
// injectBug shrinks leg ①'s drift below the threshold while still expecting Resync -> FAIL,
// runs leg ⑩'s loop WITHOUT resyncOffset/settle-guard (the pre-fix code) -> rate 2 storms red,
// AND bleeds the audio clock back onto the playhead in leg ⑪ -> freeze/snap -> red.
#include "app/soundtrack.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "app/document.h"
#include "platform/audio_playback.h"
#include "runtime/transport.h"

namespace sw::soundtrack {
namespace {

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

const char* name(Action a) {
  switch (a) {
    case Action::None: return "None";
    case Action::Pause: return "Pause";
    case Action::Resync: return "Resync";
    case Action::PlayAtTarget: return "PlayAtTarget";
  }
  return "?";
}
void expectAction(const char* what, Action got, Action want) {
  bool ok = got == want;
  if (!ok) { ++g_fail; printf("  [soundtrack] FAIL %s got=%s want=%s\n", what, name(got), name(want)); }
  else printf("  [soundtrack] ok   %s -> %s\n", what, name(got));
}

// Minimal mono PCM16 WAV: 44-byte canonical header + frames of silence. Enough for AVAudioFile
// to open and report an EXACT length, which is all leg ⑥ asserts.
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

int runSoundtrackSelfTest(bool injectBug) {
  g_fail = 0;
  const double dur = 10.0;  // pretend file length for the decide() legs (seconds)

  // ① drift core (speed 1). The teeth: injectBug feeds a drift INSIDE the threshold but still
  // expects a hard-seek — a decide() that always (or never) resyncs dies on one of the two probes.
  expectAction("playing + drift 0.02 (inside 0.04) free-runs",
               decide(true, 1.0, 5.0, dur, true, 5.02), Action::None);
  const double bigDrift = injectBug ? 0.03 : 0.10;
  expectAction("playing + drift past threshold hard-seeks",
               decide(true, 1.0, 5.0, dur, true, 5.0 + bigDrift), Action::Resync);
  expectAction("drift is symmetric (audio BEHIND the target)",
               decide(true, 1.0, 5.0, dur, true, 4.85), Action::Resync);

  // ①b the speed factor rides BOTH sides of the drift gate (soundDelta×speed cs:208, threshold×
  // |speed| cs:221) — the factors cancel, so the TiXL-observable is speed-INVARIANCE: the same
  // drift decides the same way at any voiceable speed.
  expectAction("speed 2: drift 0.02 still free-runs (gate is speed-invariant)",
               decide(true, 2.0, 5.0, dur, true, 5.02), Action::None);
  expectAction("speed 2: drift 0.10 still resyncs",
               decide(true, 2.0, 5.0, dur, true, 5.10), Action::Resync);
  expectAction("speed 0.5: drift symmetric behind, still resyncs",
               decide(true, 0.5, 5.0, dur, true, 4.85), Action::Resync);

  // ①c speed==0 == pause (TiXL UpdateSoundtrackTime:159-163) — THE refuter-C trap: transport
  // state Playing + rate 0 freezes the playhead (Playback.cs:108 eps); the music MUST freeze
  // with it, not keep running into a 0.04s-jitter rewind fight.
  const double zeroSpeed = injectBug ? 1.0 : 0.0;  // bug = ignore the rate input -> FAIL
  expectAction("speed 0 while transport 'Playing' -> stream pauses (refuter-C trap)",
               decide(true, zeroSpeed, 5.0, dur, true, 5.0), Action::Pause);
  expectAction("speed 0, already silent -> nothing",
               decide(true, 0.0, 5.0, dur, false, 5.0), Action::None);

  // ①d negative speed: NAMED FORK — TiXL plays the soundtrack BACKWARDS (BASS ReverseDirection
  // + Frequency*-speed, SoundtrackClipStream.cs:49-54); AVAudioUnitVarispeed cannot reverse, so
  // the stream pauses while the transport/visuals run backwards.
  expectAction("negative speed -> Pause (named fork: no reverse audio on AVAudio)",
               decide(true, -1.0, 5.0, dur, true, 5.0), Action::Pause);

  // ①e outside the varispeed window [0.25, 4] (named fork vs BASS's ±16): pause, not a
  // resync-glitch storm chasing a playhead the audio chain can't match.
  expectAction("speed 8 (above varispeed max 4) -> Pause",
               decide(true, 8.0, 5.0, dur, true, 5.0), Action::Pause);
  expectAction("speed 0.1 (below varispeed min 0.25) -> Pause",
               decide(true, 0.1, 5.0, dur, true, 5.0), Action::Pause);
  expectAction("speed 4.0 (the ceiling itself) is voiceable",
               decide(true, 4.0, 5.0, dur, true, 5.01), Action::None);
  expectAction("speed 0.25 (the floor itself) is voiceable",
               decide(true, 0.25, 5.0, dur, true, 5.01), Action::None);

  // ② play/pause/scrub sequence (the TiXL pause/scrub semantics, one frame per row).
  expectAction("frame1: transport plays, audio idle -> enter at target",
               decide(true, 1.0, 0.0, dur, false, 0.0), Action::PlayAtTarget);
  expectAction("frame2: both running in sync -> hands off",
               decide(true, 1.0, 1.0, dur, true, 1.01), Action::None);
  expectAction("frame3: transport pauses -> stream pauses, NO seek",
               decide(false, 1.0, 1.0, dur, true, 1.01), Action::Pause);
  expectAction("frame4: scrub while paused -> target jumps, stream STAYS silent",
               decide(false, 1.0, 7.5, dur, false, 1.01), Action::None);
  expectAction("frame5: play after scrub -> re-enter at the scrubbed target",
               decide(true, 1.0, 7.5, dur, false, 1.01), Action::PlayAtTarget);

  // ③ out-of-bounds target: pause, never dangle (and stay silent once paused).
  expectAction("playhead past the file end while playing -> pause",
               decide(true, 1.0, dur + 0.5, dur, true, 9.99), Action::Pause);
  expectAction("still past the end, already silent -> nothing",
               decide(true, 1.0, dur + 0.6, dur, false, 9.99), Action::None);
  expectAction("negative target -> pause (defensive: scrub clamps at 0 upstream)",
               decide(true, 1.0, -0.1, dur, true, 0.0), Action::Pause);
  expectAction("target exactly at duration counts as out (half-open file)",
               decide(true, 1.0, dur, dur, true, 9.99), Action::Pause);

  // ④ bars->secs targeting: same playhead BAR maps to different SECONDS as BPM changes —
  // that mapping is the ONLY thing BPM touches; the audio clock stays in seconds.
  {
    Transport t;
    t.bpm = 120.0;
    expectNear("2 bars @ 120 BPM targets 4s of audio", t.secondsFromBars(2.0), 4.0);
    t.bpm = 240.0;
    expectNear("2 bars @ 240 BPM targets 2s of audio", t.secondsFromBars(2.0), 2.0);
    // the follow rule at the SAME audio position: in sync at 240, a hard drift at 120 —
    // i.e. a BPM change re-targets (seek), it never bends the audio rate.
    expectAction("BPM change shows up as a TARGET jump, handled by resync",
                 decide(true, 1.0, 4.0, dur, true, 2.0), Action::Resync);
  }

  // ⑤ load failure: missing file -> false, the instance stays empty and every control no-ops.
  {
    AudioPlayback p;
    expect("load(missing file) returns false", !p.load("/nonexistent_dir_xyz/missing.wav"));
    expect("failed load leaves nothing loaded", !p.loaded());
    expectNear("no file -> duration 0", p.durationSeconds(), 0.0);
    p.play(); p.seek(3.0); p.pause();  // must not crash on an empty instance
    expect("controls on an empty instance no-op", !p.playing());
    expectNear("position stays 0", p.positionSeconds(), 0.0);
  }

  // ⑥ real WAV roundtrip (never play()ed -> no engine start -> no audio device needed).
  {
    const std::string wav = "/tmp/sw_soundtrack_selftest.wav";
    const uint32_t sr = 48000, frames = 4800;  // exactly 0.1s
    expect("fixture wav written", writeTinyWav(wav, sr, frames));
    AudioPlayback p;
    expect("load(tiny wav) succeeds", p.load(wav));
    expectNear("duration is exact (4800/48000)", p.durationSeconds(), 0.1);
    p.seek(0.05);  // paused seek: position must read back the target
    expectNear("paused seek reads back", p.positionSeconds(), 0.05);
    p.seek(99.0);  // past the end: clamps to duration
    expectNear("seek past the end clamps", p.positionSeconds(), 0.1);
    // varispeed rate clamp (no engine start needed — the AU property is settable headless):
    // the platform setter pins the AVAudioUnitVarispeed parameter range [0.25, 4].
    expectNear("default rate is 1.0", p.rate(), 1.0);
    p.setRate(99.0);
    expectNear("setRate clamps to varispeed max 4.0", p.rate(), 4.0);
    p.setRate(0.01);
    expectNear("setRate clamps to varispeed min 0.25", p.rate(), 0.25);
    p.setRate(1.0);
    expectNear("in-range rate lands exactly", p.rate(), 1.0);
    p.unload();
    expectNear("unload empties the instance", p.durationSeconds(), 0.0);
    std::remove(wav.c_str());
  }

  // ⑦ live engine legs (修1 engine liveness / 修3 playing flag). These really start the engine,
  // so they need an output device — on a machine without one, skip loudly instead of failing.
  {
    const std::string wav = "/tmp/sw_soundtrack_selftest_live.wav";
    expect("fixture wav (1s) written", writeTinyWav(wav, 48000, 48000));
    AudioPlayback p;
    expect("load(live wav) succeeds", p.load(wav));
    p.play();
    if (!p.playing()) {
      printf("  [soundtrack] SKIP leg ⑦ (engine start failed: no output device on this machine"
             " — 修1 live recovery needs the 拔耳機 hand test, see report)\n");
    } else {
      // 修1: engine liveness is re-checked per start (engine.isRunning, not a one-shot flag).
      // Simulate the device-config-change notification, then drive the next schedule path the
      // way the follow rule would (a seek while playing) — playback must survive the restart.
      p.debugSimulateConfigChange();
      p.seek(0.25);
      expect("config change mid-play + reschedule keeps playing (engine restarted)", p.playing());
      expectNear("position resumes from the reschedule target", p.positionSeconds(), 0.25, 0.05);
      // 修3: seek to the very end while playing -> the early-out already [player stop]ped, so
      // playing() must read false (was: wedged true forever).
      p.seek(p.durationSeconds());
      expect("seek(duration) while playing drops the playing flag", !p.playing());
      // Resurrection: a paused seek back + play() works again (the follow rule's PlayAtTarget).
      p.seek(0.0);
      p.play();
      expect("play() after the end-seek resurrects", p.playing());
      p.pause();
      expect("pause() lands", !p.playing());
    }
    std::remove(wav.c_str());
  }

  // ⑦b varispeed engages in the real render chain (live — needs an output device, SKIPs
  // without one). At rate 4 the player's sample clock must cross 1.2s of source within 0.6s
  // of WALL time; a 1x chain tops out at ~0.6s, so crossing proves the 4x resample really sits
  // in the signal path. (真變速聽感 stays 柏為's ears; this pins the clock side headless.)
  {
    const std::string wav = "/tmp/sw_soundtrack_selftest_vari.wav";
    expect("fixture wav (4s) written", writeTinyWav(wav, 48000, 192000));
    AudioPlayback p;
    expect("load(vari wav) succeeds", p.load(wav));
    p.setRate(4.0);
    p.play();
    if (!p.playing()) {
      printf("  [soundtrack] SKIP leg ⑦b (engine start failed: no output device on this"
             " machine — varispeed race needs live render)\n");
    } else {
      const auto t0 = std::chrono::steady_clock::now();
      bool crossed = false;
      while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 0.6) {
        if (p.positionSeconds() >= 1.2) { crossed = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      expect("rate 4 outruns the wall clock (varispeed engaged in the render chain)", crossed);
      p.pause();
    }
    std::remove(wav.c_str());
  }

  // ⑧ failure-cache retry seam (修4), through the REAL production watcher (syncFrame pulls
  // lib.composition.soundtrackPath). Observable = statusText(): "load failed: ..." vs basename.
  {
    const std::string path = "/tmp/sw_soundtrack_retry.wav";
    const std::string base = "sw_soundtrack_retry.wav";
    std::remove(path.c_str());
    const std::string savedPath = doc::g_lib().composition.soundtrackPath;
    doc::g_lib().composition.soundtrackPath = path;     // path set while the file is MISSING
    syncFrame(false, 1.0, 0.0);                            // watcher: load fails -> cached
    expect("missing file fails and is cached", statusText() == "load failed: " + base);
    expect("fixture appears at the SAME path", writeTinyWav(path, 48000, 4800));
    syncFrame(false, 1.0, 0.0);                            // same path, file now valid
    expect("same path NOT retried per frame (named fork vs TiXL PlaybackUtils.cs:35)",
           statusText() == "load failed: " + base);
    applySoundtrackPick(path);                        // 柏為 explicitly re-picks the same path
    syncFrame(false, 1.0, 0.0);
    expect("explicit re-pick of the SAME path retries and loads", statusText() == base);
    doc::g_lib().composition.soundtrackPath = savedPath;  // restore + unload for later tests
    syncFrame(false, 1.0, 0.0);
    std::remove(path.c_str());
  }

  // ⑨ ceiling hysteresis (修3, named fork — BASS voices ±16, no edge to flutter on). Headless.
  {
    bool lock = false;
    lock = speedLockoutStep(lock, 4.01);
    expect("4.01 exits above the ceiling -> locked out", lock);
    lock = speedLockoutStep(lock, 3.99);
    expect("3.99 stays locked (re-entry needs <= 3.8)", lock);
    expectAction("locked out + audible -> Pause (hysteresis silences an in-window speed)",
                 decide(true, 3.99, 5.0, dur, true, 5.0, true), Action::Pause);
    expectAction("locked out + already silent -> None (holds, no re-play)",
                 decide(true, 3.99, 5.0, dur, false, 5.0, true), Action::None);
    lock = speedLockoutStep(lock, 3.8);
    expect("3.8 re-enters (lockout clears)", !lock);
    expectAction("after re-entry the stream resumes",
                 decide(true, 3.8, 5.0, dur, false, 5.0, false), Action::PlayAtTarget);
    expect("exact 4.0 (a UI doubling target) never locks out", !speedLockoutStep(false, 4.0));

    // the refuter flutter: 4.01↔3.99 every frame — ONE Pause, ZERO re-plays (no machine gun)
    bool l = false;
    bool audioOn = true;
    int plays = 0, pauses = 0;
    for (int i = 0; i < 20; ++i) {
      const double sp = (i % 2 == 0) ? 4.01 : 3.99;
      l = speedLockoutStep(l, sp);
      const Action a = decide(true, sp, 5.0, dur, audioOn, 5.0, l);
      if (a == Action::Pause) { ++pauses; audioOn = false; }
      if (a == Action::PlayAtTarget) { ++plays; audioOn = true; }
    }
    expect("flutter across 4.0: exactly one Pause", pauses == 1);
    expect("flutter across 4.0: zero re-plays while jittering (hysteresis holds)", plays == 0);
  }

  // ⑩ closed-loop chase harness (修1 — refuter probe_c made permanent; live, SKIPs without an
  // output device). A real AudioPlayback chased by a simulated transport target at ~60Hz for
  // 2.5s per rate through the PRODUCTION followFrame. Kill assertion: Resync rate < 5% of
  // frames — pre-fix every resync landed restartDelay×rate late and begat the next storm.
  {
    const std::string wav = "/tmp/sw_soundtrack_chase.wav";
    expect("fixture wav (12s) written", writeTinyWav(wav, 48000, 48000 * 12));
    AudioPlayback p;
    expect("load(chase wav) succeeds", p.load(wav));
    p.play();
    if (!p.playing()) {
      printf("  [soundtrack] SKIP leg ⑩ (engine start failed: no output device on this machine"
             " — the chase loop needs live render)\n");
    } else {
      p.pause();
      for (const double rate : {1.0, 1.5, 2.0, 4.0}) {
        p.setRate(rate);  // = syncFrame's speed push (constant for the whole run)
        p.seek(0.0);
        FollowState st;
        double target = 0.0;
        int frames = 0, resyncs = 0;
        auto prev = std::chrono::steady_clock::now();
        const auto t0 = prev;
        for (;;) {
          std::this_thread::sleep_for(std::chrono::milliseconds(16));
          const auto now = std::chrono::steady_clock::now();
          const double dt = std::chrono::duration<double>(now - prev).count();
          prev = now;
          target += dt * rate;  // the transport: wall clock is master, audio chases
          ++frames;
          Action a;
          if (injectBug) {
            // pre-fix loop: decide + raw apply, no third speed, no settle guard (批次8 as-was)
            a = decide(true, rate, target, p.durationSeconds(), p.playing(),
                       p.positionSeconds());
            switch (a) {
              case Action::None: case Action::Pause: break;
              case Action::Resync: p.seek(target); break;
              case Action::PlayAtTarget: p.seek(target); p.play(); break;
            }
          } else {
            a = followFrame(st, p, true, rate, target);
          }
          if (a == Action::Resync) ++resyncs;
          if (std::chrono::duration<double>(now - t0).count() >= 2.5) break;
        }
        char what[160];
        snprintf(what, sizeof what, "chase @%.2fx: resync storm dead (%d/%d hard-seeks < 5%%)",
                 rate, resyncs, frames);
        expect(what, resyncs * 20 < frames);
        snprintf(what, sizeof what, "chase @%.2fx: still audible at the end", rate);
        expect(what, p.playing());
        // End drift bounded — unless a seek is mid-restart (readback frozen at the seek target
        // = drift unmeasurable; that window belongs to the settle guard).
        const double endDrift = p.positionSeconds() - target;
        const bool settling =
            st.pendingSeekPos >= 0.0 && p.positionSeconds() <= st.pendingSeekPos + 1e-6;
        snprintf(what, sizeof what, "chase @%.2fx: end drift %+.0fms within 2x threshold",
                 rate, endDrift * 1000.0);
        expect(what, settling || std::fabs(endDrift) <= 2.0 * kResyncThresholdSecs);
        p.pause();
      }
    }
    std::remove(wav.c_str());
  }

  // ⑪ playhead-isolation invariant (D4-E3 咬帳 #1/#2 made executable) lives in
  // soundtrack_selftest_d4.cpp (mechanical split, ARCHITECTURE rule 4 — the
  // runAnimGuiS6Legs precedent). injectBug bleeds the audio clock back onto the
  // playhead there -> freeze/snap -> red.
  g_fail += runSoundtrackPlayheadIsoLeg(injectBug);

  printf("[selftest-soundtrack] %s\n", g_fail == 0 ? "PASS" : "FAIL");
  return g_fail == 0 ? 0 : 1;
}

}  // namespace sw::soundtrack
