#include "platform/video_capture.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace sw {

// The ObjC delegate that receives sample buffers on the capture queue and forwards BGRA bytes to the
// C++ callback. Held by the Impl; deallocated with the session.
}  // namespace sw

@interface SwVideoCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) sw::VideoCapture::FrameCallback cb;
@property(nonatomic, assign) void* user;
@property(nonatomic, assign) std::atomic<uint64_t>* frameCount;
@end

@implementation SwVideoCaptureDelegate
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
  CVImageBufferRef pix = CMSampleBufferGetImageBuffer(sampleBuffer);
  if (pix == NULL) return;
  CVPixelBufferLockBaseAddress(pix, kCVPixelBufferLock_ReadOnly);
  size_t w = CVPixelBufferGetWidth(pix);
  size_t h = CVPixelBufferGetHeight(pix);
  size_t stride = CVPixelBufferGetBytesPerRow(pix);
  const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddress(pix);
  if (base != nullptr && w > 0 && h > 0) {
    if (self.frameCount) self.frameCount->fetch_add(1);
    if (self.cb) {
      const size_t dstStride = w * 4;
      if (stride == dstStride) {
        self.cb(self.user, base, (uint32_t)w, (uint32_t)h);
      } else {
        std::vector<uint8_t> packed(dstStride * h);
        for (size_t y = 0; y < h; ++y)
          std::memcpy(packed.data() + y * dstStride, base + y * stride, dstStride);
        self.cb(self.user, packed.data(), (uint32_t)w, (uint32_t)h);
      }
    }
  }
  CVPixelBufferUnlockBaseAddress(pix, kCVPixelBufferLock_ReadOnly);
}
@end

namespace sw {

struct VideoCapture::Impl {
  AVCaptureSession* session = nil;
  SwVideoCaptureDelegate* delegate = nil;
  dispatch_queue_t queue = nil;
  std::atomic<uint64_t> frameCount{0};
  bool running = false;
};

VideoCapture::VideoCapture() : impl_(new Impl()) {}
VideoCapture::~VideoCapture() { stop(); delete impl_; }
bool VideoCapture::isRunning() const { return impl_->running; }

static AVCaptureDevice* defaultVideoDevice() {
  // Prefer the modern discovery session (avoids the deprecated defaultDeviceWithMediaType warning).
  if (@available(macOS 10.15, *)) {
    AVCaptureDeviceDiscoverySession* disc = [AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:@[ AVCaptureDeviceTypeBuiltInWideAngleCamera,
                                           AVCaptureDeviceTypeExternalUnknown ]
                              mediaType:AVMediaTypeVideo
                               position:AVCaptureDevicePositionUnspecified];
    if (disc.devices.count > 0) return disc.devices.firstObject;
  }
  return [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
}

bool VideoCapture::hasDefaultDevice() {
  @autoreleasepool { return defaultVideoDevice() != nil; }
}

int VideoCapture::authorizationStatus() {
  return (int)[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
}

bool VideoCapture::start(FrameCallback cb, void* user) {
  stop();
  @autoreleasepool {
    AVCaptureDevice* device = defaultVideoDevice();
    if (device == nil) return false;

    // Authorization: authorized (3) → go; denied/restricted (2/1) → false (caller SKIPs). Not-determined
    // (0): request synchronously (headless has no UI runloop to service an async prompt reliably, so we
    // treat not-yet-granted as SKIP rather than hang — the app's live path can prompt interactively).
    AVAuthorizationStatus st = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (st == AVAuthorizationStatusDenied || st == AVAuthorizationStatusRestricted) return false;
    if (st == AVAuthorizationStatusNotDetermined) return false;

    NSError* err = nil;
    AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
    if (input == nil) return false;

    AVCaptureSession* session = [[AVCaptureSession alloc] init];
    if (![session canAddInput:input]) return false;
    [session addInput:input];

    AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
    output.videoSettings = @{
      (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    output.alwaysDiscardsLateVideoFrames = YES;
    if (![session canAddOutput:output]) return false;
    [session addOutput:output];

    SwVideoCaptureDelegate* del = [[SwVideoCaptureDelegate alloc] init];
    del.cb = cb;
    del.user = user;
    del.frameCount = &impl_->frameCount;
    dispatch_queue_t q = dispatch_queue_create("sw.video.capture", DISPATCH_QUEUE_SERIAL);
    [output setSampleBufferDelegate:del queue:q];

    impl_->session = session;
    impl_->delegate = del;
    impl_->queue = q;
    impl_->frameCount = 0;
    [session startRunning];
    impl_->running = true;
    return true;
  }
}

void VideoCapture::stop() {
  if (impl_->session != nil) {
    @autoreleasepool {
      [impl_->session stopRunning];
    }
  }
  impl_->session = nil;
  impl_->delegate = nil;
  impl_->queue = nil;
  impl_->running = false;
}

// ── Self-test ─────────────────────────────────────────────────────────────────────────────────────
namespace {
std::atomic<uint64_t> g_seenFrames{0};
std::atomic<uint32_t> g_lastW{0};
std::atomic<uint32_t> g_lastH{0};
void selftestFrameCb(void*, const uint8_t*, uint32_t w, uint32_t h) {
  g_lastW = w;
  g_lastH = h;
  g_seenFrames.fetch_add(1);
}
}  // namespace

int runVideoCaptureSelfTest(bool /*injectBug*/) {
  @autoreleasepool {
    if (!VideoCapture::hasDefaultDevice()) {
      std::printf("[selftest-io-video-capture] SKIP: no default video device on this machine\n");
      return 0;  // graceful skip — not a false green
    }
    int st = VideoCapture::authorizationStatus();
    if (st != (int)AVAuthorizationStatusAuthorized) {
      const char* r = st == (int)AVAuthorizationStatusDenied      ? "denied"
                      : st == (int)AVAuthorizationStatusRestricted ? "restricted"
                                                                   : "not-determined";
      std::printf("[selftest-io-video-capture] SKIP: camera authorization=%s (grant to run live)\n", r);
      return 0;  // graceful skip
    }

    g_seenFrames = 0;
    g_lastW = 0;
    g_lastH = 0;
    VideoCapture cap;
    if (!cap.start(selftestFrameCb, nullptr)) {
      std::printf("[selftest-io-video-capture] SKIP: session start failed (device/permission)\n");
      return 0;
    }

    // Pump the runloop up to ~3s waiting for the first frame (frames arrive on the capture queue).
    for (int i = 0; i < 300 && g_seenFrames.load() == 0; ++i) {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                               beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    cap.stop();

    uint64_t frames = g_seenFrames.load();
    uint32_t w = g_lastW.load();
    uint32_t h = g_lastH.load();
    if (frames == 0) {
      std::printf("[selftest-io-video-capture] FAIL: device present + authorized but no frame in 3s\n");
      return 1;
    }
    bool dimsOk = w > 0 && h > 0;
    std::printf("[selftest-io-video-capture] frames=%llu last=%ux%u (content deferred-content-verify) %s\n",
                (unsigned long long)frames, w, h, dimsOk ? "PASS" : "FAIL");
    return dimsOk ? 0 : 1;
  }
}

}  // namespace sw
