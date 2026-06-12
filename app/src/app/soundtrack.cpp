#include "app/soundtrack.h"

#include <nfd.hpp>

#include <cmath>
#include <string>

#include "app/document.h"              // lib.composition.soundtrackPath + g_status
#include "platform/audio_playback.h"   // the platform leaf this module drives

namespace sw::soundtrack {
namespace {

// The one playback instance (the soundtrack is a singleton resource, like the capture unit).
AudioPlayback& playback() {
  static AudioPlayback p;
  return p;
}

// (Re)load watcher state: the path the playback currently has open, and the path that last
// failed. NAMED FORK (refuter-C 修4): TiXL's soundtrack path retries EVERY frame —
// PlaybackUtils.cs:35 hands AudioEngine.UseSoundtrackClip a fresh handle per frame, so a bad
// file is re-attempted at 60Hz. (AudioEngine.HandleFileChange's FailedFilePath cache is the
// OPERATOR-audio path, and its skip condition is `... || true` — always true, dead letter.)
// We deliberately do NOT retry per frame (no 60Hz open() churn on a missing file); the cache
// clears when the path changes OR when 柏為 explicitly re-picks via the dialog
// (applySoundtrackPick), which always retries even the same path.
std::string g_loadedPath;
std::string g_failedPath;

std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Pull lib.composition.soundtrackPath onto the playback when it changed — the same
// frame-boundary pull frame_cook uses for BPM, so open/new/undo/dialog all converge here
// with no edge wiring.
void reloadIfPathChanged() {
  const std::string& want = doc::g_lib.composition.soundtrackPath;
  if (want == g_loadedPath) return;
  if (!want.empty() && want == g_failedPath) return;  // cached failure: don't retry-spam
  playback().unload();
  g_loadedPath.clear();
  if (want.empty()) { g_failedPath.clear(); return; }
  if (playback().load(want)) {
    g_loadedPath = want;
    g_failedPath.clear();
    doc::g_status = "soundtrack: " + baseName(want);
  } else {
    g_failedPath = want;  // warn once (load() already printed the cause), keep running
    doc::g_status = "soundtrack failed to load: " + baseName(want);
  }
}

}  // namespace

Action decide(bool transportPlaying, double targetSecs, double durationSecs, bool audioPlaying,
              double audioPosSecs) {
  // Transport paused -> stream paused, NO seek (TiXL UpdateSoundtrackTime:159-163). Scrubbing
  // while paused only moves the target; the stream stays silent until play.
  if (!transportPlaying) return audioPlaying ? Action::Pause : Action::None;

  // Target outside the file (playhead before bar 0 can't happen here, but past the end can):
  // pause the stream rather than letting it dangle (UpdateSoundtrackTime:183-198).
  if (targetSecs < 0.0 || targetSecs >= durationSecs) return audioPlaying ? Action::Pause : Action::None;

  // Transport playing, audio not: enter at the target (TiXL unpauses then the same-frame
  // drift check resyncs, UpdateSoundtrackTime:200-228 — we fold that into one action).
  if (!audioPlaying) return Action::PlayAtTarget;

  // Both running: let the audio free-run; hard-seek only past the drift threshold, because
  // frequent resync = audible glitches (SoundtrackClipStream.cs:149-156).
  const double drift = std::fabs(audioPosSecs - targetSecs);
  return drift > kResyncThresholdSecs ? Action::Resync : Action::None;
}

void syncFrame(bool transportPlaying, double targetSecs) {
  reloadIfPathChanged();
  AudioPlayback& p = playback();
  if (!p.loaded()) return;
  switch (decide(transportPlaying, targetSecs, p.durationSeconds(), p.playing(),
                 p.positionSeconds())) {
    case Action::None: break;
    case Action::Pause: p.pause(); break;
    case Action::Resync: p.seek(targetSecs); break;
    case Action::PlayAtTarget:
      p.seek(targetSecs);
      p.play();
      break;
  }
}

void applySoundtrackPick(const std::string& path) {
  doc::g_lib.composition.soundtrackPath = path;  // savev2 home; dirty via snapshot
  g_failedPath.clear();  // an explicit pick ALWAYS retries, even a previously failed same path
}

bool promptAndLoad() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"audio file", "wav,aiff,aif,mp3,m4a,flac,caf"}};
  nfdresult_t r = NFD::OpenDialog(outPath, filters, 1, nullptr);
  if (r != NFD_OKAY) return false;  // cancel or error
  applySoundtrackPick(outPath.get());
  return true;
}

std::string statusText() {
  const std::string& want = doc::g_lib.composition.soundtrackPath;
  if (want.empty()) return "no soundtrack";
  if (want == g_failedPath) return "load failed: " + baseName(want);
  return baseName(want);
}

}  // namespace sw::soundtrack
