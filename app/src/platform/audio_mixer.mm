#include "platform/audio_mixer.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <cstdio>
#include <map>
#include <memory>

namespace sw {

namespace {
bool g_audioMixerBug = false;   // golden teeth latch (see setAudioMixerBug); production leaves it false.
}  // namespace
void setAudioMixerBug(bool on) { g_audioMixerBug = on; }
bool audioMixerBug() { return g_audioMixerBug; }

// One keyed stream = one AVAudioPlayerNode → varispeed → engine mainMixer, plus per-key seek base + render
// tap. The single-segment position model is copied verbatim from audio_playback.mm (schedule ONE segment
// [baseFrame,end); positionFrames = baseFrame + rendered-since-start; pause folds rendered back into base).
// Level: a bufferBlock tap on the player records the block's max-abs into `lastLevel` (audio thread → relaxed
// atomic); level() reads it iff the stream is playing and not paused (OperatorAudioStreamBase.cs:334-345).
struct Stream {
  AVAudioPlayerNode* player = nil;
  AVAudioUnitVarispeed* varispeed = nil;
  AVAudioFile* file = nil;
  std::string path;
  double sampleRate = 0.0;
  long long lengthFrames = 0;
  long long baseFrame = 0;
  double rate = 1.0;
  double gain = 1.0;
  bool mute = false;
  double pan = 0.0;
  bool playing = false;
  bool paused = false;
  std::shared_ptr<std::atomic<float>> lastLevel = std::make_shared<std::atomic<float>>(0.0f);
};

struct AudioMixer::Impl {
  AVAudioEngine* engine = nil;
  std::map<std::string, Stream> streams;
  bool startWarned = false;
  std::atomic<bool> configChanged{false};
  id configObserver = nil;

  Stream* find(const std::string& key) {
    auto it = streams.find(key);
    return it == streams.end() ? nullptr : &it->second;
  }
  const Stream* find(const std::string& key) const {
    auto it = streams.find(key);
    return it == streams.end() ? nullptr : &it->second;
  }

  long long renderedFrames(const Stream& s) const {
    if (!s.playing || s.player == nil) return 0;
    AVAudioTime* nodeTime = s.player.lastRenderTime;
    if (nodeTime == nil) return 0;
    AVAudioTime* pt = [s.player playerTimeForNodeTime:nodeTime];
    if (pt == nil || !pt.sampleTimeValid) return 0;
    return pt.sampleTime > 0 ? (long long)pt.sampleTime : 0;
  }
  long long currentFrame(const Stream& s) const {
    long long f = s.baseFrame + renderedFrames(s);
    if (f > s.lengthFrames) f = s.lengthFrames;
    if (f < 0) f = 0;
    return f;
  }

  // (Re)pin player → varispeed → mainMixer in the file's processing format (connect replaces any previous
  // edge, so this is both the load wiring and the config-change rebuild). Re-applies the stored rate/pan/gain.
  void connectChain(Stream& s) {
    [engine connect:s.player to:s.varispeed format:s.file.processingFormat];
    [engine connect:s.varispeed to:engine.mainMixerNode format:s.file.processingFormat];
    s.varispeed.rate = (float)s.rate;
    s.player.pan = (float)s.pan;
    s.player.volume = (float)((!s.mute) ? s.gain : 0.0);
  }

  // Install the render tap that keeps this stream's last-block max-abs (the level source). Tap format = the
  // player output format; buffer callback runs on the audio thread → write the relaxed atomic only.
  void installTap(Stream& s) {
    std::shared_ptr<std::atomic<float>> slot = s.lastLevel;
    AVAudioMixerNode* mixer = engine.mainMixerNode;
    [s.player installTapOnBus:0
                   bufferSize:1024
                       format:[s.player outputFormatForBus:0]
                        block:^(AVAudioPCMBuffer* buf, AVAudioTime*) {
      float peak = 0.0f;
      const AVAudioFrameCount n = buf.frameLength;
      const AVAudioChannelCount ch = buf.format.channelCount;
      float* const* data = buf.floatChannelData;
      if (data != nullptr) {
        for (AVAudioChannelCount c = 0; c < ch; ++c) {
          const float* p = data[c];
          for (AVAudioFrameCount i = 0; i < n; ++i) {
            float a = p[i] < 0.0f ? -p[i] : p[i];
            if (a > peak) peak = a;
          }
        }
      }
      slot->store(peak, std::memory_order_relaxed);
    }];
    (void)mixer;
  }

  void scheduleAndPlay(Stream& s) {
    if (s.file == nil) return;
    if (configChanged.exchange(false)) {
      [engine stop];
      for (auto& kv : streams) connectChain(kv.second);
      startWarned = false;
    }
    if (!engine.isRunning) {
      if (startWarned) { s.playing = false; return; }
      NSError* err = nil;
      if (![engine startAndReturnError:&err]) {
        printf("[audio-mixer] engine start failed: %s\n",
               err ? err.localizedDescription.UTF8String : "unknown");
        startWarned = true;
        s.playing = false;
        return;
      }
    }
    [s.player stop];
    const long long remaining = s.lengthFrames - s.baseFrame;
    if (remaining <= 0) { s.playing = false; return; }
    [s.player scheduleSegment:s.file
                startingFrame:(AVAudioFramePosition)s.baseFrame
                   frameCount:(AVAudioFrameCount)remaining
                       atTime:nil
            completionHandler:nil];
    [s.player play];
    s.playing = true;
    s.paused = false;
  }

  void teardown(Stream& s) {
    if (s.player != nil) {
      [s.player stop];
      @try { [s.player removeTapOnBus:0]; } @catch (...) {}
      [engine detachNode:s.player];
    }
    if (s.varispeed != nil) [engine detachNode:s.varispeed];
    s.player = nil;
    s.varispeed = nil;
    s.file = nil;
  }
};

AudioMixer::AudioMixer() : impl_(new Impl) {
  impl_->engine = [[AVAudioEngine alloc] init];
  Impl* impl = impl_;
  impl_->configObserver = [[NSNotificationCenter defaultCenter]
      addObserverForName:AVAudioEngineConfigurationChangeNotification
                  object:impl_->engine
                   queue:nil
              usingBlock:^(NSNotification*) { impl->configChanged.store(true); }];
}

AudioMixer::~AudioMixer() {
  if (impl_->configObserver != nil)
    [[NSNotificationCenter defaultCenter] removeObserver:impl_->configObserver];
  for (auto& kv : impl_->streams) impl_->teardown(kv.second);
  impl_->streams.clear();
  if (impl_->engine.isRunning) [impl_->engine stop];
  impl_->engine = nil;
  impl_->configObserver = nil;
  delete impl_;
}

bool AudioMixer::ensureStream(const std::string& key, const std::string& path) {
  Stream* existing = impl_->find(key);
  if (existing != nullptr && existing->path == path && existing->file != nil) return true;  // cs:780 no-op
  if (existing != nullptr) { impl_->teardown(*existing); impl_->streams.erase(key); }  // cs:788 rebuild

  NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
  NSError* err = nil;
  AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
  if (file == nil || err != nil) {
    printf("[audio-mixer] cannot open '%s': %s\n", path.c_str(),
           err ? err.localizedDescription.UTF8String : "unknown error");
    return false;
  }
  if (file.length <= 0 || file.processingFormat.sampleRate <= 0.0) {
    printf("[audio-mixer] '%s' has no audible content\n", path.c_str());
    return false;
  }
  Stream s;
  s.player = [[AVAudioPlayerNode alloc] init];
  s.varispeed = [[AVAudioUnitVarispeed alloc] init];
  s.file = file;
  s.path = path;
  s.sampleRate = file.processingFormat.sampleRate;
  s.lengthFrames = (long long)file.length;
  [impl_->engine attachNode:s.player];
  [impl_->engine attachNode:s.varispeed];
  impl_->connectChain(s);
  impl_->installTap(s);
  impl_->streams[key] = std::move(s);
  return true;
}

void AudioMixer::removeStream(const std::string& key) {
  Stream* s = impl_->find(key);
  if (s == nullptr) return;
  impl_->teardown(*s);
  impl_->streams.erase(key);
}

bool AudioMixer::hasStream(const std::string& key) const { return impl_->find(key) != nullptr; }
size_t AudioMixer::streamCount() const { return impl_->streams.size(); }

void AudioMixer::play(const std::string& key) {
  Stream* s = impl_->find(key);
  if (s == nullptr || s->file == nil) return;
  impl_->scheduleAndPlay(*s);
}

void AudioMixer::stop(const std::string& key) {
  Stream* s = impl_->find(key);
  if (s == nullptr) return;
  if (s->player != nil) [s->player stop];
  s->baseFrame = 0;       // Stop rewinds to 0 (OperatorAudioStreamBase.cs:203-211)
  s->playing = false;
  s->paused = false;
  s->lastLevel->store(0.0f, std::memory_order_relaxed);
}

void AudioMixer::pause(const std::string& key) {
  Stream* s = impl_->find(key);
  if (s == nullptr || !s->playing) return;
  s->baseFrame = impl_->currentFrame(*s);   // fold rendered frames into base BEFORE stop
  if (s->player != nil) [s->player stop];
  s->playing = false;
  s->paused = true;
  s->lastLevel->store(0.0f, std::memory_order_relaxed);
}

void AudioMixer::resume(const std::string& key) {
  Stream* s = impl_->find(key);
  if (s == nullptr || !s->paused) return;
  impl_->scheduleAndPlay(*s);
}

void AudioMixer::seek(const std::string& key, double seconds) {
  Stream* s = impl_->find(key);
  if (s == nullptr || s->file == nil) return;
  if (seconds < 0.0) seconds = 0.0;
  long long frame = (long long)(seconds * s->sampleRate);
  if (frame > s->lengthFrames) frame = s->lengthFrames;
  s->baseFrame = frame;
  if (s->playing) impl_->scheduleAndPlay(*s);
}

bool AudioMixer::playing(const std::string& key) const {
  const Stream* s = impl_->find(key);
  return s != nullptr && s->playing && !s->paused;
}
bool AudioMixer::paused(const std::string& key) const {
  const Stream* s = impl_->find(key);
  return s != nullptr && s->paused;
}

void AudioMixer::setGain(const std::string& key, double gain, bool mute) {
  Stream* s = impl_->find(key);
  if (s == nullptr) return;
  if (gain < 0.0) gain = 0.0;
  s->gain = gain;
  s->mute = mute;
  if (s->player != nil) s->player.volume = (float)((!mute) ? gain : 0.0);  // cs:256-265
}

void AudioMixer::setPan(const std::string& key, double pan) {
  Stream* s = impl_->find(key);
  if (s == nullptr) return;
  if (pan < -1.0) pan = -1.0;
  if (pan > 1.0) pan = 1.0;
  s->pan = pan;
  if (s->player != nil) s->player.pan = (float)pan;  // StereoOperatorAudioStream.SetPanning, cs:58-61
}

void AudioMixer::setRate(const std::string& key, double rate) {
  Stream* s = impl_->find(key);
  if (s == nullptr) return;
  if (!(rate >= kRateMin)) rate = kRateMin;  // guard NaN → kRateMin
  if (rate > kRateMax) rate = kRateMax;
  s->rate = rate;
  if (s->varispeed != nil) s->varispeed.rate = (float)rate;
}

double AudioMixer::level(const std::string& key) const {
  const Stream* s = impl_->find(key);
  if (s == nullptr) return 0.0;
  if (!g_audioMixerBug && (!s->playing || s->paused)) return 0.0;   // cs:336-337 gate (skipped under teeth)
  double v = (double)s->lastLevel->load(std::memory_order_relaxed);
  if (v < 0.0) v = 0.0;
  if (!g_audioMixerBug && v > 1.0) v = 1.0;   // min(peak, 1) — cs:344 / export cs:320 (skipped under teeth)
  return v;
}

double AudioMixer::durationSeconds(const std::string& key) const {
  const Stream* s = impl_->find(key);
  if (s == nullptr || s->sampleRate <= 0.0) return 0.0;
  return (double)s->lengthFrames / s->sampleRate;
}

double AudioMixer::positionSeconds(const std::string& key) const {
  const Stream* s = impl_->find(key);
  if (s == nullptr || s->sampleRate <= 0.0) return 0.0;
  return (double)impl_->currentFrame(*s) / s->sampleRate;
}

void AudioMixer::debugInjectLevelBlock(const std::string& key, double maxAbs, bool isPlaying, bool isPaused) {
  Stream* s = impl_->find(key);
  if (s == nullptr) {
    // Allow injecting into a key with no real file (headless): create a bare stream record.
    Stream bare;
    bare.lastLevel = std::make_shared<std::atomic<float>>(0.0f);
    impl_->streams[key] = std::move(bare);
    s = impl_->find(key);
  }
  if (maxAbs < 0.0) maxAbs = 0.0;
  s->lastLevel->store((float)maxAbs, std::memory_order_relaxed);
  s->playing = isPlaying;
  s->paused = isPaused;
}

}  // namespace sw
