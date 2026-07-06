// platform/link_sync — Ableton Link session behind a pimpl (the ONE native seam for network beat
// sync). Owns an `ableton::Link` instance (GPLv2 — see third_party/ableton-link/LICENSE.md; vendored
// header-only + asio-standalone). The Link controller runs its own asio thread for peer discovery /
// clock sync (UDP multicast); this leaf hides all of it — callers include no asio and no Link header.
//
// = TiXL AbletonLinkSync.cs's native AbletonLinkDLL wrapper (the [DllImport] block, cs:165-276):
//   CreateAbletonLink → construct; setup/setTempo → tempo; update → snapshot beat/phase/tempo/
//   quantum/time/peerCount; startPlaying/stopPlaying/enableStartStopSync; numPeers. sw folds them
//   into one snapshot()+control surface so the app's per-frame cook reads a value struct (no threading
//   in the caller). The Result-selection math (Bars/Phase/Beats/…) lives in the RUNTIME node, not here.
//
// platform leaf: ZERO runtime/app dependency. The app (audio-clock owner) constructs one LinkSync,
// enables it, and each frame reads snapshot() to cook the AbletonLinkSync node — the same leaf-seam
// inversion audio_capture uses (platform owns the native handle; the layer above owns the meaning).
#pragma once
#include <cstdint>

namespace sw {

// One coherent sample of the Link session at snapshot() time (host-clock `now`). Mirrors TiXL's
// update() out-params (cs:269-275) + the connection/playing bits. All timeline reads use ONE captured
// host time so beat/phase are mutually consistent (= Link's captureAppSessionState + beatAtTime(now)).
struct LinkSnapshot {
  double beat = 0.0;        // beatAtTime(now, quantum) — beats since the session origin
  double phase = 0.0;       // phaseAtTime(now, quantum) ∈ [0, quantum)
  double tempo = 120.0;     // sessionState.tempo() — BPM
  double quantum = 4.0;     // the quantum (bar length in beats) this session syncs to
  double timeMicros = 0.0;  // the captured host time in microseconds (= TiXL update()'s `time`)
  int    peerCount = 0;     // numPeers() — other Link participants on the network (0 = solo)
  bool   isPlaying = false; // sessionState.isPlaying() (start/stop sync)
  bool   isConnected = false;  // the Link instance exists (constructed) — TiXL _nativeLinkInstance != 0
};

// The native Link session. Non-copyable (owns a thread + socket). Construction does NOT enable network
// communication (TiXL enable() gate); call enable(true) to join the network. A no-peer enabled session
// still runs a deterministic LOCAL timeline (the closed-form the golden verifies).
class LinkSync {
 public:
  explicit LinkSync(double initialBpm = 120.0);
  ~LinkSync();
  LinkSync(const LinkSync&) = delete;
  LinkSync& operator=(const LinkSync&) = delete;

  bool connected() const;              // the Link instance was constructed (= TiXL IsConnected)
  void enable(bool on);                // join / leave the network (Link.enable) — network comms gate
  bool enabled() const;
  void enableStartStopSync(bool on);   // TiXL EnableStartStopSync (cs:237-240)

  void setTempo(double bpm);           // propose a tempo to the session (setTempo @ now)
  void setQuantum(double q);           // the bar length in beats (default 4)
  // Anchor the timeline so `beat` occurs at the current host time (= TiXL requestBeatAtTime, cs:218-224).
  // Deterministic-origin hook: requestBeatAtTime(0, now) makes the golden's timeline reproducible.
  void requestBeatAtTime(double beat);
  void startPlaying();                 // TiXL startPlaying (cs:243-248)
  void stopPlaying();                  // TiXL stopPlaying (cs:250-256)

  // Capture one coherent snapshot at the current host clock. When the Link instance failed to construct
  // (should not happen on macOS) isConnected=false and the fields hold the defaults (TiXL's not-connected
  // early-out, cs:27-31).
  LinkSnapshot snapshot() const;

  // TEST-ONLY: capture a snapshot AS IF the host clock were `hostMicros` (deterministic golden origin).
  // Production snapshot() uses the real host clock; this lets the closed-form golden pin beat/phase to a
  // known host time without racing the wall clock. Not used by production.
  LinkSnapshot snapshotAtHostMicros(double hostMicros) const;
  // TEST-ONLY: anchor `beat` at a SPECIFIED host time (not the live clock), so the golden controls both
  // the anchor and the read instant exactly (no live-clock drift). Not used by production.
  void requestBeatAtHostMicros(double beat, double hostMicros);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

// Headless RED→GREEN proof of the LOCAL (no-peer) session timeline (--selftest-linksync): with a known
// tempo + requestBeatAtTime(0) origin, beatAtTime advances at tempo/60 beats/sec and phase = beat mod
// quantum, the Link invariant fmod(beatAtTime(t,q),q) == phaseAtTime(t,q) holds, and setTempo/setQuantum
// are read back. injectBug corrupts one term so the fixed wants bite. Peer SYNC is deferred-hw-verify.
int runLinkSyncSelfTest(bool injectBug);

}  // namespace sw
