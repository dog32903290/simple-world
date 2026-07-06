#pragma once
// platform/video_writer — PRODUCT-facing frame-sequence → video-file writer (zone: platform).
//
// The frame-accurate deterministic export (MV 承重 #3/#4) needs a real container the user
// keeps: ProRes 4444 (alpha, 疊實拍命脈) or H.264. TiXL writes video through Media Foundation
// (Editor/Gui/Windows/RenderExport/MF/MfVideoWriter.cs — 8-bit, Windows-only; v4.2 swapped it
// for ffmpeg to reach Linux). macOS gives us AVFoundation natively, so this is a NAMED FORK of
// TiXL's writer: same CALLER contract (open with fps+size+range, push RGBA frames in order,
// finish), different backend — AVAssetWriter + AVAssetWriterInputPixelBufferAdaptor. ProRes4444
// with a real alpha channel is the structural WIN over TiXL's MF/ffmpeg 8-bit path.
//
// The offline cook loop (app/export_engine) hands us RGBA8 frames read back from the point-graph
// target (platform/image_save already owns the same getBytes readback for the PNG path). This
// writer only encodes — it never touches Metal or the cook. One frame in, one frame out, in order.
//
// Compiled WITH ARC (like video_decode.mm / audio_capture.mm): pure AVFoundation ObjC objects,
// no metal-cpp pointers cross this seam (the caller passes a plain RGBA byte buffer).
#include <cstdint>
#include <memory>
#include <string>

namespace sw {
namespace platform {

// The container/codec the writer emits. PNG_SEQUENCE is the simple fallback (one file per frame,
// via image_save's encoder) — no container, always available, the golden's determinism proof.
enum class VideoCodec {
  ProRes4444,   // AVVideoCodecTypeAppleProRes4444 — 10-bit + alpha (the MV alpha path)
  H264,         // AVVideoCodecTypeH264 — 8-bit, no alpha (smaller preview deliverable)
  PngSequence,  // per-frame PNG (fallback / determinism golden; `path` is a directory)
};

// A frame sink. Open() once, pushFrame() per frame IN ORDER (top row first, RGBA8, tightly
// packed w*4 bytes/row — the exact layout image_save writes), finish() to flush the file.
// All calls are synchronous; a false return means the encode/IO failed and the caller aborts.
class VideoWriter {
 public:
  VideoWriter();
  ~VideoWriter();
  VideoWriter(const VideoWriter&) = delete;
  VideoWriter& operator=(const VideoWriter&) = delete;

  // Open the destination. `absPath` = output file (.mov for ProRes/H264) or output directory
  // (PNG_SEQUENCE). `fps` = frames per second (the export dt is 1/fps; must be > 0). `w`/`h` =
  // pixel dimensions of every pushed frame (must be > 0). Returns false on a bad arg, an
  // unwritable path, or an AVAssetWriter setup failure.
  bool open(const std::string& absPath, VideoCodec codec, double fps, int w, int h);

  // Encode ONE frame. `rgba` points to w*h*4 bytes (top row first, non-premultiplied AlphaLast —
  // ProRes4444 carries the alpha verbatim). `frameIndex` is the 0-based export frame number; the
  // presentation time is frameIndex/fps so the container timing is frame-exact (never wall-clock).
  // Returns false if not open or the encode failed.
  bool pushFrame(const uint8_t* rgba, uint32_t frameIndex);

  // Flush and close the container. After finish() the writer is spent (open() again for a new
  // file). Returns false if the writer never opened or the finalize failed. Idempotent-safe:
  // calling finish() twice returns false the second time without crashing.
  bool finish();

  // Frames successfully pushed so far (the golden asserts this == expected frame count).
  uint32_t framesWritten() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace platform
}  // namespace sw
