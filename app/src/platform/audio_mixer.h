// platform/audio_mixer — per-operator audio mixer: N independently-keyed streams feeding ONE output
// mixer, each with its own gain/pan/rate/seek + a per-stream level readout. The native seam for the
// AudioPlayer / PlayAudioClip / SpatialAudioPlayer operator family (TiXL io/audio playback nodes).
//
// TiXL AUTHORITY (the machine this clones):
//   Core/Audio/AudioMixerManager.cs — operator streams → operator mixer → global mixer → soundcard.
//   Core/Audio/OperatorAudioStreamBase.cs + StereoOperatorAudioStream.cs — the per-operator stream:
//     CreateStream + MixerAddChannel(paused,buffered) + Play/Pause/Stop/Seek/SetVolume/SetSpeed/SetPanning
//     + GetLevel (BassMix.ChannelGetLevel → max(L,R)/32768, clamped ≤1).
//   AudioEngine.cs — the Guid→stream registry: create-on-first-cook, (re)load on file change, dispose on
//     finalize, stale-auto-pause of un-cooked operators.
//
// AVAudioEngine PARITY MODEL: AVAudioEngine's mainMixerNode IS the mixer graph natively — attaching N
// AVAudioPlayerNodes to it is the exact analog of MixerAddChannel(operatorMixer, stream). Each key owns
// one AVAudioPlayerNode → AVAudioUnitVarispeed (speed=pitch, BASS Frequency-scaling parity — the SAME
// choice audio_playback.mm made) → mainMixer, with a per-node pan set on the player node. This is the
// per-KEY generalization of audio_playback.mm's single-stream player; the resource-per-key machine mirrors
// point_graph's ensureFeedbackPair (a std::map keyed by an opaque string, lazily created, torn down on
// unload). soundtrack (platform/audio_playback) is a SEPARATE single-stream instance — this leaf does not
// touch it and has no shared state with it.
//
// LEVEL (the one value that does NOT map 1:1 to a BASS call): BASS reads a windowed peak from a buffered
// decode mixer channel; here a lightweight render tap on each player node keeps the last block's max-abs
// sample. That equals TiXL's EXPORT-path level formula EXACTLY (OperatorAudioStreamBase.cs:315-320,
// ExportLevel = min(maxAbs, 1)) and approximates the live GetLevel peak/32768 — the closed-form the mixer
// selftest hand-derives against. A stream that is not playing (or paused) reads level 0 (cs:336-337).
//
// platform leaf: ObjC behind a C++ pimpl; callers include no ObjC and no runtime/app/ui headers. All
// methods main-thread except the render-tap max-abs (written on the audio thread, read as a relaxed
// atomic). Load failure is non-fatal (returns false, that key stays absent).
#pragma once
#include <cstddef>
#include <string>

namespace sw {

class AudioMixer {
 public:
  // Speed range = AVAudioUnitVarispeed.rate hardware window (0.25–4.0), the SAME clamp audio_playback uses.
  // TiXL's StereoOperatorAudioStream clamps speed to [0.1, 4.0] (OperatorAudioStreamBase.cs:273); the low
  // end differs (0.25 vs 0.1) — a NAMED platform fork fork-mixer-speed-min (the varispeed AU floor), same
  // fork audio_playback.h documents. Callers below 0.25 get 0.25 (audible, not silent).
  static constexpr double kRateMin = 0.25;
  static constexpr double kRateMax = 4.0;

  AudioMixer();
  ~AudioMixer();
  AudioMixer(const AudioMixer&) = delete;
  AudioMixer& operator=(const AudioMixer&) = delete;

  // Create (or re-point) the stream for `key` to play `path`. Idempotent when the path is unchanged for an
  // existing key (mirrors AudioEngine.HandleFileChange short-circuit, cs:780). On a path change the old
  // stream is torn down and rebuilt (cs:788). Returns false on any load failure (missing file / unreadable
  // codec) WITHOUT crashing; the key is then absent. A freshly created stream starts PAUSED at volume 0 —
  // play() unmutes it (OperatorAudioStreamBase.cs:147/156).
  bool ensureStream(const std::string& key, const std::string& path);
  void removeStream(const std::string& key);   // dispose one key's stream (no-op if absent)
  bool hasStream(const std::string& key) const;
  size_t streamCount() const;

  // Transport controls, per key. All no-op when the key is absent. play() (re)schedules from the current
  // seek base and starts the shared engine lazily (engine.isRunning re-checked each call — macOS stops the
  // engine on a device switch; never cached). stop() halts + rewinds to base 0 (cs:203). pause()/resume()
  // hold/restore (cs:182/192). seek() sets the play base in seconds (clamped [0,duration]); while playing it
  // restarts from there (cs:286).
  void play(const std::string& key);
  void stop(const std::string& key);
  void pause(const std::string& key);
  void resume(const std::string& key);
  void seek(const std::string& key, double seconds);
  bool playing(const std::string& key) const;   // IsPlaying && !IsPaused (AudioEngine.cs:890-897)
  bool paused(const std::string& key) const;

  // Per-stream attributes (live, no reschedule). gain: linear 0..1 volume; mute forces 0 (cs:256-265).
  // pan: -1 (L) .. +1 (R). rate: playback speed = pitch (varispeed), clamped [kRateMin,kRateMax].
  void setGain(const std::string& key, double gain, bool mute);
  void setPan(const std::string& key, double pan);
  void setRate(const std::string& key, double rate);

  // The stream's current output level: last render block's max-abs sample, clamped to [0,1]. 0 when the key
  // is absent, not playing, or paused (OperatorAudioStreamBase.cs:334-345). This is the closed-form the
  // mixer selftest asserts against (== TiXL ExportLevel formula).
  double level(const std::string& key) const;
  double durationSeconds(const std::string& key) const;   // 0 when absent
  double positionSeconds(const std::string& key) const;    // seek base + rendered frames, seconds

  // Teeth-only (selftests): feed a synthetic render block for `key` so the headless level path can be
  // exercised without a live audio device (no engine start, no real file playback). Sets the stream's
  // last-block max-abs to `maxAbs` (clamped ≥0) and marks it playing/paused per the flags, so level()
  // returns the closed-form value. Production never calls this.
  void debugInjectLevelBlock(const std::string& key, double maxAbs, bool isPlaying, bool isPaused);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace sw
