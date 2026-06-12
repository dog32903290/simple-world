#include "app/soundtrack.h"

#include <nfd.hpp>

#include <cmath>
#include <string>

#include "app/document.h"              // lib.composition.soundtrackPath + g_status
#include "platform/audio_playback.h"   // the platform leaf this module drives

namespace sw::soundtrack {

// The audible window in the header is a mirror of the platform varispeed range — if the
// platform constant ever moves, this trips at compile time instead of silently de-syncing
// the pause window from what the hardware can voice.
static_assert(kAudibleSpeedMin == AudioPlayback::kRateMin &&
                  kAudibleSpeedMax == AudioPlayback::kRateMax,
              "soundtrack audible-speed window must mirror AudioPlayback's varispeed range");

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

// The production follow-rule state (the soundtrack singleton's). Reset whenever the loaded
// file changes — a pending seek / lockout from the previous file means nothing to the new one.
FollowState g_followState;

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
  g_followState = FollowState{};  // stale pending-seek/lockout belongs to the old file
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

// Ceiling-hysteresis step (refuter-E3 修3, named fork — see header). Pure so the flutter
// sequence is headless-testable; the state itself lives in FollowState.
bool speedLockoutStep(bool lockedOut, double speed) {
  if (speed > kAudibleSpeedMax) return true;            // excursion above the ceiling locks out
  return lockedOut && speed > kAudibleSpeedReentry;     // hold through the (3.8, 4.0] band
}

Action decide(bool transportPlaying, double speed, double targetSecs, double durationSecs,
              bool audioPlaying, double audioPosSecs, bool speedLockedOut) {
  // Transport paused -> stream paused, NO seek (TiXL UpdateSoundtrackTime:159-163). Scrubbing
  // while paused only moves the target; the stream stays silent until play.
  // Same leg covers every speed the chain can't voice:
  //   • speed==0 == pause (cs:159-163) — the playhead is frozen by Playback.cs:108's eps gate
  //     even when the play STATE says Playing (the refuter-C "rate=0 freezes the playhead while
  //     the music keeps running" trap dies here).
  //   • speed < 0: NAMED FORK — TiXL plays the soundtrack BACKWARDS (BASS ReverseDirection +
  //     Frequency*-speed, SoundtrackClipStream.cs:49-54); AVAudioUnitVarispeed cannot reverse,
  //     so the stream pauses while the visuals run backwards at full rate.
  //   • |speed| outside [0.25, 4]: NAMED FORK — BASS frequency-scales to ±16 (TimeControls.cs:
  //     106 caps the UI there); varispeed stops at 4x/0.25x. Chasing a 16x playhead with 4x
  //     audio would resync-glitch every few frames — pause instead, deterministically silent.
  //   • speedLockedOut: the 修3 hysteresis — after an excursion above the ceiling, in-window
  //     speeds in (3.8, 4.0] stay silent so a knob fluttering across 4.0 can't machine-gun
  //     Pause/PlayAtTarget (named fork, no TiXL window edge to flutter on).
  const bool voiceable =
      !speedLockedOut && speed >= kAudibleSpeedMin && speed <= kAudibleSpeedMax;
  if (!transportPlaying || !voiceable) return audioPlaying ? Action::Pause : Action::None;

  // Target outside the file (playhead before bar 0 can't happen here, but past the end can):
  // pause the stream rather than letting it dangle (UpdateSoundtrackTime:183-198).
  if (targetSecs < 0.0 || targetSecs >= durationSecs) return audioPlaying ? Action::Pause : Action::None;

  // Transport playing, audio not: enter at the target (TiXL unpauses then the same-frame
  // drift check resyncs, UpdateSoundtrackTime:200-228 — we fold that into one action).
  if (!audioPlaying) return Action::PlayAtTarget;

  // Both running: let the audio free-run; hard-seek only past the drift threshold, because
  // frequent resync = audible glitches (SoundtrackClipStream.cs:149-156). The speed factor
  // rides BOTH sides — soundDelta×speed (cs:208) vs threshold×|speed| (cs:221) — which cancels
  // for any voiceable speed; mirrored verbatim anyway so the formula stays line-mappable.
  const double soundDelta = (audioPosSecs - targetSecs) * speed;
  const double maxSoundDelta = kResyncThresholdSecs * std::fabs(speed);
  return std::fabs(soundDelta) > maxSoundDelta ? Action::Resync : Action::None;
}

Action followFrame(FollowState& st, AudioPlayback& p, bool transportPlaying, double speed,
                   double targetSecs) {
  st.speedLockedOut = speedLockoutStep(st.speedLockedOut, speed);
  Action a = decide(transportPlaying, speed, targetSecs, p.durationSeconds(), p.playing(),
                    p.positionSeconds(), st.speedLockedOut);

  // Seek-settle guard (named fork, see header): while our last seek's render restart is still
  // in flight, positionSeconds() is frozen at the seek readback — drift is unmeasurable and the
  // aimed-ahead target shows up as fake +offset×rate drift. Hold Resync until the render clock
  // actually moves (a real restart = 2-3 frames), or give up after kSeekSettleMaxFrames so a
  // zombie engine still gets bounded retry instead of eternal silence.
  if (a == Action::Resync && st.pendingSeekPos >= 0.0) {
    const bool rendered = p.positionSeconds() > st.pendingSeekPos + 1e-6;
    if (!rendered && st.settleFrames < kSeekSettleMaxFrames) {
      ++st.settleFrames;
      return Action::None;
    }
    st.pendingSeekPos = -1.0;  // render confirmed (or bound tripped): drift rule resumes
    st.settleFrames = 0;
  }

  switch (a) {
    case Action::None: break;
    case Action::Pause:
      p.pause();
      st.pendingSeekPos = -1.0;  // nothing in flight once the stream is stopped
      st.settleFrames = 0;
      break;
    case Action::Resync:
      // The third speed (修1): aim AHEAD by the restart delay × rate (TiXL cs:226) so the
      // stream re-enters in sync instead of late by delay×rate (the resync machine gun).
      p.seek(targetSecs + resyncOffsetSecs(speed));
      st.pendingSeekPos = p.positionSeconds();  // post-clamp readback, the settle anchor
      st.settleFrames = 0;
      break;
    case Action::PlayAtTarget:
      // Entry IS a seek+start — TiXL unpauses then the same-frame drift check resyncs with the
      // same offset (UpdateSoundtrackTime:200-228 folded); carrying it here also kills the
      // permanent -restartDelay drift a 1x entry used to park on the stream.
      p.seek(targetSecs + resyncOffsetSecs(speed));
      p.play();
      st.pendingSeekPos = p.positionSeconds();
      st.settleFrames = 0;
      break;
  }
  return a;
}

void syncFrame(bool transportPlaying, double speed, double targetSecs) {
  reloadIfPathChanged();
  AudioPlayback& p = playback();
  if (!p.loaded()) return;
  // Push a changed IN-WINDOW speed onto the varispeed (= TiXL's playbackSpeedChanged gate,
  // AudioEngine.cs:236-254: UpdateSoundtrackPlaybackSpeed only when |Δspeed| > 0.001).
  // Out-of-window speeds never reach the platform — decide() expresses them as Pause, and the
  // last in-window rate stays parked on the unit for the return.
  if (speed >= kAudibleSpeedMin && speed <= kAudibleSpeedMax &&
      std::fabs(speed - p.rate()) > 0.001) {
    p.setRate(speed);
  }
  followFrame(g_followState, p, transportPlaying, speed, targetSecs);
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
