// video_writer.mm — see video_writer.h. ObjC++ / ARC (like video_decode.mm / audio_capture.mm):
// pure AVFoundation objects, no metal-cpp pointers cross this seam, so ARC governs the ObjC
// lifetimes. NAMED FORK of TiXL MfVideoWriter.cs — AVAssetWriter instead of Media Foundation.
#include "platform/video_writer.h"

#include "platform/image_save.h"  // saveRgbaToPng: the PNG_SEQUENCE sink reuses the shared encoder

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <cstdio>

namespace sw {
namespace platform {

struct VideoWriter::Impl {
  VideoCodec codec = VideoCodec::ProRes4444;
  double fps = 30.0;
  int w = 0, h = 0;
  uint32_t framesWritten = 0;
  bool opened = false;

  // AVFoundation container path (ProRes4444 / H264). Unused for PNG_SEQUENCE.
  AVAssetWriter* writer = nil;
  AVAssetWriterInput* input = nil;
  AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;

  // PNG_SEQUENCE path: the output directory frames land in (frame_%06d.png).
  std::string pngDir;
};

VideoWriter::VideoWriter() : p_(std::make_unique<Impl>()) {}
VideoWriter::~VideoWriter() {
  // Never leave a half-open AVAssetWriter dangling (its background encode thread must be joined).
  if (p_ && p_->opened && p_->writer) finish();
}

namespace {
// Encode one RGBA8 (AlphaLast, top-row-first) frame into a CVPixelBuffer for the adaptor. The buffer
// is drawn from the adaptor's OWN pixelBufferPool when available — that pool hands out buffers in the
// exact format/attributes the encoder negotiated (avoids the "encoder rejects a hand-rolled 32RGBA
// buffer" failure). The source is RGBA; the encoder pool is BGRA (the universally-supported format for
// ProRes/H264), so we swizzle R<->B per pixel while copying row by row (dst stride may be padded).
// Returns a retained buffer the caller releases; NULL on failure.
CVPixelBufferRef makePixelBuffer(AVAssetWriterInputPixelBufferAdaptor* adaptor, const uint8_t* rgba,
                                 int w, int h) {
  CVPixelBufferRef pb = NULL;
  if (adaptor.pixelBufferPool) {
    CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &pb);
  }
  if (!pb) {  // no pool (writing not started yet) — fall back to a BGRA buffer
    NSDictionary* attrs = @{
      (id)kCVPixelBufferCGImageCompatibilityKey : @YES,
      (id)kCVPixelBufferCGBitmapContextCompatibilityKey : @YES,
    };
    if (CVPixelBufferCreate(kCFAllocatorDefault, w, h, kCVPixelFormatType_32BGRA,
                            (__bridge CFDictionaryRef)attrs, &pb) != kCVReturnSuccess || !pb) {
      return NULL;
    }
  }
  CVPixelBufferLockBaseAddress(pb, 0);
  uint8_t* dst = (uint8_t*)CVPixelBufferGetBaseAddress(pb);
  const size_t dstStride = CVPixelBufferGetBytesPerRow(pb);
  const size_t srcStride = (size_t)w * 4;
  for (int y = 0; y < h; ++y) {
    const uint8_t* srow = rgba + (size_t)y * srcStride;
    uint8_t* drow = dst + (size_t)y * dstStride;
    for (int x = 0; x < w; ++x) {  // RGBA (source) -> BGRA (encoder): swap R and B, keep A
      drow[x * 4 + 0] = srow[x * 4 + 2];  // B
      drow[x * 4 + 1] = srow[x * 4 + 1];  // G
      drow[x * 4 + 2] = srow[x * 4 + 0];  // R
      drow[x * 4 + 3] = srow[x * 4 + 3];  // A
    }
  }
  CVPixelBufferUnlockBaseAddress(pb, 0);
  return pb;
}
}  // namespace

bool VideoWriter::open(const std::string& absPath, VideoCodec codec, double fps, int w, int h) {
  if (p_->opened) return false;
  if (absPath.empty() || fps <= 0.0 || w <= 0 || h <= 0) return false;
  p_->codec = codec;
  p_->fps = fps;
  p_->w = w;
  p_->h = h;
  p_->framesWritten = 0;

  if (codec == VideoCodec::PngSequence) {
    p_->pngDir = absPath;
    p_->opened = true;  // per-frame files are written on push (image_save creates the dir)
    return true;
  }

  @autoreleasepool {
    NSString* path = [NSString stringWithUTF8String:absPath.c_str()];
    NSURL* url = [NSURL fileURLWithPath:path];
    // AVAssetWriter refuses to overwrite; clear a stale file so a re-run is idempotent.
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

    NSError* err = nil;
    p_->writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeQuickTimeMovie error:&err];
    if (!p_->writer) {
      std::fprintf(stderr, "[video_writer] AVAssetWriter init failed: %s\n",
                   err ? err.localizedDescription.UTF8String : "?");
      return false;
    }

    NSString* codecKey = (codec == VideoCodec::H264) ? AVVideoCodecTypeH264
                                                     : AVVideoCodecTypeAppleProRes4444;
    NSDictionary* settings = @{
      AVVideoCodecKey : codecKey,
      AVVideoWidthKey : @(w),
      AVVideoHeightKey : @(h),
    };
    p_->input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                   outputSettings:settings];
    p_->input.expectsMediaDataInRealTime = NO;  // offline export — never drop frames for wall time

    // BGRA is the format every AVFoundation video encoder accepts (ProRes4444 keeps the alpha, H264
    // drops it). makePixelBuffer swizzles the caller's RGBA into this. A hand-rolled 32RGBA buffer is
    // rejected by the ProRes encoder — the pool-with-BGRA path is the robust one.
    NSDictionary* pbAttrs = @{
      (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
      (id)kCVPixelBufferWidthKey : @(w),
      (id)kCVPixelBufferHeightKey : @(h),
    };
    p_->adaptor = [AVAssetWriterInputPixelBufferAdaptor
        assetWriterInputPixelBufferAdaptorWithAssetWriterInput:p_->input
                                   sourcePixelBufferAttributes:pbAttrs];

    if (![p_->writer canAddInput:p_->input]) {
      std::fprintf(stderr, "[video_writer] cannot add video input (codec unsupported?)\n");
      p_->writer = nil; p_->input = nil; p_->adaptor = nil;
      return false;
    }
    [p_->writer addInput:p_->input];
    if (![p_->writer startWriting]) {
      std::fprintf(stderr, "[video_writer] startWriting failed: %s\n",
                   p_->writer.error ? p_->writer.error.localizedDescription.UTF8String : "?");
      p_->writer = nil; p_->input = nil; p_->adaptor = nil;
      return false;
    }
    [p_->writer startSessionAtSourceTime:kCMTimeZero];
    p_->opened = true;
    return true;
  }
}

bool VideoWriter::pushFrame(const uint8_t* rgba, uint32_t frameIndex) {
  if (!p_->opened || !rgba) return false;

  if (p_->codec == VideoCodec::PngSequence) {
    char name[64];
    std::snprintf(name, sizeof(name), "/frame_%06u.png", frameIndex);
    if (!saveRgbaToPng(rgba, p_->w, p_->h, p_->pngDir + name)) return false;
    ++p_->framesWritten;
    return true;
  }

  @autoreleasepool {
    // Presentation time is FRAME-EXACT: frameIndex/fps. Use a large timescale (fps*1000) so a
    // non-integer fps (23.976) still lands on an exact rational tick, never wall-clock rounded.
    const int32_t timescale = (int32_t)llround(p_->fps * 1000.0);
    CMTime pts = CMTimeMake((int64_t)frameIndex * 1000, timescale);

    // The input pulls at its own pace; block until it is ready rather than dropping a frame
    // (offline export must emit EVERY frame in order — no realtime pressure).
    while (!p_->input.readyForMoreMediaData) {
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.002]];
    }
    CVPixelBufferRef pb = makePixelBuffer(p_->adaptor, rgba, p_->w, p_->h);
    if (!pb) return false;
    BOOL ok = [p_->adaptor appendPixelBuffer:pb withPresentationTime:pts];
    CVPixelBufferRelease(pb);
    if (!ok) {
      std::fprintf(stderr, "[video_writer] appendPixelBuffer failed at frame %u (status %ld): %s\n",
                   frameIndex, (long)p_->writer.status,
                   p_->writer.error ? p_->writer.error.localizedDescription.UTF8String : "no-error");
      return false;
    }
    ++p_->framesWritten;
    return true;
  }
}

bool VideoWriter::finish() {
  if (!p_->opened) return false;
  p_->opened = false;

  if (p_->codec == VideoCodec::PngSequence) return true;  // per-frame files already flushed

  @autoreleasepool {
    [p_->input markAsFinished];
    __block bool done = false;
    [p_->writer finishWritingWithCompletionHandler:^{ done = true; }];
    // finishWriting is async; spin the runloop until the encode thread signals completion.
    while (!done) {
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
    }
    bool ok = (p_->writer.status == AVAssetWriterStatusCompleted);
    if (!ok) {
      std::fprintf(stderr, "[video_writer] finish failed (status %ld): %s\n",
                   (long)p_->writer.status,
                   p_->writer.error ? p_->writer.error.localizedDescription.UTF8String : "?");
    }
    p_->writer = nil; p_->input = nil; p_->adaptor = nil;
    return ok;
  }
}

uint32_t VideoWriter::framesWritten() const { return p_->framesWritten; }

}  // namespace platform
}  // namespace sw
