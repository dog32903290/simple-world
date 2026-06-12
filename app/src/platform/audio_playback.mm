#include "platform/audio_playback.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <cstdio>

namespace sw {

// One playback = one AVAudioPlayerNode feeding the engine's main mixer. The position model is
// deliberately single-path: every (re)start schedules ONE segment from baseFrame_ to the end of
// the file, and positionSeconds() = baseFrame_ + frames rendered since that start (the player's
// own sample clock). pause() folds the rendered frames back into baseFrame_ and clears the
// schedule, so play() after pause/seek is always the same code: schedule-from-baseFrame_ + play.
// Sample-exact resume is irrelevant here — the app-side follow rule resyncs anything past 40ms
// (TiXL AudioResyncThreshold) anyway.
struct AudioPlayback::Impl {
  AVAudioEngine* engine = nil;
  AVAudioPlayerNode* player = nil;
  AVAudioFile* file = nil;
  double sampleRate = 0.0;
  long long lengthFrames = 0;
  long long baseFrame = 0;   // file frame the current/next schedule starts at
  bool playing = false;

  // Engine liveness is read from engine.isRunning at every start decision — NEVER cached in a
  // one-shot flag. macOS stops the engine behind our back (default output device switch /
  // headphones unplugged); a cached "started" bit froze positionSeconds() at baseFrame and the
  // app churned silent resyncs until restart (refuter-C 修1).
  //
  // startWarned = engine-start fail-cache: when start fails (no output device) we warn ONCE and
  // stop retrying — a 60Hz [engine start] + printf storm helps nobody. The cache clears on the
  // next configuration change (a device appeared/changed = the state actually moved, retry then).
  bool startWarned = false;

  // Set by the AVAudioEngineConfigurationChangeNotification observer — which fires on an
  // ARBITRARY thread, so it ONLY flips this atomic. The heavy recovery (engine stop, reconnect,
  // restart, reschedule) runs on the main thread inside scheduleAndPlay, reached next frame by
  // the app's follow rule (position froze -> drift > threshold -> Resync -> seek -> here).
  std::atomic<bool> configChanged{false};
  id configObserver = nil;

  // Frames the player has rendered since the last schedule+play (0 when not playing or no
  // render has happened yet — e.g. the first frames right after play()).
  long long renderedFrames() const {
    if (!playing || player == nil) return 0;
    AVAudioTime* nodeTime = player.lastRenderTime;
    if (nodeTime == nil) return 0;
    AVAudioTime* pt = [player playerTimeForNodeTime:nodeTime];
    if (pt == nil || !pt.sampleTimeValid) return 0;
    return pt.sampleTime > 0 ? (long long)pt.sampleTime : 0;
  }

  long long currentFrame() const {
    long long f = baseFrame + renderedFrames();
    if (f > lengthFrames) f = lengthFrames;
    if (f < 0) f = 0;
    return f;
  }

  // Schedule one segment [baseFrame, end) and start the player. The whole start path lives
  // here so play() and a mid-play seek() behave identically.
  void scheduleAndPlay() {
    if (file == nil) return;
    // Device/config change since the last start: the engine already stopped itself. Take it to a
    // clean stop, re-pin the player->mixer edge (the mixer's output format may have changed with
    // the device), and clear the fail-cache — a config change IS the state change that earns a
    // fresh start attempt (and a fresh log line if it fails again).
    if (configChanged.exchange(false)) {
      [engine stop];
      [engine connect:player to:engine.mainMixerNode format:file.processingFormat];
      startWarned = false;
    }
    if (!engine.isRunning) {
      if (startWarned) { playing = false; return; }  // cached failure: wait for a config change
      NSError* err = nil;
      if (![engine startAndReturnError:&err]) {
        printf("[audio-playback] engine start failed: %s\n",
               err ? err.localizedDescription.UTF8String : "unknown");
        startWarned = true;  // warn on the TRANSITION only, no 60Hz retry/printf storm
        playing = false;     // we are audibly paused; don't claim otherwise
        return;              // positionSeconds() keeps reporting baseFrame
      }
    }
    [player stop];  // clears any previous schedule AND resets the player's sample clock to 0
    const long long remaining = lengthFrames - baseFrame;
    if (remaining <= 0) {
      // At/after the end: nothing to schedule. [player stop] above already executed, so the
      // playing flag MUST drop with it — leaving it set wedged playing()==true forever and the
      // app-side follow rule never re-entered (refuter-C 修3).
      playing = false;
      return;
    }
    [player scheduleSegment:file
              startingFrame:(AVAudioFramePosition)baseFrame
                 frameCount:(AVAudioFrameCount)remaining
                     atTime:nil
          completionHandler:nil];
    [player play];
    playing = true;
  }

  void stopPlayer() {
    if (player != nil) [player stop];
    playing = false;
  }
};

AudioPlayback::AudioPlayback() : impl_(new Impl) {
  impl_->engine = [[AVAudioEngine alloc] init];
  impl_->player = [[AVAudioPlayerNode alloc] init];
  [impl_->engine attachNode:impl_->player];
  // Output device switched / headphones unplugged: macOS stops the engine and posts this. The
  // block runs on an arbitrary thread — flip the atomic ONLY; recovery happens on the main
  // thread in scheduleAndPlay (see Impl::configChanged).
  Impl* impl = impl_;  // raw capture; the observer is removed before impl_ is deleted
  impl_->configObserver = [[NSNotificationCenter defaultCenter]
      addObserverForName:AVAudioEngineConfigurationChangeNotification
                  object:impl_->engine
                   queue:nil
              usingBlock:^(NSNotification*) { impl->configChanged.store(true); }];
}

AudioPlayback::~AudioPlayback() {
  if (impl_->configObserver != nil)
    [[NSNotificationCenter defaultCenter] removeObserver:impl_->configObserver];
  unload();
  if (impl_->engine.isRunning) [impl_->engine stop];
  [impl_->engine detachNode:impl_->player];
  impl_->engine = nil;  // ARC releases
  impl_->player = nil;
  impl_->configObserver = nil;
  delete impl_;
}

bool AudioPlayback::load(const std::string& path) {
  unload();
  NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
  NSError* err = nil;
  AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
  if (file == nil || err != nil) {
    printf("[audio-playback] cannot open '%s': %s\n", path.c_str(),
           err ? err.localizedDescription.UTF8String : "unknown error");
    return false;
  }
  if (file.length <= 0 || file.processingFormat.sampleRate <= 0.0) {
    printf("[audio-playback] '%s' has no audible content\n", path.c_str());
    return false;
  }
  // (Re)connect the player in the file's processing format — connect replaces any previous
  // connection, so switching files with a different sample rate just rewires the mixer edge.
  [impl_->engine connect:impl_->player
                      to:impl_->engine.mainMixerNode
                  format:file.processingFormat];
  impl_->file = file;
  impl_->sampleRate = file.processingFormat.sampleRate;
  impl_->lengthFrames = (long long)file.length;
  impl_->baseFrame = 0;
  impl_->playing = false;
  return true;
}

void AudioPlayback::unload() {
  impl_->stopPlayer();
  impl_->file = nil;  // ARC releases
  impl_->sampleRate = 0.0;
  impl_->lengthFrames = 0;
  impl_->baseFrame = 0;
}

bool AudioPlayback::loaded() const { return impl_->file != nil; }

void AudioPlayback::play() {
  if (impl_->file == nil || impl_->playing) return;
  impl_->scheduleAndPlay();
}

void AudioPlayback::pause() {
  if (!impl_->playing) return;
  impl_->baseFrame = impl_->currentFrame();  // fold rendered frames into the base BEFORE stop
  impl_->stopPlayer();
}

void AudioPlayback::seek(double seconds) {
  if (impl_->file == nil) return;
  if (seconds < 0.0) seconds = 0.0;
  long long frame = (long long)(seconds * impl_->sampleRate);
  if (frame > impl_->lengthFrames) frame = impl_->lengthFrames;
  impl_->baseFrame = frame;
  if (impl_->playing) impl_->scheduleAndPlay();  // restart from the new base
  // paused: just hold the new base — play() will schedule from it
}

bool AudioPlayback::playing() const { return impl_->playing; }

void AudioPlayback::debugSimulateConfigChange() { impl_->configChanged.store(true); }

double AudioPlayback::positionSeconds() const {
  if (impl_->file == nil || impl_->sampleRate <= 0.0) return 0.0;
  return (double)impl_->currentFrame() / impl_->sampleRate;
}

double AudioPlayback::durationSeconds() const {
  if (impl_->file == nil || impl_->sampleRate <= 0.0) return 0.0;
  return (double)impl_->lengthFrames / impl_->sampleRate;
}

}  // namespace sw
