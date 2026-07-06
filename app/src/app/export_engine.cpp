#include "app/export_engine.h"

#include <cstdio>
#include <vector>

#include <Metal/Metal.hpp>  // metal-cpp: PointGraph ctor + target() getBytes readback

#include "app/frame_cook_export.h"        // DeterministicCookState / cookFrameDeterministic
#include "platform/video_writer.h"        // VideoWriter / VideoCodec
#include "runtime/compound_graph.h"       // SymbolLibrary / defaultDrawTarget helpers via PointGraph
#include "runtime/point_graph.h"          // PointGraph

namespace sw::app {

namespace {

// Read back the point-graph target (RGBA8Unorm, StorageModeShared) into `out` (w*h*4 bytes, top row
// first). Same getBytes mechanism image_save/eye use; the product write lives in platform, but the
// export loop reads the pixels HERE (app) and hands the bytes to the writer. Returns false on a null
// or non-RGBA8 target (an unsupported layout must fail loudly, not write garbage).
bool readTargetRgba(MTL::Texture* tex, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
  if (!tex) return false;
  if (tex->pixelFormat() != MTL::PixelFormatRGBA8Unorm) return false;
  // The target resolution must match what we asked for (setFrameResolutionOverride); if the graph
  // produced a different size (a node forced its own Resolution enum), trust the texture's real size.
  const uint32_t tw = (uint32_t)tex->width();
  const uint32_t th = (uint32_t)tex->height();
  (void)w; (void)h;
  out.assign((size_t)tw * th * 4, 0);
  tex->getBytes(out.data(), tw * 4, MTL::Region::Make2D(0, 0, tw, th), 0);
  return true;
}

}  // namespace

ExportResult runExport(const ExportSettings& s, const SymbolLibrary& lib, MTL::Device* dev,
                       MTL::Library* shaderLib, MTL::CommandQueue* queue, const ProgressFn& onProgress) {
  ExportResult r;
  if (!dev || !shaderLib || !queue) { r.message = "no Metal context"; return r; }
  if (s.fps <= 0.0 || s.width == 0 || s.height == 0) { r.message = "bad settings (fps/size)"; return r; }
  if (s.endFrame < s.beginFrame) { r.message = "empty range (end < begin)"; return r; }
  if (s.outputPath.empty()) { r.message = "no output path"; return r; }

  const uint32_t total = s.endFrame - s.beginFrame + 1;

  // One point graph sized to the export resolution (WindowFollow resolves to this via the override).
  PointGraph pg(dev, shaderLib, queue, s.width, s.height);
  if (!pg.valid()) { r.message = "point graph invalid"; return r; }
  pg.setFrameResolutionOverride(RenderResolution{s.width, s.height});

  // Resolve the render target: an explicit resident path, else the current-composition terminal
  // (defaultDrawTarget, same rule the live viewport uses), else the root fallback as an id string.
  std::string targetPath = s.targetPath;
  if (targetPath.empty()) {
    int t = pg.defaultDrawTarget(lib, lib.rootId);
    targetPath = std::to_string(t);  // resident path == flat id string at the root (graph_bridge.h)
  }

  platform::VideoWriter writer;
  if (!writer.open(s.outputPath, s.codec, s.fps, (int)s.width, (int)s.height)) {
    r.message = "writer open failed (" + s.outputPath + ")";
    return r;
  }

  // The deterministic cook state — built ONCE from the frozen lib, owns all cross-frame memory.
  framecook::DeterministicCookState cookState(lib, s.fps);

  std::vector<uint8_t> rgba;
  bool cancelled = false;
  for (uint32_t i = 0; i < total; ++i) {
    const uint32_t frame = s.beginFrame + i;
    framecook::cookFrameDeterministic(cookState, pg, targetPath, frame, /*exportBug=*/0);

    if (!readTargetRgba(pg.target(), s.width, s.height, rgba)) {
      r.message = "readback failed at frame " + std::to_string(frame);
      writer.finish();
      return r;
    }
    if (!writer.pushFrame(rgba.data(), i)) {  // writer uses the 0-based EXPORT index for PTS/filename
      r.message = "encode failed at frame " + std::to_string(frame);
      writer.finish();
      return r;
    }
    if (onProgress && !onProgress(i + 1, total)) { cancelled = true; break; }
  }

  const bool finishedOk = writer.finish();
  r.framesWritten = writer.framesWritten();
  if (cancelled) { r.message = "cancelled"; r.ok = false; return r; }
  if (!finishedOk) { r.message = "finalize failed"; r.ok = false; return r; }
  r.ok = true;
  return r;
}

}  // namespace sw::app
