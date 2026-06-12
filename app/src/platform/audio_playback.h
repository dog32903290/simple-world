// platform/audio_playback — soundtrack file -> output device. The native seam for the
// composition's backing audio: load a file, play/pause/seek, and report a readable position
// so the app layer can keep it in sync with the Transport (TiXL semantics: the WALL CLOCK is
// master, audio FOLLOWS — see app/soundtrack.h for the follow rule itself).
//
// Implementation: AVAudioEngine + AVAudioPlayerNode (NOT the raw-AUHAL route capture took).
// Capture went AUHAL because overriding AVAudioEngine's INPUT device desynced its tap (zero
// blocks, see audio_capture.mm:77-83). Output here always targets the system default device —
// no device override, so the documented AVAudioEngine failure mode never applies, and the
// player node gives load/seek/position for free instead of an ExtAudioFile ring buffer.
//
// platform leaf: ObjC behind a C++ pimpl — callers include no ObjC, no runtime/app/ui headers.
// All methods are main-thread; load failure is non-fatal (returns false, instance stays empty).
#pragma once
#include <string>

namespace sw {

class AudioPlayback {
 public:
  AudioPlayback();
  ~AudioPlayback();
  AudioPlayback(const AudioPlayback&) = delete;
  AudioPlayback& operator=(const AudioPlayback&) = delete;

  // Open an audio file for playback (wav/aiff/mp3/m4a — whatever AVAudioFile reads).
  // Replaces any previously loaded file (stops playback). Returns false on any failure
  // (missing file, unreadable codec) WITHOUT crashing; the instance is then unloaded.
  bool load(const std::string& path);
  void unload();          // stop + release the file (no-op when nothing loaded)
  bool loaded() const;

  // Transport controls. play() starts the engine lazily whenever it isn't running (checked via
  // engine.isRunning each time — macOS stops the engine on a default-device switch, so "started
  // once" is never cached). If the engine cannot start (no output device) it warns ONCE and
  // stays paused; the failure is cached (no per-frame retry/log storm) until the next device
  // configuration change. A configuration change while playing recovers on the next play()/
  // seek(): the engine restarts and the schedule resumes from the requested position — the
  // app-side drift rule reaches that path within one resync threshold. pause() captures the
  // position so a later play() resumes from it. seek() clamps to [0, duration] and takes effect
  // immediately (also while paused — position() then reads back the seek target, audio stays
  // silent).
  void play();
  void pause();
  void seek(double seconds);
  bool playing() const;

  // Teeth-only (--selftest-soundtrack): pretend macOS posted a configuration-change
  // notification (device switch) so the headless selftest can exercise the restart path
  // without unplugging hardware. Production code never calls this.
  void debugSimulateConfigChange();

  // The audio clock, in seconds into the file. While playing this advances with the actual
  // rendered samples (player time), so the app's follow rule can measure drift against the
  // transport; while paused it holds. Clamped to [0, durationSeconds()].
  double positionSeconds() const;
  double durationSeconds() const;  // 0 when nothing is loaded

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace sw
