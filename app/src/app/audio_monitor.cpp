#include "app/audio_monitor.h"

#include "runtime/attack_detector.h"
#include "runtime/audio_analyzer.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

namespace sw::audio_monitor {
namespace {
AudioAnalyzer      g_analyzer;
AttackDetector     g_attack;
SpectrumAnalyzer   g_spectrum;
std::atomic<float> g_rms{0.0f};
std::atomic<float> g_env{0.0f};
unsigned long long g_sampleClock = 0;  // audio-thread only (drives the attack detector's clock)
}  // namespace

void onBlock(void* /*user*/, const float* mono, int numFrames, float sampleRate) {
  if (mono == nullptr || numFrames <= 0 || sampleRate <= 0.0f) return;
  // capture already mono-mixed; feed the analyzer as a single channel.
  const float* chans[1] = {mono};
  g_analyzer.processBlock(chans, 1, numFrames, 1.0f);
  const AudioSnapshot snap = g_analyzer.snapshot();
  g_sampleClock += (unsigned long long)numFrames;
  const double timeMs = (double)g_sampleClock / sampleRate * 1000.0;
  AttackFrameInput in;
  in.hasRms = true;  in.rms = snap.rms;
  in.hasPeak = true; in.peak = snap.peak;
  in.timeMs = timeMs;
  const AttackFrameOutput out = g_attack.processFrame(in);
  g_env.store((float)out.envelope, std::memory_order_relaxed);
  g_rms.store(snap.rms, std::memory_order_relaxed);
  g_spectrum.processBlock(mono, numFrames, sampleRate);
}

float            rms() { return g_rms.load(std::memory_order_relaxed); }
float            envelope() { return g_env.load(std::memory_order_relaxed); }
SpectrumSnapshot spectrum() { return g_spectrum.snapshot(); }

int runAudioMonitorSelfTest(bool injectBug) {
  const float sr = 48000.0f;
  const int N = 4096;
  std::vector<float> sine(N);
  for (int i = 0; i < N; ++i) sine[i] = std::sin(2.0f * (float)M_PI * 1000.0f * i / sr);
  onBlock(nullptr, sine.data(), N, sr);
  const float rLoud = rms();
  const SpectrumSnapshot sLoud = spectrum();
  int pk = 0;
  for (int b = 1; b < kBandCount; ++b)
    if (sLoud.bands[b] > sLoud.bands[pk]) pk = b;
  const bool loudOk = rLoud > 0.1f && std::abs(pk - 16) <= 1;  // 1000 Hz -> band ~16

  std::vector<float> z(N, 0.0f);
  onBlock(nullptr, z.data(), N, sr);
  const float rQuiet = rms();
  const bool quietOk = rQuiet < 0.05f;

  bool ok = loudOk && quietOk;
  if (injectBug) ok = !ok;
  std::printf("[selftest-audiomonitor] loudRms=%.3f peakBand=%d quietRms=%.3f -> %s\n",
              rLoud, pk, rQuiet, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::audio_monitor
