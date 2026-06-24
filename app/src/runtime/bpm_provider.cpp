// runtime/bpm_provider — see bpm_provider.h. Verbatim port of TiXL Core/IO/BpmProvider.cs:22-33.
#include "runtime/bpm_provider.h"

namespace sw {

BpmProvider& BpmProvider::instance() {
  // = BpmProvider.Instance (a static readonly singleton, BpmProvider.cs:12). Meyers singleton: one
  // process-global, constructed on first use, init-order safe.
  static BpmProvider s_instance;
  return s_instance;
}

void BpmProvider::setNewBpmRate(float rate) {
  // = SetBpm.cs:38-39: BpmProvider.Instance.NewBpmRate = clampedRate; .SetBpmTriggered = true.
  newBpmRate_ = rate;
  setBpmTriggered_ = true;
}

bool BpmProvider::tryGetNewBpmRate(float& out) {
  // = BpmProvider.cs:22-33 verbatim. Not armed → hand back the stale rate, return false (cs:24-28).
  if (!setBpmTriggered_) {
    out = newBpmRate_;
    return false;
  }
  // Armed → clear the trigger (cs:30), hand back the rate (cs:31), return true (cs:32). Clear-on-read:
  // a 2nd pull in the same un-re-armed window returns false (the make-or-break triggered-pull).
  setBpmTriggered_ = false;
  out = newBpmRate_;
  return true;
}

void BpmProvider::resetForTest() {
  setBpmTriggered_ = false;
  newBpmRate_ = 0.0f;
}

}  // namespace sw
