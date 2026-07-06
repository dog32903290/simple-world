#include "app/frame_cook_export.h"

#include <map>
#include <string>

#include "app/cook_host_values.h"         // cookHostValueNodes / cookPointValueFromGraph
#include "app/frame_cook.h"               // cookAudioReactionNodes / cookStatefulValueNodes (public seams)
#include "runtime/audio_reaction.h"       // AudioReactionState
#include "runtime/compound_graph.h"       // SymbolLibrary / CompositionSettings::bpm
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/point_graph.h"          // PointGraph::cookResident
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache
#include "runtime/spectrum_analyzer.h"    // SpectrumSnapshot (zeroed = deterministic)
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap
#include "runtime/transport.h"            // Transport

namespace sw::framecook {

// All cross-frame state lives HERE (not in file-statics) so an export is byte-reproducible and
// isolated from the live editor's warm state. `fps` fixes the dt for every clock on this path.
struct DeterministicCookState::Impl {
  double fps = 30.0;
  double bpm = 120.0;                 // pulled from the lib composition (bars<->secs home)
  const SymbolLibrary* lib = nullptr; // the frozen doc (automation curves resolve THROUGH it, like run())
  ResidentEvalGraph graph;           // built ONCE from the lib snapshot (export cooks a frozen doc)
  Transport transport;               // export-local playhead; the live g_transport is never touched
  double runTimeSecs = 0.0;          // fixed-dt accumulator (= TiXL RunTimeInSecs, but deterministic)
  std::map<std::string, AudioReactionState> arState;
  std::map<std::string, StatefulValueState> svState;
  ContextVarMap ctxVars;
  double lastFrameTimeSecs = 0.0;  // the absolute time the last cook placed the playhead at (probe)
};

DeterministicCookState::DeterministicCookState(const SymbolLibrary& lib, double fps)
    : p_(new Impl()) {
  p_->fps = (fps > 0.0) ? fps : 30.0;
  p_->bpm = lib.composition.bpm > 0.0 ? lib.composition.bpm : 120.0;
  p_->lib = &lib;  // borrowed; the caller keeps the doc alive for the whole export (process-long doc::g_lib)
  p_->transport.bpm = p_->bpm;
  // Build the resident projection from the (frozen) lib, exactly like run()'s rebuild leg — but
  // ONCE: an export renders a snapshot of the document, so there is no per-frame libRevision watch.
  p_->graph = buildEvalGraph(lib, lib.rootId);
  initResidentCache(p_->graph);
}

DeterministicCookState::~DeterministicCookState() { delete p_; }

void cookFrameDeterministic(DeterministicCookState& st, PointGraph& pg, const std::string& targetPath,
                            uint32_t frameIndex, int exportBug) {
  DeterministicCookState::Impl& s = *st.p_;

  // --- DETERMINISTIC CLOCK (TiXL IsRenderingToFile, Playback.cs:61-113) ---
  // Playhead placed EXACTLY at frameIndex/fps seconds → bars. No wall-clock, no accumulation drift:
  // frame N is always the same time regardless of how long the render took. This is the whole point.
  const double dtSecs = 1.0 / s.fps;                       // FIXED sim/fx step
  // The determinism TOOTH: exportBug 1 makes the absolute frame time depend on a PROCESS-LIFETIME
  // call counter instead of the frame index — the EXACT regression this path prevents (run() reads
  // a wall Stopwatch, so re-using its accumulator here would make frame N's time drift run-to-run).
  // A monotonically-growing counter stands in for that leaked wall clock; two renders of the same
  // frame index then land on DIFFERENT times, so the pixels (particle time = fxSecs) diverge.
  static uint64_t s_leakCounter = 0;  // the "leaked wall clock" the bug wrongly reads
  const double timeJitter = (exportBug == 1) ? ((double)(s_leakCounter++) * dtSecs) : 0.0;
  const double posSecs = (double)frameIndex * dtSecs + timeJitter;  // frame → absolute time (seconds)
  s.lastFrameTimeSecs = posSecs;  // probe for the determinism golden
  const double posBars = s.transport.barsFromSeconds(posSecs);
  s.transport.scrub(posBars);          // playhead = automation sampling clock (Playback.TimeInBars)
  // fxTime (stateful-sim clock) steps by the FIXED dt. In TiXL IsRenderingToFile, FxTimeInBars ==
  // TimeInBars (cs:112) — the fx clock is locked to the frame time, never free-running. We mirror
  // that: fxTime tracks the deterministic playhead, so particle/feedback sims advance one fixed step.
  s.transport.fxTime = posBars;
  const double dtSimSecs = dtSecs;  // sim dt is always the fixed step (the leak is in absolute time)
  s.runTimeSecs = posSecs;  // fixed-dt run clock (StopWatch/PlayAudioClip read this; no wall drift)

  const double fxBars = s.transport.fxTime;
  const double fxSecs = s.transport.secondsFromBars(fxBars);

  // --- COOK SEQUENCE (mirrors run(), MINUS the live-device legs) ---
  // AudioReaction/DetectBpm/IoDevice/Link/AudioPlayback pull LIVE hardware state (mic spectrum, MIDI,
  // sockets) which is inherently non-deterministic — an offline render must NOT depend on them, so
  // this path feeds a ZEROED spectrum to AudioReaction (its output is then a pure function of params +
  // fixed fxTime) and skips the pure-device cooks entirely. (Dossier: recording live IO onto a track
  // for deterministic replay = TiXL #992, a future lane; today's export renders the visual graph.)
  const SpectrumSnapshot spec{};  // all-zero: deterministic AudioReaction input
  cookAudioReactionNodes(s.graph, spec, s.transport, frameIndex, s.lib, s.arState);

  // Stateful value ops (Damp/Spring/Anim*) — driven by the FIXED dt, so their integrators reproduce.
  {
    bool libDirtied = false;
    cookStatefulValueNodes(s.graph, (float)dtSecs, (float)fxSecs, s.runTimeSecs, s.transport,
                           frameIndex, s.lib, s.svState, s.ctxVars, /*ctxVarBug=*/0,
                           /*mutableLib=*/nullptr, &libDirtied);
    // An export renders a FROZEN doc — we never write keyframe edits back mid-render, so libDirtied
    // is intentionally ignored here (no bumpLibRevision; the graph is built once above).
  }

  // Host-value currencies (String / host-scalar / ColorList + RequestedResolution). cookHostValueNodes
  // takes a MUTABLE lib (it can run [SetBpm]/[SetPlaybackTime]); an export renders a FROZEN doc and OWNS
  // the playhead, so we pass lib=nullptr + transport=nullptr — those consumer legs are skipped (no doc
  // mutation, no SetPlaybackTime fighting the frame clock). String/host-scalar/ColorList still cook.
  cookHostValueNodes(s.graph, (float)posBars, (float)fxBars, /*lib=*/nullptr,
                     pg.frameResolution().w, pg.frameResolution().h, &s.ctxVars, /*transport=*/nullptr);

  // The GPU context — the SIM clock. ctx.time = fxSecs (deterministic), ctx.deltaTime = the FIXED
  // step (or the bugged jitter). This is where the particle/feedback shaders read their time.
  EvaluationContext ctx{};
  ctx.frameIndex = frameIndex;
  ctx.time = (float)fxSecs;
  ctx.deltaTime = (float)dtSimSecs;
  ctx.localFxTime = (float)fxBars;

  pg.cookResident(s.graph, ctx, /*reg=*/nullptr, targetPath, (float)posBars, (float)fxBars,
                  s.lib, &s.ctxVars);
  cookPointValueFromGraph(s.graph, pg, (float)posBars, (float)fxBars, /*lib=*/nullptr);
}

double lastFrameTimeSecs(const DeterministicCookState& st) { return st.p_->lastFrameTimeSecs; }

}  // namespace sw::framecook
