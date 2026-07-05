// runtime/audio_playback_bus — the per-frame audio-playback COMMAND seam for the io/audio playback
// operator NODES (AudioPlayer / PlayAudioClip / [later] SpatialAudioPlayer). The mixer-side mirror of
// io_device_bus: the RUNTIME node cook (frame_cook's cookAudioPlaybackNodes) reads each node's params +
// per-instance edge memory and APPENDS the exact stream commands it would issue this frame (ensure/play/
// stop/pause/gain/pan/rate/seek, keyed by the node's resident path); the APP drains those commands to the
// PLATFORM leaf (platform/audio_mixer) each frame, then clears the bus. runtime NEVER includes a platform
// header — the app is the only reader-to-hardware, exactly like io_device_bus's midiOut drain and like
// audio_monitor feeding the spectrum. This keeps the playback nodes pure-runtime (no AVFoundation include)
// and machine-verifiable: a golden cooks the node, reads THIS bus, and asserts the command sequence +
// the ADSR-modulated volume — no audio device, no real file.
//
// TiXL parity: AudioPlayer.cs:151-160 calls AudioEngine.UpdateStereoOperatorPlayback(operatorId, filePath,
// shouldPlay, shouldStop, volume, mute, panning, speed, seek) once per cook; AudioEngine then edge-detects
// play/stop, applies seek-on-play-edge, and drives the stream. sw hoists the edge detection into the node
// cook (per-instance memory here) and expresses the RESULT as a flat command list; the app replays it onto
// the AudioMixer. Faithful in the values issued; the transport (which physical engine) is the app's, one
// AudioMixer for the whole process, not one per node.
//
// runtime leaf: pure computation (std::vector / std::string), no hardware, no UI, no upward dep.
#pragma once
#include <string>
#include <vector>

namespace sw {

// One playback command emitted by a node cook this frame. `kind` selects which mixer call the app makes;
// only the fields that kind uses are meaningful (the rest are ignored). `key` = the node's stable stream
// key (resident path). All value payloads ride the two scalar/one-string slots.
struct AudioPlaybackCommand {
  enum Kind {
    Ensure = 0,   // ensureStream(key, str=path)
    Play,         // play(key)
    Stop,         // stop(key)
    Pause,        // pause(key)
    Resume,       // resume(key)
    Seek,         // seek(key, a=seconds)
    SetGain,      // setGain(key, a=gain, b!=0 ? mute)
    SetPan,       // setPan(key, a=pan)
    SetRate,      // setRate(key, a=rate)
    Remove        // removeStream(key)
  };
  int kind = Ensure;
  std::string key;
  std::string str;   // path (Ensure)
  double a = 0.0;    // primary scalar (seconds / gain / pan / rate)
  double b = 0.0;    // secondary (mute flag for SetGain: !=0 → muted)
};

// The single per-frame playback command sink (Meyers singleton, one per process). The node cooks APPEND
// their commands during the cook; the app READS the accumulated list once per frame and replays it onto the
// AudioMixer; the app CLEARS it after (endAudioPlaybackFrame). The golden drives it single-threaded
// (cook → read → assert), so no clear is needed inside a golden. Not thread-safe by itself (single-threaded
// cook, like io_device_bus).
struct AudioPlaybackBus {
  std::vector<AudioPlaybackCommand> commands;
};

AudioPlaybackBus& audioPlaybackBus();

// Emit helpers (called by the node cooks in audio_playback_cook.cpp; the app drains `commands` each frame).
void emitAudioEnsure(const std::string& key, const std::string& path);
void emitAudioPlay(const std::string& key);
void emitAudioStop(const std::string& key);
void emitAudioPause(const std::string& key);
void emitAudioResume(const std::string& key);
void emitAudioSeek(const std::string& key, double seconds);
void emitAudioGain(const std::string& key, double gain, bool mute);
void emitAudioPan(const std::string& key, double pan);
void emitAudioRate(const std::string& key, double rate);
void emitAudioRemove(const std::string& key);

// Clear the accumulated commands — called once per frame AFTER the app drained them to the AudioMixer.
void endAudioPlaybackFrame();

}  // namespace sw
