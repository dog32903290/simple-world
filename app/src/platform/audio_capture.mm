#include "platform/audio_capture.h"

#include "runtime/attack_detector.h"
#include "runtime/audio_analyzer.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <cstdio>

namespace sw {

struct AudioCapture::Impl {
  AVAudioEngine* engine = nil;
  AudioAnalyzer  analyzer;
  AttackDetector attack;
  std::atomic<float>              envelope{0.0f};
  std::atomic<float>              rms{0.0f};
  std::atomic<unsigned long long> blocks{0};
  std::atomic<bool>               running{false};
  double             sampleRate = 48000.0;
  unsigned long long sampleClock = 0;  // audio-thread only
};

// Build the engine, install the input tap, start it. Runs on whatever thread calls it
// (main, directly or via the grant handler). Returns false on a known failure.
bool AudioCapture::startEngine(AudioCapture::Impl* impl) {
  @autoreleasepool {
    impl->engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = impl->engine.inputNode;
    AVAudioFormat* fmt = [input inputFormatForBus:0];
    if (fmt == nil || fmt.sampleRate <= 0.0 || fmt.channelCount == 0) {
      printf("[audio-capture] no usable input format (no device?)\n");
      impl->engine = nil;
      return false;
    }
    impl->sampleRate = fmt.sampleRate;

    [input installTapOnBus:0
                bufferSize:512
                    format:fmt
                     block:^(AVAudioPCMBuffer* buf, AVAudioTime* /*when*/) {
                       const float* const* ch = buf.floatChannelData;
                       if (ch == nullptr) return;
                       const int n = (int)buf.frameLength;
                       const int nch = (int)buf.format.channelCount;
                       impl->analyzer.processBlock(ch, nch, n, 1.0f);
                       const AudioSnapshot snap = impl->analyzer.snapshot();
                       impl->sampleClock += (unsigned long long)n;
                       const double timeMs =
                           (double)impl->sampleClock / impl->sampleRate * 1000.0;
                       AttackFrameInput in;
                       in.hasRms = true;  in.rms = snap.rms;
                       in.hasPeak = true; in.peak = snap.peak;
                       in.timeMs = timeMs;
                       const AttackFrameOutput out = impl->attack.processFrame(in);
                       impl->envelope.store((float)out.envelope, std::memory_order_relaxed);
                       impl->rms.store(snap.rms, std::memory_order_relaxed);
                       impl->blocks.fetch_add(1, std::memory_order_relaxed);
                     }];

    NSError* err = nil;
    if (![impl->engine startAndReturnError:&err]) {
      printf("[audio-capture] engine start failed: %s\n",
             err ? err.localizedDescription.UTF8String : "unknown");
      impl->engine = nil;
      return false;
    }
    impl->running.store(true, std::memory_order_relaxed);
    printf("[audio-capture] running @ %.0f Hz\n", impl->sampleRate);
    return true;
  }
}

AudioCapture::AudioCapture() : impl_(new Impl) {}
AudioCapture::~AudioCapture() {
  stop();
  delete impl_;
}

bool AudioCapture::start() {
  const AVAuthorizationStatus status =
      [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusAuthorized) return startEngine(impl_);

  if (status == AVAuthorizationStatusNotDetermined) {
    Impl* impl = impl_;
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               if (granted)
                                 dispatch_async(dispatch_get_main_queue(),
                                                ^{ startEngine(impl); });
                               else
                                 printf("[audio-capture] microphone denied\n");
                             }];
    return true;  // engine starts asynchronously once the user grants
  }

  printf("[audio-capture] microphone not authorized (status=%ld); no audio drive\n",
         (long)status);
  return false;
}

void AudioCapture::stop() {
  if (!impl_) return;
  if (impl_->engine != nil) {
    @autoreleasepool {
      [impl_->engine stop];
      impl_->engine = nil;
    }
  }
  impl_->running.store(false, std::memory_order_relaxed);
}

bool  AudioCapture::running() const { return impl_->running.load(std::memory_order_relaxed); }
float AudioCapture::envelope() const { return impl_->envelope.load(std::memory_order_relaxed); }
float AudioCapture::lastRms() const { return impl_->rms.load(std::memory_order_relaxed); }
unsigned long long AudioCapture::blocksProcessed() const {
  return impl_->blocks.load(std::memory_order_relaxed);
}
void AudioCapture::setTestEnvelope(float v) {
  impl_->envelope.store(v, std::memory_order_relaxed);
}

int runAudioCaptureSmoke(double seconds) {
  AudioCapture cap;
  if (!cap.start()) {
    printf("[audio-capture-smoke] start() returned false (denied / no device)\n");
    return 1;
  }
  printf("[audio-capture-smoke] running %.1fs — make noise (kick/clap)...\n", seconds);
  @autoreleasepool {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0.0) {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                               beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
      printf("  blocks=%llu rms=%.4f env=%.4f running=%d\n", cap.blocksProcessed(),
             cap.lastRms(), cap.envelope(), cap.running() ? 1 : 0);
    }
  }
  cap.stop();
  printf("[audio-capture-smoke] done (blocks=%llu)\n", cap.blocksProcessed());
  return 0;
}

}  // namespace sw
