#pragma once
// app/export_engine — the deterministic video/PNG-sequence export ENTRY (zone: app).
//
// The BLOCKING entry over the one export loop: since the driver convergence, runExport() is a thin
// pump over app/export_session (ExportSession = the single loop owning the PointGraph +
// DeterministicCookState + VideoWriter trio, the Command-view background guard, and PRE-ROLL). This
// header stays the clean seam the headless --export CLI calls (one entry + a progress callback + a
// cancel flag); the GUI's Render window pumps the SAME ExportSession per editor frame instead. No
// imgui, no NSApplication. The shared request/verdict/progress types (ExportSettings / ExportResult /
// ProgressFn) live HERE so both drivers speak one contract.
//
// PRE-ROLL (ExportSettings::preroll, default true): a from-beginFrame>0 export cooks frames
// [0, beginFrame) FIRST (no readback, no encode) so every stateful integrator (Damp/particle/
// feedback) reaches the SAME cross-frame state a continuous from-0 render would have had at
// beginFrame — otherwise those sims cold-start and frame `beginFrame` onward does not match a
// continuous render (refuter probe: hashes DIFFER without pre-roll). Implemented ONCE in
// ExportSession::begin (export_session.cpp); both drivers inherit it from there.
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
  // Pre-roll: when beginFrame > 0, stateful integrators (Damp/particle/feedback) would otherwise
  // cold-start AT beginFrame instead of having been cooked continuously from frame 0 — a from-30
  // export would then NOT match frame 30 of a from-0 export (refuter probe: hashes DIFFER). Default
  // ON: cookFrameDeterministic runs frames [0, beginFrame) first (no GPU readback, no encode) to warm
  // every stateful op's cross-frame memory before the real loop starts. --no-preroll (export_cli) is
  // the escape hatch (old/fast behavior, or when a caller KNOWS beginFrame==0 already). Defaulted so
  // existing ExportSettings{} call sites (goldens, other lanes) get the CORRECT behavior for free.
  bool preroll = true;
  // SELFTEST-ONLY bug injection: skip engaging the Command-view background ambient (the out-window-
  // persistence -> executor wiring, owned by ExportSession::begin — both drivers honor this). Always
  // false in production (export_cli/Render UI never set it). Exists so --selftest-export's background
  // tooth can prove the wiring is load-bearing — with this true, the export reverts to exactly the
  // PRE-FIX behavior (executor's own opaque-black default), so the corner-pixel assertion goes RED,
  // same shape as cookFrameDeterministic's exportBug.
  bool debugSkipBackgroundWire = false;
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
// PRE-ROLL PHASE (settings.preroll): the signature stays 2-arg (uint32_t can't carry a sign), so the
// pre-roll phase is distinguished by framesTotal==0 — a real render frame ALWAYS has framesTotal>=1
// (total = endFrame-beginFrame+1), so framesTotal==0 can only mean "still pre-rolling"; framesDone
// counts UP the pre-roll frames completed so far (1..beginFrame). A caller that doesn't care about the
// distinction (today: none do — the UI progress lane is a separate future piece) can ignore it and
// just treat any callback as "still going". Pre-roll frames are NOT cancellable (they do no visible
// I/O yet; the cancel flag only takes effect once the real per-frame loop below starts calling back
// with framesTotal==total).
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
