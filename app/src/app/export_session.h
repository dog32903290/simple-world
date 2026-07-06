#pragma once
// app/export_session — the STEPWISE (UI-driven, per-frame) wrapper around the deterministic export
// (zone: app). The GUI's Render window drives ONE export frame per editor frame so the progress bar
// stays live and Cancel is responsive; the headless CLI keeps using the synchronous export_engine
// runExport(). This wrapper owns the SAME per-run objects runExport builds (a PointGraph sized to the
// export resolution, a DeterministicCookState, a VideoWriter) but exposes them behind begin /
// stepOneFrame / finish / abort so the caller pumps the loop from its own frame tick.
//
// NAMED FORK of runExport's blocking loop, NOT a second export core. The determinism lives entirely in
// frame_cook_export (DeterministicCookState + cookFrameDeterministic) — this file NEVER touches it; it
// just re-drives the identical four-step iteration (cook → readback → writer.pushFrame → advance) one
// step at a time. This is the SAME split frame_cook.cpp's live run() vs frame_cook_export's offline
// driver already model: two drivers over one deterministic core. runExport() stays the batch driver
// (one call blocks the thread); ExportSession is the interactive driver (the caller keeps its UI alive
// between steps). TiXL blueprint = RenderProcess.ExportSession + RenderProcess.Update() pumping ONE
// frame per editor frame (RenderProcess.cs:189-232) — this is the sw analog of that per-frame pump.
#include <cstdint>
#include <memory>
#include <string>

#include "app/export_engine.h"  // ExportSettings / ExportResult (the shared render request/verdict)

namespace MTL {
class Device;
class Library;
class CommandQueue;
}  // namespace MTL

namespace sw {
struct SymbolLibrary;

namespace app {

// A per-frame-pumped export. Lifecycle: construct → begin() ONCE → stepOneFrame() repeatedly until
// done() → finish() (or abort() to cancel early). All state is export-local (a fresh PointGraph +
// DeterministicCookState + VideoWriter), so the live editor transport / warm pools are never touched —
// exactly runExport's isolation guarantee, kept intact. Not copyable (owns Metal + writer resources).
class ExportSession {
 public:
  ExportSession();
  ~ExportSession();
  ExportSession(const ExportSession&) = delete;
  ExportSession& operator=(const ExportSession&) = delete;

  // Build the export-local objects for `settings` against `lib` (the frozen document to render) with a
  // live Metal context (the GUI passes its Renderer's device/metallib/queue; the shell owns them). On
  // success the session is ACTIVE with framesDone()==0 and framesTotal()==(endFrame-beginFrame+1), and
  // the caller starts pumping stepOneFrame(). Returns false and sets lastMessage() (leaving the session
  // inactive) on a bad setting, an invalid point graph, or a writer that could not open its file.
  // Mirrors runExport's validation + setup block (export_engine.cpp:38-66); calling begin() twice on a
  // live session first tears down the previous one (defensive — the Render UI only begins when idle).
  bool begin(const ExportSettings& settings, const SymbolLibrary& lib, MTL::Device* dev,
             MTL::Library* shaderLib, MTL::CommandQueue* queue);

  // Cook + encode ONE frame (the next one in order) and advance. Call once per editor frame while
  // active() && !done(). Returns true while the run is healthy (whether or not this call was the last
  // frame — check done() after); returns false on a cook/readback/encode failure, which also aborts the
  // session (writer finalized, active()→false, lastMessage() set). A no-op returning true when the
  // session is not active or already done (so the caller can pump unconditionally). This is exactly one
  // iteration of runExport's for-loop body (export_engine.cpp:70-84), minus the progress callback.
  bool stepOneFrame();

  // Finalize the container and mark the session done (active()→false). Call after the last stepOneFrame()
  // (done()==true). Returns true iff every frame was written AND the writer finalized cleanly; sets
  // result() for the caller. Safe to call on an inactive session (returns the recorded result). Mirrors
  // runExport's tail (export_engine.cpp:87-92).
  bool finish();

  // Cancel an in-flight export: finalize whatever was written, mark cancelled, and go inactive. The
  // result() reads ok=false / message="cancelled" (the SAME verdict runExport returns when the progress
  // callback cancels). No-op on an inactive session.
  void abort();

  // ---- Machine-readable progress (the Render UI's progress bar + the state.json export hook) --------
  bool active() const;              // begun, not yet finished/aborted/failed
  bool done() const;               // every frame in the range has been stepped (ready for finish())
  bool cancelled() const;          // abort() was called (or a step failed) — the run did not complete
  uint32_t framesDone() const;      // frames stepped so far (0..framesTotal)
  uint32_t framesTotal() const;     // total frames in the requested range (endFrame-beginFrame+1)
  const std::string& lastMessage() const;  // failure reason ("" while healthy)
  const ExportResult& result() const;      // the final verdict (valid after finish()/abort())

 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace app
}  // namespace sw
