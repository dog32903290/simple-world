// app/audio_playback_drain — see the header. Owns the single process-wide AudioMixer, cooks the playback
// nodes, and replays the runtime AudioPlaybackBus onto the mixer each frame.
#include "app/audio_playback_drain.h"

#include "platform/audio_mixer.h"          // AudioMixer (the real per-operator engine)
#include "runtime/audio_playback_bus.h"    // AudioPlaybackBus / audioPlaybackBus / endAudioPlaybackFrame
#include "runtime/audio_playback_cook.h"   // cookAudioPlaybackNodes (AudioPlayer/PlayAudioClip node cook)

namespace sw {

void cookAndDrainAudioPlayback(ResidentEvalGraph& g, float fxSecs, double runTimeSecs) {
  cookAudioPlaybackNodes(g, fxSecs, runTimeSecs);   // pure runtime: emit stream commands onto the bus

  AudioPlaybackBus& bus = audioPlaybackBus();
  if (!bus.commands.empty()) {
    static AudioMixer s_mixer;   // one mixer for the whole process (= TiXL single operator-mixer)
    for (const AudioPlaybackCommand& c : bus.commands) {
      switch (c.kind) {
        case AudioPlaybackCommand::Ensure:  s_mixer.ensureStream(c.key, c.str); break;
        case AudioPlaybackCommand::Play:    s_mixer.play(c.key); break;
        case AudioPlaybackCommand::Stop:    s_mixer.stop(c.key); break;
        case AudioPlaybackCommand::Pause:   s_mixer.pause(c.key); break;
        case AudioPlaybackCommand::Resume:  s_mixer.resume(c.key); break;
        case AudioPlaybackCommand::Seek:    s_mixer.seek(c.key, c.a); break;
        case AudioPlaybackCommand::SetGain: s_mixer.setGain(c.key, c.a, c.b != 0.0); break;
        case AudioPlaybackCommand::SetPan:  s_mixer.setPan(c.key, c.a); break;
        case AudioPlaybackCommand::SetRate: s_mixer.setRate(c.key, c.a); break;
        case AudioPlaybackCommand::Remove:  s_mixer.removeStream(c.key); break;
        default: break;
      }
    }
  }
  endAudioPlaybackFrame();   // clear the bus for next frame (mirror of endIoDeviceFrame)
}

}  // namespace sw
