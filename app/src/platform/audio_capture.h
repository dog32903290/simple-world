// platform/audio_capture — live microphone (or any chosen input device) -> raw mono blocks.
// The one native seam for World 1: a raw AUHAL input unit (audio thread) captures the chosen
// device and hands each mono-mixed block to a registered callback. The DSP (RMS / attack /
// FFT spectrum) lives ABOVE platform — app/audio_monitor owns it and registers the callback —
// so this stays a pure platform leaf with ZERO runtime dependency.
//
// platform leaf: owns the Core Audio unit behind a C++ pimpl, so callers include no ObjC and
// no runtime headers. Microphone access is requested on start() (first grant is the user's, a
// TCC prompt). Failure is non-fatal — no blocks flow and bound params fall back to base.
#pragma once
#include <string>

namespace sw {

class AudioCapture {
 public:
  AudioCapture();
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  // Per-block callback, invoked on the AUDIO THREAD for each captured block of mono-mixed
  // float samples. The DSP owner (app/audio_monitor) registers here — that inversion is what
  // keeps platform free of any runtime include. Set it BEFORE start(): the audio thread only
  // reads it once streaming begins, so setting-before-start avoids any race.
  using BlockCallback = void (*)(void* user, const float* mono, int numFrames, float sampleRate);
  void setBlockCallback(BlockCallback cb, void* user);

  // Request mic permission and (once granted) start capturing the given CoreAudio input
  // device (0 = system default). Returns false only on an immediate, known failure; when
  // permission is undetermined it returns true and capture starts asynchronously after the
  // user grants. Calling again switches device (stop + restart).
  bool start(unsigned int coreAudioDeviceId = 0);
  void stop();

  bool         running() const;        // unit actually delivering audio
  unsigned int currentDeviceId() const;// CoreAudio id the unit is bound to (0 = default)
  unsigned long long blocksProcessed() const;  // callback count (smoke diagnostic)

 private:
  struct Impl;
  static bool startEngine(Impl* impl, unsigned int deviceId);  // unit + input callback (in .mm)
  Impl* impl_;
};

// CLI smoke: start capture on the first input device whose name contains `deviceMatch`
// (empty = system default), run the runloop for `seconds`, print block count + an inline block
// RMS (computed in a local callback — no runtime dep) so a human can confirm that device feeds
// through. Safe once the mic is authorized.
int runAudioCaptureSmoke(double seconds, const std::string& deviceMatch = "");

// CLI diagnostic: print the current TCC microphone authorization status WITHOUT requesting it
// (safe — no prompt, no poison). NotDetermined / Denied / Restricted / Authorized.
int runAudioPermissionStatus();

}  // namespace sw
