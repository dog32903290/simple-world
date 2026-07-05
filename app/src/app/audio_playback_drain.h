// app/audio_playback_drain — cooks the io/audio playback nodes then replays the resulting AudioPlaybackBus
// commands onto the app-owned platform AudioMixer, clearing the bus. Zone: app (app→runtime + app→platform
// both legal; owns the single process-wide mixer instance). The app is the ONLY writer-to-hardware for the
// playback nodes — the mirror of the io-output drain and of app/soundtrack owning the platform AudioPlayback:
// the runtime nodes stay pure (no AVFoundation include), the app bridges the command seam to the real engine.
//
// The audible output is deferred-hw-verify (no golden asserts real sound out of the speakers). The command
// SEQUENCE + VALUES are verified upstream — audio_playback_golden (bus commands + ADSR-gated gain) and
// audio_mixer_golden (the mixer level readout contract) — so this bridge is a thin, verified replay.
#pragma once

namespace sw {

struct ResidentEvalGraph;   // runtime/resident_eval_graph.h

// Called once per frame by frame_cook. Cooks every AudioPlayer/PlayAudioClip node (fxSecs = LocalFxTime
// seconds, runTimeSecs = wall accumulator), then replays the emitted AudioPlaybackBus commands onto the
// process-wide mixer (lazy-constructed on the first frame that carries any command — no engine spins up
// until a playback node actually cooks) and clears the bus for next frame.
void cookAndDrainAudioPlayback(ResidentEvalGraph& g, float fxSecs, double runTimeSecs);

}  // namespace sw
