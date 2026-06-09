#include "platform/audio_capture.h"

#include "platform/audio_devices.h"  // listInputDevices() for the smoke device resolve
#include "runtime/attack_detector.h"
#include "runtime/audio_analyzer.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>  // AudioUnitSetProperty, kAudioOutputUnitProperty_CurrentDevice
#include <CoreAudio/CoreAudio.h>        // AudioObjectID

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sw {

struct AudioCapture::Impl {
  AudioUnit      au = nullptr;          // AUHAL input unit (we do NOT use AVAudioEngine — see startEngine)
  AudioAnalyzer  analyzer;
  AttackDetector attack;
  std::atomic<float>              envelope{0.0f};
  std::atomic<float>              rms{0.0f};
  std::atomic<unsigned long long> blocks{0};
  std::atomic<bool>               running{false};
  unsigned int       currentDeviceId = 0;  // CoreAudio id in use (0 = system default)
  double             sampleRate = 48000.0;
  unsigned long long sampleClock = 0;  // audio-thread only
  // Capture scratch (audio thread): non-interleaved float32, one buffer per channel sized
  // to maxFrames; abl points its mBuffers at chBuf[i] each render. Allocated in startEngine.
  int                channels = 0;
  UInt32             maxFrames = 4096;
  std::vector<std::vector<float>> chBuf;
  std::vector<float*>             chPtr;       // stable pointers into chBuf for processBlock
  std::vector<uint8_t>            ablStorage;  // backs the variable-length AudioBufferList
  AudioBufferList*                abl = nullptr;
  SpectrumAnalyzer                spectrum;     // per-band FFT analysis (TiXL parity), fed below
  std::vector<float>              monoScratch;  // audio thread: channel mono-mix for the FFT
  // AUHAL input callback — a static member so it can touch these (private) Impl fields while
  // matching the C AURenderCallback signature; the header stays Core Audio-free for main.
  static OSStatus inputProc(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags,
                            const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber,
                            UInt32 inNumberFrames, AudioBufferList* ioData);
};

// AUHAL input callback (audio thread). Pulls the just-captured block via AudioUnitRender
// into impl's per-channel buffers, then drives the pure-compute analyzer + attack detector
// and publishes the results via atomics — same contract the old AVAudioEngine tap had.
OSStatus AudioCapture::Impl::inputProc(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags,
                                       const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber,
                                       UInt32 inNumberFrames, AudioBufferList* /*ioData (null for input)*/) {
  auto* impl = static_cast<AudioCapture::Impl*>(inRefCon);
  if (impl->au == nullptr || impl->abl == nullptr || impl->channels <= 0) return noErr;
  UInt32 frames = inNumberFrames > impl->maxFrames ? impl->maxFrames : inNumberFrames;
  for (int c = 0; c < impl->channels; ++c) {
    impl->abl->mBuffers[c].mNumberChannels = 1;
    impl->abl->mBuffers[c].mDataByteSize   = frames * (UInt32)sizeof(float);
    impl->abl->mBuffers[c].mData           = impl->chBuf[c].data();
  }
  const OSStatus r =
      AudioUnitRender(impl->au, ioActionFlags, inTimeStamp, inBusNumber, frames, impl->abl);
  if (r != noErr) return r;
  impl->analyzer.processBlock(impl->chPtr.data(), impl->channels, (int)frames, 1.0f);
  // Feed the per-band spectrum analyzer (TiXL AudioReaction parity) with a mono mix.
  if ((int)impl->monoScratch.size() >= (int)frames && impl->channels > 0) {
    const float inv = 1.0f / (float)impl->channels;
    for (UInt32 i = 0; i < frames; ++i) {
      float acc = 0.0f;
      for (int c = 0; c < impl->channels; ++c) acc += impl->chBuf[c][i];
      impl->monoScratch[i] = acc * inv;
    }
    impl->spectrum.processBlock(impl->monoScratch.data(), (int)frames, (float)impl->sampleRate);
  }
  const AudioSnapshot snap = impl->analyzer.snapshot();
  impl->sampleClock += frames;
  const double timeMs = (double)impl->sampleClock / impl->sampleRate * 1000.0;
  AttackFrameInput in;
  in.hasRms = true;  in.rms = snap.rms;
  in.hasPeak = true; in.peak = snap.peak;
  in.timeMs = timeMs;
  const AttackFrameOutput out = impl->attack.processFrame(in);
  impl->envelope.store((float)out.envelope, std::memory_order_relaxed);
  impl->rms.store(snap.rms, std::memory_order_relaxed);
  impl->blocks.fetch_add(1, std::memory_order_relaxed);
  return noErr;
}

// Build a raw AUHAL (HALOutput) input unit bound to the chosen device, install the input
// callback, start it. Runs on whatever thread calls it (main, directly or via the grant
// handler). Returns false on a known failure.
//
// Why not AVAudioEngine: overriding AVAudioEngine.inputNode's device via the raw AudioUnit
// property sets and reads back correctly, yet desyncs the engine so its tap receives ZERO
// buffers — proven by DIAG (readback==wanted, format valid, engine running, blocks=0) for
// ANY explicit device, even the built-in mic by its own id; only the untouched default
// (deviceId==0) path delivered. AVAudioInputNode also doesn't expose auAudioUnit, so the v3
// deviceID setter is unreachable. The AUHAL is the canonical "capture from a chosen device"
// path and works for every device, with the same atomics contract behind AudioCapture.
bool AudioCapture::startEngine(AudioCapture::Impl* impl, unsigned int deviceId) {
  AudioComponentDescription desc = {};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_HALOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
  if (comp == nullptr) { printf("[audio-capture] no HALOutput component\n"); return false; }
  OSStatus st = AudioComponentInstanceNew(comp, &impl->au);
  if (st != noErr || impl->au == nullptr) {
    printf("[audio-capture] AU instance failed: %d\n", (int)st);
    impl->au = nullptr;
    return false;
  }

  // AUHAL: enable input (element 1 = mic side), disable output (element 0 = speaker side).
  UInt32 one = 1, zero = 0;
  AudioUnitSetProperty(impl->au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one, sizeof(one));
  AudioUnitSetProperty(impl->au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero, sizeof(zero));

  // Bind the device (0 -> resolve the current system default input first).
  AudioObjectID dev = (AudioObjectID)deviceId;
  if (dev == 0) {
    UInt32 sz = sizeof(dev);
    AudioObjectPropertyAddress addr = { kAudioHardwarePropertyDefaultInputDevice,
                                        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &sz, &dev);
  }
  st = AudioUnitSetProperty(impl->au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof(dev));
  if (st != noErr) printf("[audio-capture] set device %u failed: %d\n", deviceId, (int)st);
  impl->currentDeviceId = deviceId;

  // Read the device's hardware input format (scope Input, element 1) for rate + channel count.
  AudioStreamBasicDescription devFmt = {}; UInt32 fsz = sizeof(devFmt);
  st = AudioUnitGetProperty(impl->au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &devFmt, &fsz);
  if (st != noErr || devFmt.mSampleRate <= 0.0 || devFmt.mChannelsPerFrame == 0) {
    printf("[audio-capture] no usable input format (no device?) st=%d\n", (int)st);
    AudioComponentInstanceDispose(impl->au); impl->au = nullptr; return false;
  }
  impl->sampleRate = devFmt.mSampleRate;
  int ch = (int)devFmt.mChannelsPerFrame; if (ch > 16) ch = 16;  // cap analysis channels
  impl->channels = ch;

  // Ask the AUHAL to deliver non-interleaved float32 at the same rate (scope Output of the
  // input element = the client side). Matches analyzer.processBlock's channel-pointer input.
  AudioStreamBasicDescription cli = {};
  cli.mSampleRate = devFmt.mSampleRate;
  cli.mFormatID = kAudioFormatLinearPCM;
  cli.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsPacked;
  cli.mFramesPerPacket = 1;
  cli.mChannelsPerFrame = (UInt32)ch;
  cli.mBitsPerChannel = 32;
  cli.mBytesPerFrame = (UInt32)sizeof(float);   // non-interleaved -> per-channel stride
  cli.mBytesPerPacket = (UInt32)sizeof(float);
  st = AudioUnitSetProperty(impl->au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &cli, sizeof(cli));
  if (st != noErr) {
    printf("[audio-capture] set client format failed: %d\n", (int)st);
    AudioComponentInstanceDispose(impl->au); impl->au = nullptr; return false;
  }

  // Size capture buffers to the unit's max block, then build a persistent per-channel
  // AudioBufferList the callback fills each render.
  UInt32 maxF = 4096; UInt32 msz = sizeof(maxF);
  AudioUnitGetProperty(impl->au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, &msz);
  if (maxF == 0) maxF = 4096;
  impl->maxFrames = maxF;
  impl->chBuf.assign(ch, std::vector<float>(maxF, 0.0f));
  impl->monoScratch.assign(maxF, 0.0f);
  impl->chPtr.resize(ch);
  for (int c = 0; c < ch; ++c) impl->chPtr[c] = impl->chBuf[c].data();
  impl->ablStorage.assign(offsetof(AudioBufferList, mBuffers) + (size_t)ch * sizeof(AudioBuffer), 0);
  impl->abl = reinterpret_cast<AudioBufferList*>(impl->ablStorage.data());
  impl->abl->mNumberBuffers = (UInt32)ch;

  // AUHAL delivers captured input via the *input* callback (not an output render callback).
  AURenderCallbackStruct cb = {}; cb.inputProc = &AudioCapture::Impl::inputProc; cb.inputProcRefCon = impl;
  st = AudioUnitSetProperty(impl->au, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof(cb));
  if (st != noErr) {
    printf("[audio-capture] set input callback failed: %d\n", (int)st);
    AudioComponentInstanceDispose(impl->au); impl->au = nullptr; return false;
  }

  impl->sampleClock = 0;
  st = AudioUnitInitialize(impl->au);
  if (st != noErr) {
    printf("[audio-capture] AU init failed: %d\n", (int)st);
    AudioComponentInstanceDispose(impl->au); impl->au = nullptr; return false;
  }
  st = AudioOutputUnitStart(impl->au);
  if (st != noErr) {
    printf("[audio-capture] AU start failed: %d\n", (int)st);
    AudioUnitUninitialize(impl->au); AudioComponentInstanceDispose(impl->au); impl->au = nullptr; return false;
  }
  impl->running.store(true, std::memory_order_relaxed);
  printf("[audio-capture] running @ %.0f Hz (%d ch, AUHAL dev=%u)\n", impl->sampleRate, ch, (unsigned)dev);
  return true;
}

AudioCapture::AudioCapture() : impl_(new Impl) {}
AudioCapture::~AudioCapture() {
  stop();
  delete impl_;
}

bool AudioCapture::start(unsigned int coreAudioDeviceId) {
  if (impl_->au != nullptr) stop();  // switching device: tear down the current unit first
  const AVAuthorizationStatus status =
      [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusAuthorized) return startEngine(impl_, coreAudioDeviceId);

  if (status == AVAuthorizationStatusNotDetermined) {
    Impl* impl = impl_;
    const unsigned int dev = coreAudioDeviceId;
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               if (granted)
                                 dispatch_async(dispatch_get_main_queue(),
                                                ^{ startEngine(impl, dev); });
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
  if (impl_->au != nullptr) {
    // AudioOutputUnitStop is synchronous — the input callback will not fire after it
    // returns, so it's safe to tear down and drop the buffer the callback reads.
    AudioOutputUnitStop(impl_->au);
    AudioUnitUninitialize(impl_->au);
    AudioComponentInstanceDispose(impl_->au);
    impl_->au = nullptr;
  }
  impl_->abl = nullptr;
  impl_->running.store(false, std::memory_order_relaxed);
}

bool  AudioCapture::running() const { return impl_->running.load(std::memory_order_relaxed); }
unsigned int AudioCapture::currentDeviceId() const { return impl_->currentDeviceId; }
float AudioCapture::envelope() const { return impl_->envelope.load(std::memory_order_relaxed); }
float AudioCapture::lastRms() const { return impl_->rms.load(std::memory_order_relaxed); }
SpectrumSnapshot AudioCapture::spectrumSnapshot() const { return impl_->spectrum.snapshot(); }
unsigned long long AudioCapture::blocksProcessed() const {
  return impl_->blocks.load(std::memory_order_relaxed);
}

int runAudioPermissionStatus() {
  const AVAuthorizationStatus s =
      [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  const char* name = s == AVAuthorizationStatusNotDetermined ? "NotDetermined (never asked / was reset)"
                   : s == AVAuthorizationStatusRestricted    ? "Restricted (policy-blocked)"
                   : s == AVAuthorizationStatusDenied         ? "Denied (recorded a NO)"
                   : s == AVAuthorizationStatusAuthorized     ? "Authorized (granted)"
                                                              : "unknown";
  printf("[audio-permission] microphone = %s [%ld]\n", name, (long)s);
  return 0;
}

int runAudioCaptureSmoke(double seconds, const std::string& deviceMatch) {
  AudioCapture cap;
  unsigned int devId = 0;
  std::string  devName = "system default";
  if (!deviceMatch.empty()) {
    for (const AudioDevice& d : listInputDevices())
      if (d.name.find(deviceMatch) != std::string::npos) { devId = d.coreAudioId; devName = d.name; break; }
    if (devId == 0) {
      printf("[audio-capture-smoke] no input device matching \"%s\"\n", deviceMatch.c_str());
      return 1;
    }
  }
  printf("[audio-capture-smoke] device: %s (id=%u)\n", devName.c_str(), devId);
  if (!cap.start(devId)) {
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
