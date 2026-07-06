#include "platform/video_decode.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace sw {

// Sticky teeth switch (see header). File-scope so decodeFrame and the selftest share it.
static int g_videoDecodeBug = 0;
void setVideoDecodeBug(int mode) { g_videoDecodeBug = mode; }
int  videoDecodeBug() { return g_videoDecodeBug; }

// One decoder = one AVAsset + its video track metadata. AVAssetReader is forward-only, so we do NOT
// hold a long-lived reader; each decodeFrame builds a reader with a tight timeRange around the target
// frame and reads its first sample. This keeps random access (any frame index) simple and correct at
// the cost of a reader rebuild per fetch — acceptable for a deterministic export that pulls each frame
// once (and the common forward-advance case still decodes cheaply because the target sits at the range
// start). BGRA8 output (kCVPixelFormatType_32BGRA) → MTLPixelFormatBGRA8Unorm upload.
struct VideoDecoder::Impl {
  AVAsset* asset = nil;
  AVAssetTrack* track = nil;
  uint32_t width = 0;
  uint32_t height = 0;
  double frameRate = 0.0;
  double duration = 0.0;
  uint32_t frameCount = 0;
};

VideoDecoder::VideoDecoder() : impl_(new Impl()) {}
VideoDecoder::~VideoDecoder() { close(); delete impl_; }

bool VideoDecoder::isOpen() const { return impl_->asset != nil && impl_->track != nil; }
uint32_t VideoDecoder::width() const { return impl_->width; }
uint32_t VideoDecoder::height() const { return impl_->height; }
double VideoDecoder::frameRate() const { return impl_->frameRate; }
double VideoDecoder::durationSeconds() const { return impl_->duration; }
uint32_t VideoDecoder::frameCount() const { return impl_->frameCount; }

void VideoDecoder::close() {
  impl_->asset = nil;
  impl_->track = nil;
  impl_->width = impl_->height = impl_->frameCount = 0;
  impl_->frameRate = impl_->duration = 0.0;
}

bool VideoDecoder::open(const std::string& absPath) {
  close();
  @autoreleasepool {
    NSString* p = [NSString stringWithUTF8String:absPath.c_str()];
    NSURL* url = [NSURL fileURLWithPath:p];
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
    if (asset == nil) return false;

    NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    if (tracks.count == 0) return false;
    AVAssetTrack* track = tracks.firstObject;

    CGSize sz = track.naturalSize;
    double fps = track.nominalFrameRate;
    if (fps <= 0.0) {
      // Fall back to the reciprocal of the min frame duration if nominalFrameRate is unreported.
      CMTime mfd = track.minFrameDuration;
      double s = CMTimeGetSeconds(mfd);
      fps = (s > 0.0) ? 1.0 / s : 0.0;
    }
    double dur = CMTimeGetSeconds(asset.duration);
    if (sz.width <= 0 || sz.height <= 0 || fps <= 0.0 || dur <= 0.0) return false;

    impl_->asset = asset;
    impl_->track = track;
    impl_->width = (uint32_t)std::llround(sz.width);
    impl_->height = (uint32_t)std::llround(sz.height);
    impl_->frameRate = fps;
    impl_->duration = dur;
    // round(): a 8-frame clip at 30fps has duration 8/30; round(8/30*30)=8. floor would lose the last
    // frame to floating error; round is the index-count that matches the authored frame set.
    impl_->frameCount = (uint32_t)std::llround(dur * fps);
    return true;
  }
}

// Read the single video frame whose presentation interval contains `timeSeconds` and upload it to a
// texture. Builds a fresh forward-only reader clamped to [timeSeconds, end] and copies the first sample.
// Takes the asset+track directly (not the private Impl) so it stays a file-scope helper.
static MTL::Texture* readFrameAtTime(AVAsset* asset, AVAssetTrack* track, MTL::Device* dev,
                                     double timeSeconds) {
  @autoreleasepool {
    NSError* err = nil;
    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&err];
    if (reader == nil) return nullptr;

    NSDictionary* outSettings = @{
      (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
    };
    AVAssetReaderTrackOutput* out =
        [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:outSettings];
    out.alwaysCopiesSampleData = NO;
    if (![reader canAddOutput:out]) return nullptr;
    [reader addOutput:out];

    // Start the read at the target time so the FIRST emitted sample is the frame we want. A 600-scale
    // CMTime (common .mov timescale) is fine; the reader decodes through to the requested PTS.
    double t = timeSeconds < 0.0 ? 0.0 : timeSeconds;
    CMTime start = CMTimeMakeWithSeconds(t, 600);
    reader.timeRange = CMTimeRangeMake(start, kCMTimePositiveInfinity);
    if (![reader startReading]) return nullptr;

    CMSampleBufferRef sample = [out copyNextSampleBuffer];
    if (sample == NULL) { [reader cancelReading]; return nullptr; }

    CVImageBufferRef pixbuf = CMSampleBufferGetImageBuffer(sample);
    if (pixbuf == NULL) { CFRelease(sample); [reader cancelReading]; return nullptr; }

    CVPixelBufferLockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
    size_t w = CVPixelBufferGetWidth(pixbuf);
    size_t h = CVPixelBufferGetHeight(pixbuf);
    size_t srcStride = CVPixelBufferGetBytesPerRow(pixbuf);
    const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddress(pixbuf);

    MTL::Texture* tex = nullptr;
    if (base != nullptr && w > 0 && h > 0) {
      MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
      desc->setTextureType(MTL::TextureType2D);
      desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
      desc->setWidth(w);
      desc->setHeight(h);
      desc->setStorageMode(MTL::StorageModeShared);  // CPU getBytes readback (golden reads it)
      desc->setUsage(MTL::TextureUsageShaderRead);
      tex = dev->newTexture(desc);
      desc->release();
      if (tex != nullptr) {
        // Upload row by row (CVPixelBuffer stride may exceed w*4 due to alignment padding).
        const size_t dstStride = w * 4;
        if (srcStride == dstStride) {
          tex->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, base, dstStride);
        } else {
          std::vector<uint8_t> packed(dstStride * h);
          for (size_t y = 0; y < h; ++y)
            std::memcpy(packed.data() + y * dstStride, base + y * srcStride, dstStride);
          tex->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, packed.data(), dstStride);
        }
      }
    }

    CVPixelBufferUnlockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
    CFRelease(sample);
    [reader cancelReading];
    return tex;
  }
}

MTL::Texture* VideoDecoder::decodeFrame(MTL::Device* dev, uint32_t frameIndex) {
  if (!isOpen() || dev == nullptr || impl_->frameRate <= 0.0) return nullptr;
  uint32_t idx = frameIndex;
  if (videoDecodeBug() == 1) idx += 1;  // TEETH: off-by-one frame positioning
  if (idx >= impl_->frameCount) return nullptr;
  // Sample at the MIDDLE of the frame's presentation interval (idx + 0.5)/fps so rounding at the
  // interval boundary can never land us on the neighbouring frame — index-exact fetch.
  double t = (idx + 0.5) / impl_->frameRate;
  return readFrameAtTime(impl_->asset, impl_->track, dev, t);
}

MTL::Texture* VideoDecoder::decodeFrameAtTime(MTL::Device* dev, double seconds) {
  if (!isOpen() || dev == nullptr || impl_->frameRate <= 0.0) return nullptr;
  double idxF = std::floor(seconds * impl_->frameRate);
  if (idxF < 0.0) idxF = 0.0;
  uint32_t idx = (uint32_t)idxF;
  if (idx >= impl_->frameCount) idx = impl_->frameCount > 0 ? impl_->frameCount - 1 : 0;
  return decodeFrame(dev, idx);
}

}  // namespace sw
