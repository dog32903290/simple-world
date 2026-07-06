#pragma once
// app/export_engine — the deterministic video/PNG-sequence export ENTRY (zone: app).
//
// Ties the three parts together: (1) the deterministic offline cook loop (frame_cook_export), which
// places the playhead at frameIndex/fps and cooks a frame with a fixed dt; (2) the GPU readback of
// the point-graph target (RGBA8, the same getBytes image_save uses); (3) the platform video/PNG
// writer. The UI settings window (range/fps/resolution picker + progress bar) is a SEPARATE lane —
// this header is the clean seam it (and the headless --export CLI) calls: one entry + a progress
// callback + a cancel flag. No imgui, no NSApplication.
//
// TiXL blueprint = Editor/Gui/Windows/RenderExport/RenderProcess.cs (the render loop + timing) +
// RenderSettings.cs (fps/range/codec). The frame→time mapping is TiXL RenderTiming: frame index →
// seconds → the composition clock. The writer backend is a NAMED FORK (AVFoundation, not MF/ffmpeg).
#include <cstdint>
#include <functional>
#include <string>

#include "platform/video_writer.h"  // VideoCodec (the ExportSettings default) — app→platform dep is legal

namespace MTL {
class Device;
class Library;
class CommandQueue;
}  // namespace MTL

namespace sw {
struct SymbolLibrary;

namespace app {

// What to render. Frames run over the CLOSED range [beginFrame, endFrame] inclusive at `fps`; the
// playhead for frame i sits at i/fps seconds (deterministic). w/h are the render resolution (pushed
// onto the point graph via setFrameResolutionOverride). `outputPath` = the .mov file (ProRes/H264)
// or the PNG-sequence directory. Defaults = a 2-second 30fps 512² ProRes4444 clip.
struct ExportSettings {
  uint32_t beginFrame = 0;
  uint32_t endFrame = 59;   // inclusive → 60 frames by default
  double fps = 30.0;
  uint32_t width = 512;
  uint32_t height = 512;
  platform::VideoCodec codec = platform::VideoCodec::ProRes4444;  // alpha path (MV 承重 #4)
  std::string outputPath;
  // Empty = the export resolves the current-composition terminal (defaultDrawTarget), same rule as
  // the live viewport. A non-empty resident path pins a specific node (future: export a sub-graph).
  std::string targetPath;
};

// Result of a run. `ok` is the overall verdict; `framesWritten` lets a headless caller/golden assert
// the count; `message` carries a human-readable failure reason (empty on success).
struct ExportResult {
  bool ok = false;
  uint32_t framesWritten = 0;
  std::string message;
};

// Progress callback: called once per rendered frame with (framesDone, framesTotal). Return false to
// CANCEL (the loop stops after the current frame, finalizes what was written, and returns ok=false
// with message "cancelled"). null = no progress reporting, never cancels.
using ProgressFn = std::function<bool(uint32_t framesDone, uint32_t framesTotal)>;

// Run the export against `lib` (the frozen document to render). `dev`/`lib`/`queue` are a live Metal
// context — the headless CLI creates them via CreateSystemDefaultDevice (same bootstrap the goldens
// use); the GUI passes its Renderer's. Synchronous: returns when the file is finalized or a frame
// failed. Never touches the live editor transport (the cook state is export-local).
ExportResult runExport(const ExportSettings& settings, const SymbolLibrary& lib,
                       MTL::Device* dev, MTL::Library* shaderLib, MTL::CommandQueue* queue,
                       const ProgressFn& onProgress = nullptr);

}  // namespace app
}  // namespace sw
