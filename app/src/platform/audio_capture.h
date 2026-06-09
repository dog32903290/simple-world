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
#include <string>

#include "runtime/spectrum_analyzer.h"  // SpectrumSnapshot (per-band FFT analysis, TiXL parity)

namespace sw {

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  // Request mic permission and (once granted) start the input tap on the given CoreAudio
  // input device (0 = system default). Returns false only on an immediate, known failure;
  // when permission is still undetermined it returns true and the engine starts
  // asynchronously after the user grants. Calling again switches device (stop + restart).
  bool start(unsigned int coreAudioDeviceId = 0);
  void stop();

  bool         running() const;        // engine actually delivering audio
  unsigned int currentDeviceId() const;// CoreAudio id the engine is bound to (0 = default)
  float envelope() const;      // latest attack envelope (0..~1), thread-safe
  float lastRms() const;       // latest block RMS (smoke diagnostic)
  SpectrumSnapshot spectrumSnapshot() const;  // latest per-band FFT analysis (lock-free copy)
  unsigned long long blocksProcessed() const;  // tap-callback count (smoke diagnostic)

 private:
  struct Impl;
  static bool startEngine(Impl* impl, unsigned int deviceId);  // engine + tap (in .mm)
  Impl* impl_;
};

// CLI smoke: start capture on the first input device whose name contains `deviceMatch`
// (empty = system default), run the runloop for `seconds`, print rms/envelope so a human
// can confirm that device feeds through. Safe once the mic is authorized.
int runAudioCaptureSmoke(double seconds, const std::string& deviceMatch = "");

// CLI diagnostic: print the current TCC microphone authorization status WITHOUT
// requesting it (safe — no prompt, no poison). NotDetermined / Denied / Restricted /
// Authorized tells us whether the prompt path is even reachable for this binary.
int runAudioPermissionStatus();

}  // namespace sw
