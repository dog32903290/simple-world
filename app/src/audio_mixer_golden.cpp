// audio_mixer_golden — platform/audio_mixer ISOLATION selftest (--selftest-audio-mixer).
//
// LAYER (a) of the audio-playback seam's honest three-layer verification (see audio_playback_cook.h):
// the MIXER MACHINE's create/query/state-transition contract is CLOSED-FORM and device-independent, so it
// is verified here WITHOUT a live audio device or a real file. The one thing that needs the audio thread —
// the actual max-abs computed FROM a decoded buffer — is NOT reproduced here (that is deferred-hw-verify,
// real sound out of the speakers). Instead debugInjectLevelBlock feeds a synthetic last-block max-abs and
// this golden asserts the mixer's level READOUT clamp+gate against HAND-DERIVED CONSTANTS from the .cs:
//   • level() == min(maxAbs, 1) while playing & unpaused (OperatorAudioStreamBase.cs:344 / export cs:320).
//   • level() == 0 when not playing OR paused (cs:336-337) — the state gate, independent of the block value.
//   • absent key → 0; paused()/playing() reflect the injected flags.
// Every `want` below is a literal hand-computed from the TiXL formula (GOLDEN_STANDARD 特徵1: the oracle is
// independent-of-impl, NOT read back from the mixer). Probes sit at DIVERGING MIDDLES (a >1 value the clamp
// bites, a paused/stopped row the gate bites) — not identity/saturation (特徵2).
//
// TOOTH: injectBug latches setAudioMixerBug(true), which CORRUPTS level()'s real clamp+gate in
// platform/audio_mixer.mm (skips the playing/paused gate AND the min(peak,1) clamp — the exact production
// lines this golden asserts). The C2 diverging-middle probes then diverge: the >1 row no longer clamps to 1,
// the paused/stopped rows no longer gate to 0 → ok goes false → exit 1. (Initial version mis-judged the
// clamp+gate as having "no severable seam"; a sticky module latch that rots the REAL readout path is sw's
// standard injection convention — cf. setAudioPlaybackBug / hostScalarInjectBug — NOT a fake flag.)
#include <cmath>
#include <cstdio>

#include "platform/audio_mixer.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
bool approx(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}  // namespace

int runAudioMixerSelfTest(bool injectBug) {
  AudioMixer mix;
  bool ok = true;

  // TOOTH: corrupt the REAL level() clamp+gate for the whole run, then run the SAME probes. The C2
  // diverging-middle rows then read wrong (1.70 not clamped, paused/stopped not gated) → ok false → exit 1.
  setAudioMixerBug(injectBug);

  // ── C1. Absent key: everything reads empty/0 (no stream created). ──────────────────────────────────
  ok = ok && !mix.hasStream("k1");
  ok = ok && mix.streamCount() == 0;
  ok = ok && approx(mix.level("k1"), 0.0);
  ok = ok && !mix.playing("k1") && !mix.paused("k1");
  ok = ok && approx(mix.durationSeconds("k1"), 0.0);

  // ── C2. Level clamp + gate. Each row: inject (maxAbs, playing, paused), assert level() vs the literal
  //        hand-derived want = playing&&!paused ? min(maxAbs,1) : 0. ─────────────────────────────────
  mix.debugInjectLevelBlock("lk", 0.42, /*playing=*/true, /*paused=*/false);
  ok = ok && approx(mix.level("lk"), 0.42);   // mid-range, ungated, unclamped
  mix.debugInjectLevelBlock("lk", 1.70, /*playing=*/true, /*paused=*/false);
  ok = ok && approx(mix.level("lk"), 1.00);   // >1 → CLAMP to 1 (min(·,1), cs:344) — DIVERGING MIDDLE
  mix.debugInjectLevelBlock("lk", 0.90, /*playing=*/false, /*paused=*/false);
  ok = ok && approx(mix.level("lk"), 0.00);   // not playing → GATE to 0 (cs:336) — DIVERGING MIDDLE
  mix.debugInjectLevelBlock("lk", 0.90, /*playing=*/true, /*paused=*/true);
  ok = ok && approx(mix.level("lk"), 0.00);   // paused → GATE to 0 (cs:337) — DIVERGING MIDDLE
  mix.debugInjectLevelBlock("lk", 0.00, /*playing=*/true, /*paused=*/false);
  ok = ok && approx(mix.level("lk"), 0.00);   // silent playing → 0

  // ── C3. State transitions reflect the injected flags. ──────────────────────────────────────────────
  mix.debugInjectLevelBlock("sk", 0.5, /*playing=*/true, /*paused=*/false);
  ok = ok && mix.playing("sk") && !mix.paused("sk") && approx(mix.level("sk"), 0.5);
  mix.debugInjectLevelBlock("sk", 0.5, /*playing=*/false, /*paused=*/true);
  ok = ok && !mix.playing("sk") && mix.paused("sk") && approx(mix.level("sk"), 0.0);

  setAudioMixerBug(false);   // restore production (sticky module switch)

  std::printf("[selftest-audio-mixer] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// order 720 — after the io family (310) / io-video-timing (710-718).
REGISTER_SELFTESTS(/*orderBase=*/720, {"audio-mixer", sw::runAudioMixerSelfTest});

}  // namespace sw
