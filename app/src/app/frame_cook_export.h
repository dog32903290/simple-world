#pragma once
// app/frame_cook_export — the DETERMINISTIC offline cook (MV 承重 #3 frame-accurate export).
// Zone: app (product behaviour, a sibling of frame_cook::run's live driver).
//
// WHY A SEPARATE DRIVER (named fork of run()): the production run() advances the transport from a
// wall-clock Stopwatch (measureDeltaSeconds → transport.advance) and accumulates a process-lifetime
// wall run-clock (s_runTimeSecs). That is CORRECT for live playback but makes "export frame N == the
// frame you saw at time T" impossible — two exports of the same range would differ frame-to-frame
// because the dt each frame is the real elapsed wall time. TiXL solves this with
// Playback.IsRenderingToFile (Core/Animation/Playback.cs:61-76): setting it forces PlaybackSpeed=0
// and, in Update(), `FxTimeInBars = TimeInBars` (cs:110-113) — time is provided EXTERNALLY per frame,
// never advanced from the Stopwatch. This driver is the sw analog: the caller sets the frame index
// and fps, we place the playhead at frameIndex/fps (in seconds → bars) and step every stateful clock
// by the FIXED dt = 1/fps. No wall-clock is read anywhere on this path.
//
// SELF-CONTAINED STATE (the byte-match guarantee): run() keeps its transport / resident graph /
// stateful-op memory / runTime accumulator in FILE-STATIC singletons. Sharing those with the export
// would leak live state (a scrubbed playhead, a warm particle pool) into the render. So a
// DeterministicCookState owns ALL of it locally — a fresh state cooked from t=0 twice produces
// byte-identical target pixels (proven by --selftest-export). The live editor's transport is never
// touched by an export.
#include <cstdint>
#include <map>
#include <string>

namespace MTL { class Texture; }

namespace sw {
class PointGraph;
struct SymbolLibrary;
struct ResidentEvalGraph;
struct AudioReactionState;
struct StatefulValueState;
struct ContextVarMap;

namespace framecook {

// The per-export deterministic cook state. Construct ONE per export run, cook frames 0..N-1 through
// it IN ORDER (each cook advances the fixed-dt clocks), then discard. Owns the resident projection
// built from the lib + all cross-frame stateful memory so the render is reproducible and isolated
// from the live editor. Declared opaque here; the meat is in frame_cook_export.cpp.
struct DeterministicCookState {
  DeterministicCookState(const SymbolLibrary& lib, double fps);
  ~DeterministicCookState();
  DeterministicCookState(const DeterministicCookState&) = delete;
  DeterministicCookState& operator=(const DeterministicCookState&) = delete;

  struct Impl;
  Impl* p_;
};

// Cook ONE export frame deterministically into `pg`, then the caller reads pg.target(). `frameIndex`
// is the 0-based render frame; the playhead is placed at frameIndex/fps seconds (→ bars via the lib
// BPM) and the fx/sim clocks step by dt = 1/fps. `targetPath` = the resident path to realize (the
// same string run() resolves from pin/selection/terminal; the export entry resolves the terminal).
// Bug-injection hook (default off) for the determinism golden: exportBug != 0 re-introduces a
// wall-clock read on the sim clock so two renders diverge and the tooth bites. run() is untouched.
void cookFrameDeterministic(DeterministicCookState& st, PointGraph& pg, const std::string& targetPath,
                            uint32_t frameIndex, int exportBug = 0);

// The absolute frame time (seconds) the LAST cookFrameDeterministic placed the playhead at. Under the
// deterministic clock this is a pure function of (frameIndex, fps) — the SAME across every render;
// the wall-clock leak (exportBug) makes it drift. Exposed so --selftest-export can assert the clock
// fork directly (a graph-independent tooth on the mechanism, not on any node's time-sensitivity).
double lastFrameTimeSecs(const DeterministicCookState& st);

// Peek a resident node's extOut[outIdx] after the LAST cookFrameDeterministic (borrowed graph read,
// no mutation). 0.0f if the path does not resolve. Exposed so --selftest-export can assert a stateful
// op's value (e.g. GetFrameSpeedFactor) through the REAL export cook path end-to-end, not a fixture.
float residentNodeValue(const DeterministicCookState& st, const std::string& path, int outIdx = 0);

}  // namespace framecook
}  // namespace sw
