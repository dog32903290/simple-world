// platform/video_decode_selftest — headless RED->GREEN proof the native video decoder fetches the
// CORRECT frame by INDEX (frame-accurate, no off-by-one, no dropped/doubled frame).
//
// What it proves (CLOSED FORM — no reference tool, no captured ground-truth):
//   1. Generate an 8-frame .mov with AVAssetWriter, each frame a DISTINCT solid RGB color (an
//      asymmetric per-frame ramp so any index misalignment is visible).
//   2. Open it with VideoDecoder, read metadata (frameCount==8, dims match).
//   3. For each index N in [0,8), decodeFrame(N) → read the center pixel → assert it equals the color
//      we AUTHORED into frame N. The Nth authored color IS the Nth expected pixel; the expectation is
//      independent of the decoder (we wrote those bytes). Also spot-checks decodeFrameAtTime.
//
// injectBug (setVideoDecodeBug(1)): the decoder fetches frame N+1's pixels for a request of N. Because
// the per-frame colors differ, frame 0 then reads frame 1's color and the assert breaks → RED. A real
// off-by-one in the frame positioning, exactly the regression a broken seek/quantize introduces.
//
// The .mov is written to a temp path and removed after. Frames are authored as BGRA CVPixelBuffers (the
// decoder's output format) so the round-trip color is exact for solid fills; a ±tol guards codec
// rounding (H.264/HEVC are lossy, but a large solid block round-trips within a few code values).
#include "platform/video_decode.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace sw {

namespace {

constexpr int kNumFrames = 8;
constexpr int kFrameW = 64;
constexpr int kFrameH = 64;
constexpr double kFps = 30.0;

// The authored per-frame colors (R,G,B), asymmetric so an off-by-one across the ramp is detectable.
struct Rgb { int r, g, b; };
const Rgb kColors[kNumFrames] = {
    {200, 30, 30},   {30, 200, 30},   {30, 30, 200},   {200, 200, 30},
    {200, 30, 200},  {30, 200, 200},  {150, 90, 40},   {40, 90, 150},
};

// Fill a BGRA CVPixelBuffer with a solid RGB color.
void fillBGRA(CVPixelBufferRef buf, Rgb c) {
  CVPixelBufferLockBaseAddress(buf, 0);
  size_t w = CVPixelBufferGetWidth(buf);
  size_t h = CVPixelBufferGetHeight(buf);
  size_t stride = CVPixelBufferGetBytesPerRow(buf);
  uint8_t* base = (uint8_t*)CVPixelBufferGetBaseAddress(buf);
  for (size_t y = 0; y < h; ++y) {
    uint8_t* row = base + y * stride;
    for (size_t x = 0; x < w; ++x) {
      row[x * 4 + 0] = (uint8_t)c.b;  // B
      row[x * 4 + 1] = (uint8_t)c.g;  // G
      row[x * 4 + 2] = (uint8_t)c.r;  // R
      row[x * 4 + 3] = 255;           // A
    }
  }
  CVPixelBufferUnlockBaseAddress(buf, 0);
}

// Write an 8-frame solid-color .mov to `outPath`. Returns true on success.
bool writeTestClip(const std::string& outPath) {
  @autoreleasepool {
    NSString* p = [NSString stringWithUTF8String:outPath.c_str()];
    NSURL* url = [NSURL fileURLWithPath:p];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

    NSError* err = nil;
    AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeQuickTimeMovie
                                                         error:&err];
    if (writer == nil) { std::printf("[selftest-io-video-decode] writer init failed\n"); return false; }

    // ProRes 4444 (visually lossless, keeps RGB primaries close to exact) so the solid-color round-trip
    // is tight — a small tolerance then keeps the frame-INDEX tooth sharp. H.264's 4:2:0 chroma
    // subsampling + YUV conversion rounds saturated primaries by ~30 (too soft a tooth); ProRes avoids
    // it. Fallback to H.264 if ProRes is unavailable on this OS (older machines) — the ≥60 inter-frame
    // channel gap means even the larger H.264 rounding can't let a WRONG frame masquerade.
    NSString* codec = AVVideoCodecTypeH264;
    if (@available(macOS 10.13, *)) {
      codec = AVVideoCodecTypeAppleProRes4444;
    }
    NSDictionary* vsettings = @{
      AVVideoCodecKey : codec,
      AVVideoWidthKey : @(kFrameW),
      AVVideoHeightKey : @(kFrameH),
    };
    AVAssetWriterInput* input =
        [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:vsettings];
    input.expectsMediaDataInRealTime = NO;

    NSDictionary* srcAttrs = @{
      (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
      (NSString*)kCVPixelBufferWidthKey : @(kFrameW),
      (NSString*)kCVPixelBufferHeightKey : @(kFrameH),
    };
    AVAssetWriterInputPixelBufferAdaptor* adaptor =
        [[AVAssetWriterInputPixelBufferAdaptor alloc] initWithAssetWriterInput:input
                                                  sourcePixelBufferAttributes:srcAttrs];
    if (![writer canAddInput:input]) return false;
    [writer addInput:input];

    if (![writer startWriting]) {
      std::printf("[selftest-io-video-decode] startWriting failed: %s\n",
                  writer.error.localizedDescription.UTF8String);
      return false;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];

    for (int i = 0; i < kNumFrames; ++i) {
      // Block until the input is ready (expectsMediaDataInRealTime=NO → spins briefly).
      while (!input.isReadyForMoreMediaData) { [NSThread sleepForTimeInterval:0.001]; }
      CVPixelBufferRef buf = NULL;
      CVReturn cr = CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &buf);
      if (cr != kCVReturnSuccess || buf == NULL) {
        // Pool may be nil before first append on some OS versions — create standalone.
        CVPixelBufferCreate(NULL, kFrameW, kFrameH, kCVPixelFormatType_32BGRA,
                            (__bridge CFDictionaryRef)srcAttrs, &buf);
      }
      if (buf == NULL) return false;
      fillBGRA(buf, kColors[i]);
      CMTime pts = CMTimeMake(i, (int32_t)kFps);
      if (![adaptor appendPixelBuffer:buf withPresentationTime:pts]) {
        std::printf("[selftest-io-video-decode] append frame %d failed\n", i);
        CVPixelBufferRelease(buf);
        return false;
      }
      CVPixelBufferRelease(buf);
    }
    [input markAsFinished];

    __block bool finished = false;
    [writer finishWritingWithCompletionHandler:^{ finished = true; }];
    // finishWriting is async; pump until done (headless, no runloop needed for a completion block on a
    // background queue — poll with a timeout).
    for (int spin = 0; spin < 5000 && !finished; ++spin) {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                               beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.002]];
    }
    if (!finished || writer.status != AVAssetWriterStatusCompleted) {
      std::printf("[selftest-io-video-decode] finishWriting status=%ld err=%s\n", (long)writer.status,
                  writer.error ? writer.error.localizedDescription.UTF8String : "none");
      return false;
    }
    return true;
  }
}

// Read the center pixel of a BGRA8 texture as RGB.
Rgb centerPixel(MTL::Texture* tex) {
  uint32_t w = (uint32_t)tex->width();
  uint32_t h = (uint32_t)tex->height();
  std::vector<uint8_t> buf((size_t)w * h * 4, 0);
  tex->getBytes(buf.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  size_t i = ((size_t)(h / 2) * w + (w / 2)) * 4;
  // BGRA8Unorm layout: byte order B,G,R,A.
  return {buf[i + 2], buf[i + 1], buf[i + 0]};
}

}  // namespace

int runVideoDecodeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  // Temp path under the OS temp dir.
  std::string tmp;
  @autoreleasepool {
    NSString* dir = NSTemporaryDirectory();
    NSString* fp = [dir stringByAppendingPathComponent:@"sw_video_decode_selftest.mov"];
    tmp = std::string(fp.UTF8String);
  }

  if (!writeTestClip(tmp)) {
    std::printf("[selftest-io-video-decode] FAIL: could not author test clip\n");
    pool->release();
    return 1;
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  if (!dev) {
    std::printf("[selftest-io-video-decode] FAIL: no Metal device\n");
    pool->release();
    return 1;
  }

  VideoDecoder dec;
  if (!dec.open(tmp)) {
    std::printf("[selftest-io-video-decode] FAIL: decoder open failed\n");
    dev->release();
    pool->release();
    return 1;
  }

  bool ok = true;
  // Metadata: 8 frames, 64x64. round(8/30 * 30) = 8.
  const bool metaOk =
      dec.frameCount() == (uint32_t)kNumFrames && dec.width() == (uint32_t)kFrameW &&
      dec.height() == (uint32_t)kFrameH;
  std::printf("[selftest-io-video-decode] meta: frames=%u dims=%ux%u fps=%.2f (want 8/64x64/30) %s\n",
              dec.frameCount(), dec.width(), dec.height(), dec.frameRate(), metaOk ? "ok" : "MISS");
  ok = ok && metaOk;

  setVideoDecodeBug(injectBug ? 1 : 0);

  // Tolerance absorbs the codec's color-range round-trip (a full-range↔video-range/YUV conversion
  // shifts saturated primaries — chiefly the green channel — by up to ~30 even under ProRes). It is set
  // WELL BELOW the min inter-frame channel gap: any two authored frames differ by ≥60 in at least one
  // channel (verified over all pairs), so tol=40 can never let a WRONG frame's color pass the assert (a
  // hit needs ALL three channels within tol, but the distinguishing channel of any off-by-one frame is
  // ≥60 away) — it only absorbs the codec rounding of the RIGHT frame. The frame-INDEX tooth
  // (what this test guards) stays sharp; the tolerance is a codec-fidelity allowance, not an index one.
  const int kTol = 40;
  for (int n = 0; n < kNumFrames && metaOk; ++n) {
    MTL::Texture* tex = dec.decodeFrame(dev, (uint32_t)n);
    if (tex == nullptr) {
      std::printf("[selftest-io-video-decode] frame %d: decode returned null\n", n);
      ok = false;
      continue;
    }
    Rgb got = centerPixel(tex);
    Rgb want = kColors[n];
    bool hit = std::abs(got.r - want.r) <= kTol && std::abs(got.g - want.g) <= kTol &&
               std::abs(got.b - want.b) <= kTol;
    if (!hit) ok = false;
    std::printf("[selftest-io-video-decode] frame %d: got(%d,%d,%d) want(%d,%d,%d) %s\n", n, got.r,
                got.g, got.b, want.r, want.g, want.b, hit ? "ok" : "MISS");
    tex->release();
  }

  // Spot-check decodeFrameAtTime: t=3.5/30 → floor(3.5)=frame 3 (production path, bug off). With the
  // bug on, index+1 → frame 4's color; the same asymmetric ramp makes that break too, but we assert
  // against the PRODUCTION expectation only when not injecting (the per-frame loop above already covers
  // the bug's off-by-one — this line just proves the time→index mapping).
  if (metaOk && !injectBug) {
    MTL::Texture* tex = dec.decodeFrameAtTime(dev, 3.0 / kFps + 0.5 / kFps);  // mid of frame 3
    if (tex) {
      Rgb got = centerPixel(tex);
      Rgb want = kColors[3];
      bool hit = std::abs(got.r - want.r) <= kTol && std::abs(got.g - want.g) <= kTol &&
                 std::abs(got.b - want.b) <= kTol;
      if (!hit) ok = false;
      std::printf("[selftest-io-video-decode] atTime(frame3): got(%d,%d,%d) want(%d,%d,%d) %s\n", got.r,
                  got.g, got.b, want.r, want.g, want.b, hit ? "ok" : "MISS");
      tex->release();
    } else {
      ok = false;
    }
  }

  setVideoDecodeBug(0);
  dec.close();
  dev->release();

  // Cleanup temp clip.
  @autoreleasepool {
    [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithUTF8String:tmp.c_str()]
                                               error:nil];
  }
  pool->release();

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-video-decode] injectBug did NOT trip (frame-index tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-video-decode] injectBug correctly RED (frame-index off-by-one)\n");
    return 1;
  }
  std::printf("[selftest-io-video-decode] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
