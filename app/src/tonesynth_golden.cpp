// tonesynth_golden — --selftest-tonesynth. Closed-form parity golden for the synthesis CORE of
// TiXL AudioToneGenerator (Operators/Lib/io/audio/AudioToneGenerator.cs ProceduralToneStream).
//
// AUTHORITY: every expected value is HAND-DERIVED from the .cs waveform formulas (impl-independent,
// GOLDEN_STANDARD 特徵 1) — never from sw's own output. TiXL GenerateSample (.cs:266-286):
//   normalizedPhase = phase - floor(phase/2π)*2π ;  t = normalizedPhase / 2π
//   Sine     = sin(normalizedPhase)
//   Square   = t < 0.5 ?  0.8 : -0.8
//   Sawtooth = (2t - 1) * 0.8
//   Triangle = (4*|t - 0.5| - 1) * 0.8
// and the phase increment (.cs:255): 2π*freq/sampleRate, with freq clamped to [20,20000] (.cs:331).
//
// PROBES sit at the DIVERGING MIDDLE (GOLDEN_STANDARD 特徵 2), never at phase 0 / peak / the exact
// t=0.5 discontinuity: sine at 1.0 & 2.0 rad (rise + past-peak descent), square at t=0.4775 & 0.557
// (straddling the 0.5 branch — the discriminator for a flipped comparison), sawtooth at t=0.159 &
// 0.796 (both ramp legs), triangle at t=0.159 & 0.637 (both slopes). A negative-phase probe pins the
// floor()-based wrap (.cs:268) that plain fmod would get wrong. Mentally swapping the body formula
// (angle scale / sign / missing 0.8 / t vs normalizedPhase) reddens at least one of these.
//
// TEETH (-bug): toneSynthInjectBug() corrupts the REAL toneWaveformSample output (×0.5 gain, a value
// no TiXL waveform emits) — the exact function the (future) node's audio callback fills its buffer
// with. The wants stay the hand-derived TiXL values for clean AND -bug (GOLDEN_STANDARD 特徵 3: corrupt
// the cook, never flip the want); under the bug every waveform assert diverges → the teeth bite. This
// is a real corruption of the callers' path (mirror of gradient_golden's corrupt()), not a want-flip.
// did-not-trip → return 0 (P1: NO-BITE list catches a dead tooth).
//
// SCOPE NOTE: the AudioToneGenerator NODE (Command/IsPlaying/GetLevel outputs) is deferred-hw-verify —
// all three outputs are BASS operator-mixer driven (AudioMixerManager + ChannelGetLevel), a per-operator
// audio subsystem the Mac chain does not have yet. This golden pins the ONLY impl-independent, closed-form
// part; the noise waveforms (WhiteNoise/PinkNoise, .cs:281-294) are RNG-stateful → no closed-form oracle,
// deliberately not pinned (GOLDEN_STANDARD: honest about what has no numeric anchor).
#include <cmath>
#include <cstdio>

#include "runtime/tone_synth.h"

namespace sw {
namespace {

constexpr double kTwoPi = 2.0 * M_PI;

// One probe: (phase, waveform, hand-derived TiXL want).
struct Probe {
  double       phase;
  ToneWaveform wf;
  double       want;
  const char*  label;
};

// All wants computed by hand from the .cs formulas (see python derivation in the commit message /
// header). Tolerance 1e-6 — closed-form, double precision.
const Probe kProbes[] = {
    // Sine (.cs:273) — sin(normalizedPhase). Mid-rise + past-peak descent (NOT 0/π/π2).
    {1.0, ToneWaveform::Sine,  0.8414709848078965, "sine@1.0"},
    {2.0, ToneWaveform::Sine,  0.9092974268256817, "sine@2.0"},
    // Square (.cs:275) — t<0.5?0.8:-0.8. t=0.4775 (+0.8) and t=0.557 (-0.8) STRADDLE the branch:
    // a flipped comparison or wrong 0.8 reddens exactly here.
    {3.0, ToneWaveform::Square,  0.8, "square@t=0.4775"},
    {3.5, ToneWaveform::Square, -0.8, "square@t=0.557"},
    // Sawtooth (.cs:277) — (2t-1)*0.8. Both ramp legs (t=0.159 rising-low, t=0.796 rising-high).
    {1.0, ToneWaveform::Sawtooth, -0.5453520910529674, "saw@t=0.159"},
    {5.0, ToneWaveform::Sawtooth,  0.4732395447351628, "saw@t=0.796"},
    // Triangle (.cs:279) — (4*|t-0.5|-1)*0.8. Both slopes (t=0.159 up-leg, t=0.637 down-leg).
    {1.0, ToneWaveform::Triangle,  0.2907041821059348, "tri@t=0.159"},
    {4.0, ToneWaveform::Triangle, -0.3628167284237396, "tri@t=0.637"},
    // Negative-phase WRAP (.cs:268 floor path): -1.0 rad ≡ 2π-1; want = sin(2π-1) = -0.8414709848.
    // fmod would give a wrong sign here — pins the operator's own wrap choice.
    {-1.0, ToneWaveform::Sine, -0.8414709848078966, "sine@-1.0(wrap)"},
};

bool closeEnough(double got, double want) { return std::fabs(got - want) <= 1e-6; }

}  // namespace

// Harness convention (run_all_selftests.sh --bite): -bug variant must exit NON-zero when the teeth
// bite. No inversion — the wants never move; the injected corruption makes the asserts diverge.
int runToneSynthSelfTest(bool injectBug) {
  auto runBattery = [&](const char* tag) {
    int fails = 0;
    for (const auto& p : kProbes) {
      const double got = toneWaveformSample(p.phase, p.wf);
      const bool pass = closeEnough(got, p.want);
      if (!pass) ++fails;
      std::printf("[tonesynth][%s] %-16s phase=%+.3f got=%+.9f want=%+.9f -> %s\n",
                  tag, p.label, p.phase, got, p.want, pass ? "PASS" : "FAIL");
    }
    // Phase-increment + freq-clamp pins (.cs:255 / .cs:331) — the pipeline math around the waveform.
    {
      const double incGot = tonePhaseIncrement(440.0, 48000);
      const double incWant = kTwoPi * 440.0 / 48000.0;  // 0.05759586531581288
      const bool pass = closeEnough(incGot, incWant);
      if (!pass) ++fails;
      std::printf("[tonesynth][%s] %-16s got=%+.9f want=%+.9f -> %s\n",
                  tag, "phaseInc440", incGot, incWant, pass ? "PASS" : "FAIL");
    }
    {
      const bool lowPass  = closeEnough(toneClampFrequency(10.0), 20.0);      // .cs:331 lower guard
      const bool highPass = closeEnough(toneClampFrequency(30000.0), 20000.0);// .cs:331 upper guard
      if (!lowPass)  ++fails;
      if (!highPass) ++fails;
      std::printf("[tonesynth][%s] %-16s clamp(10)=%.1f clamp(30000)=%.1f -> %s\n",
                  tag, "freqClamp", toneClampFrequency(10.0), toneClampFrequency(30000.0),
                  (lowPass && highPass) ? "PASS" : "FAIL");
    }
    return fails;
  };

  toneSynthInjectBug() = false;
  const int cleanFail = runBattery("clean");
  if (!injectBug) {
    std::printf("[tonesynth] %s (%d probe(s) failed)\n", cleanFail == 0 ? "PASS" : "FAIL", cleanFail);
    return cleanFail == 0 ? 0 : 1;
  }

  // -bug: corrupt the REAL synthesis output (×0.5) and rerun the SAME fixed wants. Every waveform
  // probe must now diverge. (The freq-clamp/phase-inc pins are unaffected by the gain bug — they
  // stay green; the waveform teeth carry the bite.)
  toneSynthInjectBug() = true;
  const int bugFail = runBattery("bug-gain0.5");
  toneSynthInjectBug() = false;  // restore production

  if (cleanFail != 0) {  // a broken clean run is a real red regardless of bite bookkeeping
    std::printf("[tonesynth] FAIL (clean run broken under -bug harness)\n");
    return 1;
  }
  // 8 waveform probes are corrupted by the ×0.5 gain (the two clamp pins + phase-inc are gain-immune,
  // so at least the 8 waveform asserts must fail; require the full 8 to bite).
  if (bugFail < 8) {
    std::printf("[tonesynth] injectBug did not trip (corrupted synthesis must fail every waveform probe)\n");
    return 0;  // dead tooth → NO-BITE list catches it
  }
  std::printf("[tonesynth] BITE (corrupted synthesis diverged %d asserts against fixed TiXL wants)\n",
              bugFail);
  return 1;
}

}  // namespace sw
