// app/soundtrack — keeps the composition's backing audio (platform/audio_playback) in sync
// with the Transport. Zone: app (app->platform legal; owns the playback instance and the
// follow rule; the UI only pokes promptAndLoad/statusText).
//
// The sync worldview is TiXL's (Core/Audio/SoundtrackClipStream.cs:149-229): the WALL CLOCK
// is master, audio FOLLOWS. Playback.Update advances TimeInBars from a Stopwatch — never from
// the audio clock — and each frame the soundtrack stream is handed the playback time as a
// TARGET. The audio stream free-runs at its native rate; only when |audioPos - target| exceeds
// AudioResyncThreshold (0.04s, CompositionSettings.cs:82) does it hard-seek, because
// "frequent resync causes audio glitches" (SoundtrackClipStream.cs:154). Pause = stream
// paused in the mixer, no seek (UpdateSoundtrackTime:159-163); scrub has NO special path —
// the target jumps, the drift check catches it (silent while paused, the stream stays
// paused). Out-of-bounds target (before 0 / past the file end) = pause the stream
// (UpdateSoundtrackTime:183-198). BPM maps bars->secs for the TARGET only — audio rate is
// never touched (audio lives in seconds).
#pragma once
#include <string>

namespace sw::soundtrack {

// = TiXL CompositionSettings.AudioResyncThreshold default (CompositionSettings.cs:82).
inline constexpr double kResyncThresholdSecs = 0.04;

// One frame's follow decision — the pure heart of the sync, headless-testable. Inputs are the
// transport state (is the playhead moving; where is it, mapped to SECONDS) and the audio state.
//   None         keep going (drift within threshold, or correctly silent)
//   Pause        stop the stream (transport paused, or target outside the file)
//   Resync       hard-seek the playing stream to targetSecs (drift exceeded threshold)
//   PlayAtTarget seek to targetSecs and start the stream (transport playing, audio wasn't)
enum class Action { None, Pause, Resync, PlayAtTarget };
Action decide(bool transportPlaying, double targetSecs, double durationSecs, bool audioPlaying,
              double audioPosSecs);

// Apply one frame of the follow rule. Called from app/frame_cook::run AFTER the transport
// advanced. Also the (re)load watcher: when lib.composition.soundtrackPath changed (open/new/
// undo/dialog) it loads the new file — a failed path warns into doc::g_status once and is not
// retried until the path changes (TiXL AudioEngine.HandleFileChange:776-831 caches failures).
// targetSecs = transport.secondsFromBars(position); transportPlaying = transport.playing().
void syncFrame(bool transportPlaying, double targetSecs);

// Open-file dialog (NFDe) -> write lib.composition.soundtrackPath (savev2 persists it; the
// dirty flag is snapshot-derived so the write alone marks the doc dirty). The actual load
// happens in the next syncFrame, same seam as open/undo. Returns false on cancel.
bool promptAndLoad();

// Toolbar status: loaded file name, last load error, or "" when no soundtrack is set.
std::string statusText();

// Headless teeth (--selftest-soundtrack): the decide() state machine over play/pause/scrub/
// drift/out-of-bounds sequences, the bars->secs targeting at two BPMs (audio stays in the
// seconds domain), and the platform load() failure path (missing file -> false, no crash) +
// a real tiny WAV roundtrip (load -> duration -> seek -> position readback, no audio device
// needed). injectBug flips the drift comparison -> FAIL.
int runSoundtrackSelfTest(bool injectBug);

}  // namespace sw::soundtrack
