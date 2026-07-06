#include "platform/video_capture.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace sw {

// Sticky teeth switch for the closed-form pack core (see header).
static int g_videoCapturePackBug = 0;
void setVideoCapturePackBug(int mode) { g_videoCapturePackBug = mode; }
int  videoCapturePackBug() { return g_videoCapturePackBug; }

// Tightly-pack a (possibly stride-padded) BGRA source into dst (rowPitch = width*4). The exact repack
// the capture delegate runs. TEETH: bug 1 drops the per-row src step so every row reads row 0 (padded
// frames smear) — a real corruption of the stride math, biteable on a synthetic padded frame.
uint32_t packBgraFrame(uint8_t* dst, const uint8_t* src, uint32_t width, uint32_t height,
                       uint32_t srcStride) {
  if (dst == nullptr || src == nullptr || width == 0 || height == 0) return 0;
  const uint32_t dstStride = width * 4;
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* srcRow = (g_videoCapturePackBug == 1) ? src : (src + (size_t)y * srcStride);
    std::memcpy(dst + (size_t)y * dstStride, srcRow, dstStride);
  }
  return dstStride * height;
}

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
        sw::packBgraFrame(packed.data(), base, (uint32_t)w, (uint32_t)h, (uint32_t)stride);
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

// The closed-form pack tooth: a synthetic 4x2 BGRA frame with a PADDED source stride (row width*4 + 8
// bytes of junk padding) must repack to a tight width*4 buffer with the padding stripped and every
// pixel exact. injectBug (pack bug 1) reads row 0 for every row → the distinct per-row colors smear →
// the row-1 pixels break → RED. Returns (ok, anyBite): ok = all asserts green; anyBite = injectBug made
// at least one assert diverge (for the dead-tooth contract).
static void runPackTooth(bool injectBug, bool& ok, bool& anyBite) {
  const uint32_t w = 4, h = 2;
  const uint32_t srcStride = w * 4 + 8;  // 8 bytes of trailing padding per row (alignment sim)
  std::vector<uint8_t> src(srcStride * h, 0);
  // Row 0 pixels: B,G,R,A ramp per pixel; row 1: a DISTINCT set so a smear (bug) is visible.
  for (uint32_t x = 0; x < w; ++x) {
    uint8_t* r0 = src.data() + 0 * srcStride + x * 4;
    r0[0] = (uint8_t)(10 + x); r0[1] = (uint8_t)(40 + x); r0[2] = (uint8_t)(70 + x); r0[3] = 255;
    uint8_t* r1 = src.data() + 1 * srcStride + x * 4;
    r1[0] = (uint8_t)(200 + x); r1[1] = (uint8_t)(150 - x); r1[2] = (uint8_t)(100 + x); r1[3] = 254;
  }
  // Fill the padding bytes with a sentinel (0xEE) that must NOT appear in the packed output.
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t b = w * 4; b < srcStride; ++b) src[y * srcStride + b] = 0xEE;

  setVideoCapturePackBug(injectBug ? 1 : 0);
  std::vector<uint8_t> dst(w * h * 4, 0);
  uint32_t n = packBgraFrame(dst.data(), src.data(), w, h, srcStride);
  setVideoCapturePackBug(0);

  bool sizeOk = (n == w * h * 4);
  // Assert row 1 pixel 0 == its authored color (the divergence point: a smear puts row-0's color here).
  bool row1Ok = dst[(1 * w + 0) * 4 + 0] == 200 && dst[(1 * w + 0) * 4 + 1] == 150 &&
                dst[(1 * w + 0) * 4 + 2] == 100 && dst[(1 * w + 0) * 4 + 3] == 254;
  // Assert no sentinel padding leaked into the packed buffer.
  bool noPad = true;
  for (uint8_t v : dst) if (v == 0xEE) { noPad = false; break; }
  // Row 0 pixel 3 exact (guards the intra-row copy).
  bool row0Ok = dst[(0 * w + 3) * 4 + 0] == 13 && dst[(0 * w + 3) * 4 + 2] == 73;

  bool pass = sizeOk && row1Ok && noPad && row0Ok;
  ok = ok && pass;
  // The biteable assertions are row1Ok/noPad (bug 1 corrupts them); size/row0 are bug-independent.
  if (injectBug && (!row1Ok || !noPad)) anyBite = true;
  std::printf("[selftest-io-video-capture] pack: size=%d row1=%d noPad=%d row0=%d\n", sizeOk, row1Ok,
              noPad, row0Ok);
}

int runVideoCaptureSelfTest(bool injectBug) {
  @autoreleasepool {
    bool ok = true, anyBite = false;

    // (1) Closed-form pack tooth (always runs — the biteable half, no hardware).
    runPackTooth(injectBug, ok, anyBite);

    // (2) Live liveness probe (opportunistic; skipped without a camera/permission — emergent half).
    if (!injectBug) {  // the live path has no tooth; only run it on the production leg
      if (!VideoCapture::hasDefaultDevice()) {
        std::printf("[selftest-io-video-capture] live SKIP: no default video device\n");
      } else {
        int st = VideoCapture::authorizationStatus();
        if (st != (int)AVAuthorizationStatusAuthorized) {
          const char* r = st == (int)AVAuthorizationStatusDenied      ? "denied"
                          : st == (int)AVAuthorizationStatusRestricted ? "restricted"
                                                                       : "not-determined";
          std::printf("[selftest-io-video-capture] live SKIP: authorization=%s\n", r);
        } else {
          g_seenFrames = 0; g_lastW = 0; g_lastH = 0;
          VideoCapture cap;
          if (!cap.start(selftestFrameCb, nullptr)) {
            std::printf("[selftest-io-video-capture] live SKIP: session start failed\n");
          } else {
            for (int i = 0; i < 300 && g_seenFrames.load() == 0; ++i) {
              [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                       beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            }
            cap.stop();
            uint64_t frames = g_seenFrames.load();
            uint32_t w = g_lastW.load(), h = g_lastH.load();
            if (frames == 0) {
              std::printf("[selftest-io-video-capture] live FAIL: authorized but no frame in 3s\n");
              ok = false;
            } else {
              bool dimsOk = w > 0 && h > 0;
              std::printf(
                  "[selftest-io-video-capture] live frames=%llu last=%ux%u (content deferred) %s\n",
                  (unsigned long long)frames, w, h, dimsOk ? "ok" : "MISS");
              ok = ok && dimsOk;
            }
          }
        }
      }
    }

    if (injectBug) {
      if (!anyBite) {
        std::printf("[selftest-io-video-capture] injectBug did NOT trip (pack tooth is dead)\n");
        return 0;  // dead-tooth contract → NO-BITE list surfaces it
      }
      std::printf("[selftest-io-video-capture] injectBug correctly RED (pack smear)\n");
      return 1;
    }
    std::printf("[selftest-io-video-capture] %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
  }
}

}  // namespace sw
