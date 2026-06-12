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

// The speed window this audio chain can voice = the AVAudioUnitVarispeed parameter range
// (mirrors platform AudioPlayback::kRateMin/kRateMax — static_assert-pinned in soundtrack.cpp).
// NAMED FORK vs TiXL: BASS frequency scaling voices the full ±16 incl. BACKWARDS
// (SoundtrackClipStream.cs:49-54 ReverseDirection + Frequency*-speed); varispeed can't reverse
// and stops at 4x/0.25x — outside the window the stream PAUSES (visuals keep the full rate).
inline constexpr double kAudibleSpeedMin = 0.25;
inline constexpr double kAudibleSpeedMax = 4.0;

// One frame's follow decision — the pure heart of the sync, headless-testable. Inputs are the
// transport state (is the playhead moving; at what speed; where is it, mapped to SECONDS) and
// the audio state.
//   None         keep going (drift within threshold, or correctly silent)
//   Pause        stop the stream (transport paused, speed==0/unvoiceable, or target outside file)
//   Resync       hard-seek the playing stream to targetSecs (drift exceeded threshold)
//   PlayAtTarget seek to targetSecs and start the stream (transport playing, audio wasn't)
// speed semantics (TiXL UpdateSoundtrackTime):
//   • speed==0 == pause (cs:159-163) — even with the transport nominally Playing, a zero rate
//     freezes the playhead (Playback.cs:108 eps), so the stream must pause with it.
//   • drift gate carries the speed factor on BOTH sides (soundDelta×speed cs:208, threshold×
//     |speed| cs:221) — they cancel for speed≠0, so the OBSERVABLE is that the gate is
//     speed-invariant; we mirror the formula anyway (parity over cleverness).
//   • negative / out-of-window speed -> Pause (named fork above; TiXL would reverse/scale).
enum class Action { None, Pause, Resync, PlayAtTarget };
Action decide(bool transportPlaying, double speed, double targetSecs, double durationSecs,
              bool audioPlaying, double audioPosSecs);

// Apply one frame of the follow rule. Called from app/frame_cook::run AFTER the transport
// advanced. Also the (re)load watcher: when lib.composition.soundtrackPath changed (open/new/
// undo/dialog) it loads the new file — a failed path warns into doc::g_status once and is not
// retried until the path changes or the dialog re-picks it. NAMED FORK: TiXL's soundtrack path
// retries every frame (PlaybackUtils.cs:35 builds a fresh clip handle per frame; the
// HandleFileChange fail-cache lives in the OPERATOR-audio path and its skip condition is
// `|| true` — never skips). Our no-per-frame-retry is deliberate (no 60Hz open() churn);
// applySoundtrackPick is the explicit-retry escape hatch.
// targetSecs = transport.secondsFromBars(position); transportPlaying = transport.playing();
// speed = transport.rate. A changed in-window speed is pushed onto the platform varispeed
// (only on change — TiXL AudioEngine.cs:236-254 playbackSpeedChanged gate) before the action.
void syncFrame(bool transportPlaying, double speed, double targetSecs);

// Write `path` as the composition soundtrack (savev2 persists it; the dirty flag is
// snapshot-derived so the write alone marks the doc dirty) and clear the failure cache — an
// EXPLICIT pick always retries, even the exact path that just failed (柏為 fixes the file on
// disk and re-picks it: must work, refuter-C 修4). The actual load happens in the next
// syncFrame, same seam as open/undo.
void applySoundtrackPick(const std::string& path);

// Open-file dialog (NFDe) -> applySoundtrackPick(picked). Returns false on cancel.
bool promptAndLoad();

// Toolbar status: loaded file name, last load error, or "" when no soundtrack is set.
std::string statusText();

// Headless teeth (--selftest-soundtrack): the decide() state machine over play/pause/scrub/
// drift/out-of-bounds/speed sequences (speed-invariant drift gate, speed==0=pause — the
// refuter-C "rate=0+Playing" trap, negative & out-of-window speed pause as the named fork),
// the bars->secs targeting at two BPMs (audio stays in the seconds domain), the varispeed
// rate clamp + a live 4x-outruns-the-wall-clock race (needs an output device, SKIPs without),
// the platform load() failure path (missing file -> false, no crash) +
// a real tiny WAV roundtrip (load -> duration -> seek -> position readback, no audio device
// needed), the live engine legs (修1 config-change restart / 修3 end-seek playing-flag drop —
// need an output device, SKIP without one), and the 修4 failure-cache retry seam (explicit
// re-pick retries, per-frame does not). injectBug flips the drift comparison -> FAIL.
int runSoundtrackSelfTest(bool injectBug);

}  // namespace sw::soundtrack
