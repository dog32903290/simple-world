#include "runtime/transport.h"

#include <cmath>

namespace sw {

void Transport::setRate(double r) {
  if (!std::isfinite(r)) return;  // refuse garbage, keep the last sane rate (BPM-gate style)
  // Clamp to TiXL's UI reach: speed doubling stops at ±16 (TimeControls.cs:92/106).
  if (r > 16.0) r = 16.0;
  if (r < -16.0) r = -16.0;
  rate = r;
}

void Transport::scrub(double bars) {
  position = bars < 0.0 ? 0.0 : bars;
  scrubbedThisFrame_ = true;  // next advance() snaps fxTime to position (Playback.cs:121)
}

void Transport::advance(double dtSecs) {
  // = Playback.cs:Update with idle-motion always on, no render-to-file leg.
  const double isPlayingEps = 0.001;  // Playback.cs:108 Math.Abs(PlaybackSpeed) > 0.001
  const bool isPlaying = playing() && std::abs(rate) > isPlayingEps;

  if (isPlaying) {
    // Playing: playhead advances, fxTime locked to it (Playback.cs:114-118).
    position += barsFromSeconds(dtSecs) * rate;
    fxTime = position;
  } else if (scrubbedThisFrame_) {
    // Paused + scrubbed this frame: fxTime snaps to the new playhead (Playback.cs:121-124).
    fxTime = position;
  } else {
    // Paused, not scrubbed: playhead frozen, fxTime keeps running (idle-motion, Playback.cs:126-129).
    // THIS is 暫停續跑 — particle/feedback time keeps flowing while the composition position holds.
    fxTime += barsFromSeconds(dtSecs);
  }

  scrubbedThisFrame_ = false;  // the manipulation is consumed by exactly one advance.
}

}  // namespace sw
