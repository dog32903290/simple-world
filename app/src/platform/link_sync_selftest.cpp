// platform/link_sync_selftest — --selftest-linksync. Closed-form proof of the LOCAL (no-peer) Link
// session timeline: the part of AbletonLinkSync that is deterministic WITHOUT a network peer.
//
// Ground truth (independent of Link internals — pure tempo/quantum math): after requestBeatAtTime(0),
// the timeline anchors beat 0 at the anchor host time t0; beatAtTime advances at tempo/60 beats/sec, so
// at host time t0+Δ (µs) beat == (tempo/60) * (Δ/1e6). phase == positive-fmod(beat, quantum). The Link
// invariant fmod(beatAtTime(t,q),q) == phaseAtTime(t,q) holds. These are the values TiXL's Result switch
// (cs:66-74) reads: Beats = beat, Bars = beat/quantum, Phase = phase, Quantum = quantum, Time = t/1000.
//
// Peer SYNC (two machines agreeing a shared beat) is EMERGENT + hardware — DEFERRED-HW-VERIFY, not here.
//
// injectBug (setLinkSyncBug, corrupts a REAL term the golden reads — not a flipped expectation):
//   1 = ANCHOR THE WRONG BEAT (requestBeatAtTime(2.0) instead of 0.0) → the whole timeline shifts a
//       full 2 beats, a decisive divergence from every closed-form want (the anchor is load-bearing).
//   2 = SWAP phase into the beat field (channel confusion) → probes where beat≠phase bite.
#include "platform/link_sync.h"

#include <cmath>
#include <cstdio>

namespace sw {

// File-local teeth (0 = production). Set only by this selftest via setLinkSyncBug (declared below).
static int g_linkSyncBug = 0;
void setLinkSyncBug(int mode) { g_linkSyncBug = mode; }

namespace {
int g_fail = 0;
void expectClose(const char* what, double got, double want) {
  const bool ok = std::fabs(got - want) < 1e-4;
  if (!ok) { ++g_fail; printf("  [linksync] FAIL %s got=%.7f want=%.7f\n", what, got, want); }
  else printf("  [linksync] ok   %s = %.7f\n", what, got);
}

double posFmod(double a, double m) { double r = std::fmod(a, m); return r < 0 ? r + m : r; }

// One FULLY deterministic timeline probe: build a session at `bpm`, quantum `q`, pick an anchor host
// time T0 = the live clock (captured ONCE), anchor beat 0 there, then read the snapshot at host
// T0+deltaMicros. Both anchor and read are at SPECIFIED host times (no live-clock drift between them),
// so beat/phase are exactly the closed-form values. bug 1 anchors the WRONG beat into the REAL
// requestBeatAtHostMicros → the whole timeline shifts 2 beats. (An old bug 2 swapped s.beat/s.phase in
// the test harness AFTER the read — that corrupted no production path, only proved the assert could tell
// two numbers apart (P3, removed 2026-07-06). A snapshot-tier seam would have to live inside the GPL
// dylib boundary — not worth it; bug 1 carries the bite.)
LinkSnapshot probe(double bpm, double q, double /*t0*/, double deltaMicros) {
  LinkSync link(bpm);
  link.setQuantum(q);
  // T0 = the live clock captured once (must be in Link's clock domain — its timeline math is host-clock
  // relative). Anchor + read are computed FROM this single T0, so no drift enters the golden.
  const double T0 = link.snapshot().timeMicros;
  // Anchor beat 0 at T0 (deterministic origin). bug 1 anchors the WRONG beat (2.0) → the entire timeline
  // shifts by a full 2 beats, a DECISIVE divergence from the closed-form wants (not a thin near-miss).
  link.requestBeatAtHostMicros(g_linkSyncBug == 1 ? 2.0 : 0.0, T0);
  LinkSnapshot s = link.snapshotAtHostMicros(T0 + deltaMicros);
  return s;
}
}  // namespace

void runLinkSyncGoldens() {
  // A: 120 bpm (2 beats/sec), quantum 4. At +250ms → beat 0.5; +1s → beat 2.0; +2s → beat 4.0 → phase 0.
  {
    LinkSnapshot s250 = probe(120.0, 4.0, 0.0, 250000.0);
    expectClose("120bpm +250ms beat", s250.beat, 0.5);
    expectClose("120bpm +250ms phase", s250.phase, posFmod(0.5, 4.0));
    expectClose("120bpm tempo readback", s250.tempo, 120.0);
    expectClose("120bpm quantum readback", s250.quantum, 4.0);

    LinkSnapshot s1 = probe(120.0, 4.0, 0.0, 1000000.0);
    expectClose("120bpm +1s beat", s1.beat, 2.0);
    expectClose("120bpm +1s phase (2 mod 4)", s1.phase, 2.0);

    LinkSnapshot s2 = probe(120.0, 4.0, 0.0, 2000000.0);
    expectClose("120bpm +2s beat", s2.beat, 4.0);
    expectClose("120bpm +2s phase wraps to 0", s2.phase, 0.0);
    // Link invariant: fmod(beat,q) == phase (production path).
    expectClose("120bpm +2s invariant fmod==phase", posFmod(s2.beat, 4.0), s2.phase);
  }
  // B: 140 bpm (7/3 beats/sec), quantum 3. At +900ms → beat = (140/60)*0.9 = 2.1; phase = 2.1 mod 3 = 2.1.
  {
    LinkSnapshot s = probe(140.0, 3.0, 0.0, 900000.0);
    expectClose("140bpm/q3 +900ms beat", s.beat, (140.0 / 60.0) * 0.9);
    expectClose("140bpm/q3 +900ms phase", s.phase, posFmod((140.0 / 60.0) * 0.9, 3.0));
    expectClose("140bpm tempo readback", s.tempo, 140.0);
  }
  // C: quantum wrap — 90 bpm (1.5 beats/sec), quantum 2. At +2s → beat 3.0; phase = 3 mod 2 = 1.0.
  {
    LinkSnapshot s = probe(90.0, 2.0, 0.0, 2000000.0);
    expectClose("90bpm/q2 +2s beat", s.beat, 3.0);
    expectClose("90bpm/q2 +2s phase (3 mod 2)", s.phase, 1.0);
  }
}

int runLinkSyncSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] linksync (Ableton Link local-session timeline: tempo/beat/phase closed-form)\n");

  setLinkSyncBug(0);
  runLinkSyncGoldens();
  printf("[selftest] linksync clean pass: %d failure(s)\n", g_fail);

  if (injectBug) {
    setLinkSyncBug(1);
    int b1 = g_fail;
    runLinkSyncGoldens();
    printf("[selftest] linksync bug1(wrong-anchor-beat) added %d failure(s)\n", g_fail - b1);
    setLinkSyncBug(0);
  }

  printf("[selftest] linksync %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw
