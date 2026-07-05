// runtime/tap_provider — see tap_provider.h. Mirror of bpm_provider.cpp; the edge-detect (WasTriggered)
// is TiXL MathUtils.cs:531-538 verbatim, done inside setTriggers so the provider owns both memory + pulse.
#include "runtime/tap_provider.h"

#include <cmath>

namespace sw {

namespace {
// = MathUtils.WasTriggered(newState, ref current) (MathUtils.cs:531-538): return newState ONLY when it
// CHANGED (a false→true rising edge yields true; true→false yields false-but-stores; no change → false),
// then store newState into `current`. So a held-high level does NOT re-fire.
bool wasTriggered(bool newState, bool& current) {
  if (newState == current) return false;
  current = newState;
  return newState;
}
}  // namespace

TapProvider& TapProvider::instance() {
  // = TapProvider.Instance (static readonly singleton). Meyers singleton: one process-global, init-order safe.
  static TapProvider s_instance;
  return s_instance;
}

void TapProvider::setTriggers(bool beatLevel, bool resyncLevel) {
  // ForwardBeatTaps.cs:22-27: edge-detect each level against its stored previous, publish the pulse.
  beatTapTriggered_ = wasTriggered(beatLevel, wasBeatTriggered_);
  resyncTriggered_ = wasTriggered(resyncLevel, wasResyncTriggered_);
}

void TapProvider::setSlideSyncTime(float offset) {
  // ForwardBeatTaps.cs:30-35: only write when NOT NaN (NaN → keep the prior SlideSyncTime).
  if (!std::isnan(offset)) slideSyncTime_ = offset;
}

void TapProvider::resetForTest() {
  beatTapTriggered_ = false;
  resyncTriggered_ = false;
  slideSyncTime_ = 0.0f;
  wasBeatTriggered_ = false;
  wasResyncTriggered_ = false;
}

}  // namespace sw
