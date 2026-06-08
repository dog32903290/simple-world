// platform/audio_capture — live microphone (or any default input) -> attack envelope.
// The one native seam for World 1: an AVAudioEngine input tap (audio thread) feeds the
// pure-compute runtime/audio_analyzer (samples->RMS) and runtime/attack_detector
// (RMS->onset/envelope), publishing the latest envelope via an atomic. main reads
// envelope() each frame and exposes it as the LiveSource "audio.kick".
//
// platform leaf: owns the ObjC engine + the runtime analyzers; hides both behind a
// C++ pimpl so callers (main) include zero ObjC. Microphone access is requested on
// start(); first grant is the user's (a TCC prompt). Failure is non-fatal — envelope()
// just stays 0 and the bound param falls back to its base value.
#pragma once

namespace sw {

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  // Request mic permission and (once granted) start the input tap. Returns false only
  // on an immediate, known failure; when permission is still undetermined it returns
  // true and the engine starts asynchronously after the user grants. Safe to call once.
  bool start();
  void stop();

  bool  running() const;       // engine actually delivering audio
  float envelope() const;      // latest attack envelope (0..~1), thread-safe
  float lastRms() const;       // latest block RMS (smoke diagnostic)
  unsigned long long blocksProcessed() const;  // tap-callback count (smoke diagnostic)

  // Test-only: force the published envelope (no audio device). Lets the headless
  // wiring selftest prove envelope -> LiveSource -> Speed without touching the mic.
  void setTestEnvelope(float v);

 private:
  struct Impl;
  static bool startEngine(Impl* impl);  // build engine + install tap (defined in .mm)
  Impl* impl_;
};

// CLI smoke: start capture, run the runloop for `seconds`, print rms/envelope so a
// human can confirm the mic feeds through. NOT run headless at night (a denied TCC
// prompt would poison the grant) — this is for the user / a manual session.
int runAudioCaptureSmoke(double seconds);

}  // namespace sw
