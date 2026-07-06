// platform/link_sync — Ableton Link session impl. Isolates the heavy asio/Link template instantiation
// to THIS one TU (the header is asio-free). See link_sync.h for the seam contract + TiXL mapping.
//
// GPLv2: this file compiles the vendored Ableton Link (third_party/ableton-link). See LICENSE.md there.
#include "platform/link_sync.h"

#include <chrono>
#include <cstdio>
#include <cmath>

// LINK_PLATFORM_MACOSX selects Link's Darwin clock/socket backend; ASIO_STANDALONE = header-only asio
// (no Boost). Both defined by the CMake target so any TU including Link.hpp agrees.
#include <ableton/Link.hpp>

namespace sw {

using Micros = std::chrono::microseconds;

struct LinkSync::Impl {
  ableton::Link link;
  double quantum = 4.0;
  explicit Impl(double bpm) : link(bpm) {}
};

LinkSync::LinkSync(double initialBpm) : impl_(new Impl(initialBpm)) {}
LinkSync::~LinkSync() { delete impl_; }

bool LinkSync::connected() const { return impl_ != nullptr; }

void LinkSync::enable(bool on) { impl_->link.enable(on); }
bool LinkSync::enabled() const { return impl_->link.isEnabled(); }
void LinkSync::enableStartStopSync(bool on) { impl_->link.enableStartStopSync(on); }

void LinkSync::setTempo(double bpm) {
  auto s = impl_->link.captureAppSessionState();
  s.setTempo(bpm, impl_->link.clock().micros());
  impl_->link.commitAppSessionState(s);
}

void LinkSync::setQuantum(double q) { impl_->quantum = q; }

void LinkSync::requestBeatAtTime(double beat) {
  auto s = impl_->link.captureAppSessionState();
  s.requestBeatAtTime(beat, impl_->link.clock().micros(), impl_->quantum);
  impl_->link.commitAppSessionState(s);
}

void LinkSync::startPlaying() {
  auto s = impl_->link.captureAppSessionState();
  s.setIsPlaying(true, impl_->link.clock().micros());
  impl_->link.commitAppSessionState(s);
}

void LinkSync::stopPlaying() {
  auto s = impl_->link.captureAppSessionState();
  s.setIsPlaying(false, impl_->link.clock().micros());
  impl_->link.commitAppSessionState(s);
}

// Fill a snapshot from a session state at host time `now` (µs). Shared by production snapshot() and the
// test-only fixed-clock variant so both read the SAME timeline math (no divergence between prod + golden).
static LinkSnapshot fill(const ableton::Link::SessionState& s, double quantum, double nowMicros,
                         int peers) {
  LinkSnapshot out;
  const Micros t((long long)llround(nowMicros));
  out.beat = s.beatAtTime(t, quantum);
  out.phase = s.phaseAtTime(t, quantum);
  out.tempo = s.tempo();
  out.quantum = quantum;
  out.timeMicros = nowMicros;
  out.peerCount = peers;
  out.isPlaying = s.isPlaying();
  out.isConnected = true;
  return out;
}

LinkSnapshot LinkSync::snapshot() const {
  const double now = (double)impl_->link.clock().micros().count();
  auto s = impl_->link.captureAppSessionState();
  return fill(s, impl_->quantum, now, (int)impl_->link.numPeers());
}

LinkSnapshot LinkSync::snapshotAtHostMicros(double hostMicros) const {
  auto s = impl_->link.captureAppSessionState();
  return fill(s, impl_->quantum, hostMicros, (int)impl_->link.numPeers());
}

void LinkSync::requestBeatAtHostMicros(double beat, double hostMicros) {
  auto s = impl_->link.captureAppSessionState();
  s.requestBeatAtTime(beat, Micros((long long)llround(hostMicros)), impl_->quantum);
  impl_->link.commitAppSessionState(s);
}

}  // namespace sw
