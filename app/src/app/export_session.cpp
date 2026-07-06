#include "app/export_session.h"

#include <vector>

#include <Metal/Metal.hpp>  // metal-cpp: PointGraph ctor + target() getBytes readback

#include "app/frame_cook_export.h"   // DeterministicCookState / cookFrameDeterministic
#include "platform/video_writer.h"   // VideoWriter / VideoCodec
#include "runtime/compound_graph.h"  // SymbolLibrary + defaultDrawTarget (via PointGraph)
#include "runtime/point_graph.h"     // PointGraph

namespace sw::app {

namespace {

// Read back the point-graph target (RGBA8Unorm, StorageModeShared) into `out`. Identical to
// export_engine.cpp's readTargetRgba — the readback is the same getBytes mechanism image_save/eye use.
// Duplicated (not shared) to keep this stepwise driver from reaching into the batch driver's anonymous
// namespace; it is a five-line, side-effect-free helper (本質重複 minimal, not a design seam to factor).
bool readTargetRgba(MTL::Texture* tex, std::vector<uint8_t>& out) {
  if (!tex) return false;
  if (tex->pixelFormat() != MTL::PixelFormatRGBA8Unorm) return false;
  const uint32_t tw = (uint32_t)tex->width();
  const uint32_t th = (uint32_t)tex->height();
  out.assign((size_t)tw * th * 4, 0);
  tex->getBytes(out.data(), tw * 4, MTL::Region::Make2D(0, 0, tw, th), 0);
  return true;
}

}  // namespace

// The export-local objects, held across editor frames. Owns the SAME trio runExport builds locally
// (PointGraph / DeterministicCookState / VideoWriter) — here they persist between stepOneFrame() calls.
struct ExportSession::Impl {
  ExportSettings settings;
  std::unique_ptr<PointGraph> pg;
  std::unique_ptr<framecook::DeterministicCookState> cookState;
  platform::VideoWriter writer;
  std::string targetPath;
  std::vector<uint8_t> rgba;  // reused readback scratch (one alloc for the whole run)

  uint32_t total = 0;
  uint32_t framesDone = 0;
  bool active = false;
  bool cancelled = false;
  std::string message;
  ExportResult result;
};

ExportSession::ExportSession() : p_(std::make_unique<Impl>()) {}
ExportSession::~ExportSession() = default;

bool ExportSession::begin(const ExportSettings& s, const SymbolLibrary& lib, MTL::Device* dev,
                          MTL::Library* shaderLib, MTL::CommandQueue* queue) {
  // Defensive: a live session is torn down first (the Render UI only begins when idle, but keep the
  // contract crisp — a stray begin() must not leak the previous PointGraph/writer). abort() finalizes
  // the old writer; a fresh Impl then discards the (now-closed) old objects. Impl is non-copyable
  // (owns a unique_ptr + a VideoWriter), so we replace the whole pimpl rather than assign fields.
  if (p_->active) abort();
  p_ = std::make_unique<Impl>();

  // Validation mirrors runExport (export_engine.cpp:38-42): a bad request fails HERE, before any Metal
  // or file work, with the same messages so the two drivers report identically.
  auto fail = [&](const char* m) -> bool { p_->message = m; return false; };
  if (!dev || !shaderLib || !queue) return fail("no Metal context");
  if (s.fps <= 0.0 || s.width == 0 || s.height == 0) return fail("bad settings (fps/size)");
  if (s.endFrame < s.beginFrame) return fail("empty range (end < begin)");
  if (s.outputPath.empty()) return fail("no output path");

  p_->settings = s;
  p_->total = s.endFrame - s.beginFrame + 1;

  // One point graph sized to the export resolution; WindowFollow resolves to this via the override.
  p_->pg = std::make_unique<PointGraph>(dev, shaderLib, queue, s.width, s.height);
  if (!p_->pg->valid()) { p_->pg.reset(); return fail("point graph invalid"); }
  p_->pg->setFrameResolutionOverride(RenderResolution{s.width, s.height});

  // Resolve the render target the same way the live viewport + runExport do: explicit resident path,
  // else the current-composition terminal (defaultDrawTarget), else the root fallback as an id string.
  p_->targetPath = s.targetPath;
  if (p_->targetPath.empty()) {
    int t = p_->pg->defaultDrawTarget(lib, lib.rootId);
    p_->targetPath = std::to_string(t);
  }

  if (!p_->writer.open(s.outputPath, s.codec, s.fps, (int)s.width, (int)s.height)) {
    p_->pg.reset();
    return fail(("writer open failed (" + s.outputPath + ")").c_str());
  }

  // The deterministic cook state — built ONCE from the frozen lib, owns all cross-frame memory. This is
  // the byte-match core; ExportSession only pumps it (never modifies frame_cook_export).
  p_->cookState = std::make_unique<framecook::DeterministicCookState>(lib, s.fps);
  p_->active = true;
  return true;
}

bool ExportSession::stepOneFrame() {
  if (!p_->active || framesDone() >= p_->total) return true;  // idle / done -> pumpable no-op

  // Exactly one iteration of runExport's for-loop body (export_engine.cpp:71-83): cook the next frame
  // deterministically, read it back, encode it. The writer uses the 0-based EXPORT index for PTS/filename.
  const uint32_t i = p_->framesDone;
  const uint32_t frame = p_->settings.beginFrame + i;
  framecook::cookFrameDeterministic(*p_->cookState, *p_->pg, p_->targetPath, frame, /*exportBug=*/0);

  if (!readTargetRgba(p_->pg->target(), p_->rgba)) {
    p_->message = "readback failed at frame " + std::to_string(frame);
    p_->writer.finish();
    p_->cancelled = true;
    p_->active = false;
    p_->result = ExportResult{false, p_->writer.framesWritten(), p_->message};
    return false;
  }
  if (!p_->writer.pushFrame(p_->rgba.data(), i)) {
    p_->message = "encode failed at frame " + std::to_string(frame);
    p_->writer.finish();
    p_->cancelled = true;
    p_->active = false;
    p_->result = ExportResult{false, p_->writer.framesWritten(), p_->message};
    return false;
  }
  p_->framesDone = i + 1;
  return true;
}

bool ExportSession::finish() {
  if (!p_->active) return p_->result.ok;  // already finalized (finish/abort) -> return recorded verdict
  const bool finishedOk = p_->writer.finish();
  p_->active = false;
  p_->result.framesWritten = p_->writer.framesWritten();
  if (!finishedOk) {
    p_->message = "finalize failed";
    p_->result.ok = false;
    p_->result.message = p_->message;
    return false;
  }
  p_->result.ok = true;
  p_->result.message.clear();
  return true;
}

void ExportSession::abort() {
  if (!p_->active) return;
  p_->writer.finish();  // finalize whatever was written (idempotent-safe per video_writer.h)
  p_->active = false;
  p_->cancelled = true;
  p_->message = "cancelled";
  p_->result = ExportResult{false, p_->writer.framesWritten(), "cancelled"};
}

bool ExportSession::active() const { return p_->active; }
bool ExportSession::done() const { return p_->framesDone >= p_->total && p_->total > 0; }
bool ExportSession::cancelled() const { return p_->cancelled; }
uint32_t ExportSession::framesDone() const { return p_->framesDone; }
uint32_t ExportSession::framesTotal() const { return p_->total; }
const std::string& ExportSession::lastMessage() const { return p_->message; }
const ExportResult& ExportSession::result() const { return p_->result; }

}  // namespace sw::app
