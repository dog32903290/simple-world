// app/audio_monitor — owns the audio-analysis DSP (block RMS, attack envelope, per-band FFT
// spectrum) and drives it from the platform capture's per-block callback.
//
// This is where the runtime analyzers live now. It exists to keep platform/audio_capture a
// pure leaf: capture (platform) no longer includes any runtime DSP — it just hands raw
// mono-mixed blocks to onBlock() on the audio thread via a fn-pointer callback. The owner of
// that callback (this, in the app zone, where app->runtime is legal) drives the analyzers and
// publishes the results, which the UI + main read on the render thread. Dependency direction
// is clean: platform stays leaf, app->runtime is allowed.
#pragma once
#include "runtime/spectrum_analyzer.h"  // SpectrumSnapshot (app -> runtime, allowed)

namespace sw::audio_monitor {

// AudioCapture's BlockCallback (audio thread, once per captured block of mono samples).
// Drives the analyzers; lock-free (they publish via atomics / the spectrum double-buffer).
// `user` is unused (the analyzers are this module's singletons). Set on capture BEFORE start.
void onBlock(void* user, const float* mono, int numFrames, float sampleRate);

// Read on the render thread (main / UI).
float            rms();        // latest block RMS — "any sound coming in"
float            envelope();   // latest attack envelope — "transients / hits"
SpectrumSnapshot spectrum();   // latest per-band FFT analysis (TiXL AudioReaction parity)

// Isolated proof: a sine block lights its spectrum band + raises rms; silence -> ~0.
int runAudioMonitorSelfTest(bool injectBug);

}  // namespace sw::audio_monitor
