// app/soundtrack_selftest — soundtrack<->transport follow rule, headless (--selftest-soundtrack).
// Legs:
//   ① decide() drift core: free-run inside the threshold, hard-seek past it (both sides of 0.04)
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
//   ⑧ failure-cache retry seam (refuter-C 修4): a missing path fails once and is NOT retried
//      when the file later appears (named fork vs TiXL's per-frame retry, PlaybackUtils.cs:35);
//      an EXPLICIT re-pick of the SAME path (applySoundtrackPick) retries and loads
// injectBug shrinks leg ①'s drift below the threshold while still expecting Resync -> FAIL (teeth).
#include "app/soundtrack.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

  // ① drift core. The teeth: injectBug feeds a drift INSIDE the threshold but still expects a
  // hard-seek — a decide() that always (or never) resyncs dies on one of the two probes.
  expectAction("playing + drift 0.02 (inside 0.04) free-runs",
               decide(true, 5.0, dur, true, 5.02), Action::None);
  const double bigDrift = injectBug ? 0.03 : 0.10;
  expectAction("playing + drift past threshold hard-seeks",
               decide(true, 5.0, dur, true, 5.0 + bigDrift), Action::Resync);
  expectAction("drift is symmetric (audio BEHIND the target)",
               decide(true, 5.0, dur, true, 4.85), Action::Resync);

  // ② play/pause/scrub sequence (the TiXL pause/scrub semantics, one frame per row).
  expectAction("frame1: transport plays, audio idle -> enter at target",
               decide(true, 0.0, dur, false, 0.0), Action::PlayAtTarget);
  expectAction("frame2: both running in sync -> hands off",
               decide(true, 1.0, dur, true, 1.01), Action::None);
  expectAction("frame3: transport pauses -> stream pauses, NO seek",
               decide(false, 1.0, dur, true, 1.01), Action::Pause);
  expectAction("frame4: scrub while paused -> target jumps, stream STAYS silent",
               decide(false, 7.5, dur, false, 1.01), Action::None);
  expectAction("frame5: play after scrub -> re-enter at the scrubbed target",
               decide(true, 7.5, dur, false, 1.01), Action::PlayAtTarget);

  // ③ out-of-bounds target: pause, never dangle (and stay silent once paused).
  expectAction("playhead past the file end while playing -> pause",
               decide(true, dur + 0.5, dur, true, 9.99), Action::Pause);
  expectAction("still past the end, already silent -> nothing",
               decide(true, dur + 0.6, dur, false, 9.99), Action::None);
  expectAction("negative target -> pause (defensive: scrub clamps at 0 upstream)",
               decide(true, -0.1, dur, true, 0.0), Action::Pause);
  expectAction("target exactly at duration counts as out (half-open file)",
               decide(true, dur, dur, true, 9.99), Action::Pause);

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
                 decide(true, 4.0, dur, true, 2.0), Action::Resync);
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

  // ⑧ failure-cache retry seam (修4), through the REAL production watcher (syncFrame pulls
  // lib.composition.soundtrackPath). Observable = statusText(): "load failed: ..." vs basename.
  {
    const std::string path = "/tmp/sw_soundtrack_retry.wav";
    const std::string base = "sw_soundtrack_retry.wav";
    std::remove(path.c_str());
    const std::string savedPath = doc::g_lib.composition.soundtrackPath;
    doc::g_lib.composition.soundtrackPath = path;     // path set while the file is MISSING
    syncFrame(false, 0.0);                            // watcher: load fails -> cached
    expect("missing file fails and is cached", statusText() == "load failed: " + base);
    expect("fixture appears at the SAME path", writeTinyWav(path, 48000, 4800));
    syncFrame(false, 0.0);                            // same path, file now valid
    expect("same path NOT retried per frame (named fork vs TiXL PlaybackUtils.cs:35)",
           statusText() == "load failed: " + base);
    applySoundtrackPick(path);                        // 柏為 explicitly re-picks the same path
    syncFrame(false, 0.0);
    expect("explicit re-pick of the SAME path retries and loads", statusText() == base);
    doc::g_lib.composition.soundtrackPath = savedPath;  // restore + unload for later tests
    syncFrame(false, 0.0);
    std::remove(path.c_str());
  }

  printf("[selftest-soundtrack] %s\n", g_fail == 0 ? "PASS" : "FAIL");
  return g_fail == 0 ? 0 : 1;
}

}  // namespace sw::soundtrack
