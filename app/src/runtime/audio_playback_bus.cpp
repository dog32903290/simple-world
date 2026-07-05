// runtime/audio_playback_bus — the per-frame audio-playback command sink implementation (Meyers
// singleton). See audio_playback_bus.h for the leaf-seam rationale (node cook writes, app drains).
#include "runtime/audio_playback_bus.h"

namespace sw {

AudioPlaybackBus& audioPlaybackBus() {
  static AudioPlaybackBus bus;
  return bus;
}

void emitAudioEnsure(const std::string& key, const std::string& path) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Ensure, key, path, 0.0, 0.0});
}
void emitAudioPlay(const std::string& key) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Play, key, "", 0.0, 0.0});
}
void emitAudioStop(const std::string& key) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Stop, key, "", 0.0, 0.0});
}
void emitAudioPause(const std::string& key) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Pause, key, "", 0.0, 0.0});
}
void emitAudioResume(const std::string& key) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Resume, key, "", 0.0, 0.0});
}
void emitAudioSeek(const std::string& key, double seconds) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Seek, key, "", seconds, 0.0});
}
void emitAudioGain(const std::string& key, double gain, bool mute) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::SetGain, key, "", gain, mute ? 1.0 : 0.0});
}
void emitAudioPan(const std::string& key, double pan) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::SetPan, key, "", pan, 0.0});
}
void emitAudioRate(const std::string& key, double rate) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::SetRate, key, "", rate, 0.0});
}
void emitAudioRemove(const std::string& key) {
  audioPlaybackBus().commands.push_back({AudioPlaybackCommand::Remove, key, "", 0.0, 0.0});
}

void endAudioPlaybackFrame() { audioPlaybackBus().commands.clear(); }

}  // namespace sw
