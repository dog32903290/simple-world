// runtime/playback_provider — see playback_provider.h. The position/speed write-mailbox singleton.
#include "runtime/playback_provider.h"

namespace sw {

PlaybackProvider& PlaybackProvider::instance() {
  static PlaybackProvider s_instance;  // Meyers singleton, init-order safe (mirror of BpmProvider).
  return s_instance;
}

void PlaybackProvider::setNewTime(float bars) {
  newTime_ = bars;
  timeArmed_ = true;
}

void PlaybackProvider::setNewSpeed(float speed) {
  newSpeed_ = speed;
  speedArmed_ = true;
}

bool PlaybackProvider::tryGetNewTime(float& out) {
  if (!timeArmed_) return false;  // no pending write → transport untouched (make-or-break)
  timeArmed_ = false;             // clear-each-frame: next frame needs a fresh arm
  out = newTime_;
  return true;
}

bool PlaybackProvider::tryGetNewSpeed(float& out) {
  if (!speedArmed_) return false;
  speedArmed_ = false;
  out = newSpeed_;
  return true;
}

void PlaybackProvider::resetForTest() {
  timeArmed_ = false;
  newTime_ = 0.0f;
  speedArmed_ = false;
  newSpeed_ = 1.0f;
}

}  // namespace sw
