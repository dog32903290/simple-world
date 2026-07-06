// platform/video_decode — native macOS video-file frame decode (the DECODE leg of the PlayVideo /
// PlayVideoClip operators). Open a .mov/.mp4, ask for "frame N" or "the frame at time T", get back an
// MTL::Texture (BGRA8) of that decoded frame. The frame-index → pixels contract is the MV-tooling
// 承重: a deterministic export pulls frame indices, not wall-clock, so no frame is dropped or doubled.
//
// ZONE: platform (native macOS interface). AVFoundation AVAssetReader (+CoreVideo CVPixelBuffer +
// CoreMedia CMSampleBuffer) — the correct region for a native decoder, the same posture image_decode.mm
// (ImageIO) and audio_playback.mm (AVAudioEngine) occupy. It produces an MTL::Texture* directly (MTL::*
// is a system framework type, not a higher zone), so the whole decode → upload path stays in one
// platform file and never crosses a zone boundary (includes no runtime/app/ui/verify source).
//
// PARITY (named, honest): TiXL's PlayVideo decode is Windows MediaFoundation MediaEngine
// (PlayVideo.cs, MediaEngineEx) — a Windows-only stack with no portable equivalent. macOS replaces the
// WHOLE decoder with AVFoundation; there is no byte-for-byte clone of MediaFoundation. Parity is at the
// SEMANTIC level the timing core already pins: given a frame index (or a seek time), the decoder yields
// THAT frame's pixels. The closed-form timing math (seek/loop/quantize) lives in runtime/video_playback.h
// (--selftest-io-video-timing); this seam is the pixel-producing half it drives. The self-test proves
// the frame-index contract with a GENERATED clip whose Nth frame is a known solid color.
//
// FRAME-ACCURATE seek: AVAssetReader is a FORWARD-ONLY streaming reader (it has no random access). To
// fetch frame N we (re)start a reader with a timeRange whose start is N/fps and read the first sample.
// Because export is deterministic and usually advances forward, a decoder that already streamed past N-1
// reads the next sample cheaply; a backward/random seek rebuilds the reader from the target time. Either
// way the returned frame is the one whose presentation time contains N/fps — index-exact, never a
// nearest-keyframe approximation (the reader decodes through to the requested PTS).
//
// platform leaf: ObjC (AVFoundation) behind a C++ pimpl — callers include no ObjC, no runtime/app/ui
// headers. Load failure is non-fatal (returns false / nullptr, instance stays empty). Compiled WITH ARC
// (like audio_playback.mm — AVFoundation object lifetime stays simple under ARC).
#pragma once

#include <cstdint>
#include <string>

namespace MTL { class Device; class Texture; }

namespace sw {

class VideoDecoder {
 public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;

  // Open a video file at an ABSOLUTE path. Reads track metadata (dimensions, nominal frame rate,
  // duration, frame count) synchronously. Returns false on any failure (missing file, no video track,
  // undecodable) WITHOUT crashing; the instance stays unloaded. Replaces any previously opened file.
  bool open(const std::string& absPath);
  void close();
  bool isOpen() const;

  // Metadata (valid only while isOpen()). frameCount = round(duration * frameRate); frameRate is the
  // track's nominal FPS (CMTimeGetSeconds of the min-frame-duration reciprocal).
  uint32_t width() const;
  uint32_t height() const;
  double   frameRate() const;   // nominal FPS
  double   durationSeconds() const;
  uint32_t frameCount() const;  // total decodable frames (0 when not open)

  // Decode the frame at INDEX `frameIndex` (0-based) and upload it to a fresh MTL::Texture
  // (MTLPixelFormatBGRA8Unorm, StorageMode=Shared so a CPU getBytes readback works — the golden reads
  // it). Returns an OWNED texture (caller release()s / NS::TransferPtr) or nullptr on failure / out of
  // range. Frame-index-exact: the returned frame is the one whose presentation interval contains
  // frameIndex / frameRate (see FRAME-ACCURATE seek above). `dev` must be non-null.
  MTL::Texture* decodeFrame(MTL::Device* dev, uint32_t frameIndex);

  // Decode the frame at TIME `seconds` (the frame whose presentation interval contains it). Convenience
  // over decodeFrame: index = floor(seconds * frameRate), clamped to [0, frameCount-1]. Same ownership.
  MTL::Texture* decodeFrameAtTime(MTL::Device* dev, double seconds);

 private:
  struct Impl;
  Impl* impl_;
};

// ── Golden TEETH hook (--selftest-io-video-decode). Sticky module switch; the selftest restores 0.
// 0 = production. 1 = offset the requested frame index by +1 inside decodeFrame → frame N returns frame
// N+1's pixels. This is a REAL corruption of the decode's frame-positioning (not a want-flip): the
// selftest's WANT stays pinned to the authored per-frame color; the CODE path (which frame is fetched)
// is what changes. A dead-frame contract shows up as an off-by-one across the asymmetric color ramp.
void setVideoDecodeBug(int mode);
int  videoDecodeBug();

// --selftest-io-video-decode entry. Generates an 8-frame solid-color .mov with AVAssetWriter (each
// frame a distinct known RGB), decodes frames back with VideoDecoder, and asserts each frame's center
// pixel == its authored color (closed form: the Nth authored color is the Nth expected pixel — no
// reference tool needed). injectBug offsets the decode index by +1 so frame N returns frame N+1's color
// → the asymmetric per-frame colors break the assert → RED (a real frame-index misalignment, the exact
// regression a broken seek introduces). fn(bool injectBug) -> process exit code (0 PASS / 1 FAIL).
int runVideoDecodeSelfTest(bool injectBug);

}  // namespace sw
