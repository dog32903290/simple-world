#include "app/frame_cook.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

#include "app/audio_monitor.h"            // live spectrum snapshot (DSP fed by capture)
#include "app/document.h"                 // g_lib + libRevision (projection contract)
#include "runtime/audio_reaction.h"       // cookAudioReaction (TiXL AudioReaction parity)
#include "runtime/graph_bridge.h"         // refreshCompoundSpecs (frame-boundary spec swap)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/point_graph.h"          // PointGraph::cookResident
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph
#include "runtime/spectrum_analyzer.h"    // SpectrumSnapshot
#include "runtime/transport.h"            // Transport (two-clock playback head, S5)

namespace sw::framecook {
namespace {

// App clock (was main's g_time/g_frameIndex — product behaviour, so it lives here).
uint32_t g_frameIndex = 0;

// S5: the FrameScheduler's wall-clock. The real per-frame deltaTime (NOT a hardcoded 1/60) is
// measured HERE at the frame boundary and handed to the Transport — this app-layer driver is the
// "FrameScheduler := graph-level owner of frameIndex/time/deltaTime" (FRAME_SCHEDULER_CONTRACT):
// one dt per frame, every cooked node downstream reads the SAME ctx built from it.
Transport g_transport;
bool g_haveLastFrame = false;
std::chrono::steady_clock::time_point g_lastFrameTp;

// Pull dt from the wall clock (steady_clock — monotonic, immune to NTP). First frame has no prior
// timestamp -> dt = 0 (no spurious jump). Clamped to a sane ceiling so a stalled frame (debugger
// breakpoint, window drag) doesn't teleport the playhead.
double measureDeltaSeconds() {
  auto now = std::chrono::steady_clock::now();
  if (!g_haveLastFrame) { g_lastFrameTp = now; g_haveLastFrame = true; return 0.0; }
  double dt = std::chrono::duration<double>(now - g_lastFrameTp).count();
  g_lastFrameTp = now;
  if (dt < 0.0) dt = 0.0;
  if (dt > 0.25) dt = 0.25;  // ceiling: never advance > 1/4s of composition time in one frame.
  return dt;
}

// PRODUCTION (compound spine, lib-native since 批次 3 N2): the frame cook walks this
// RESIDENT eval graph, projected straight from doc::g_lib whenever doc::libRevision()
// changes (rebuild-on-edit; paths == child-id chains, so per-path GPU buffers + stateful op
// state SURVIVE the rebuild). Incremental patch wiring (patch*/patchLib*) replaces the
// rebuild in a later cut — semantics already pinned by the patch goldens.
ResidentEvalGraph g_residentGraph;
uint64_t g_builtRev = 0;  // doc::libRevision() the projection was built from (0 = never)

}  // namespace

const float* residentOut(const char* path) {
  const ResidentNode* n = g_residentGraph.node(path);
  return n ? n->extOut : nullptr;
}

void run(PointGraph& pg, const std::string& targetPath) {
  // Frame start = THE defined point where the composition path is validated (document.h):
  // every panel drawn after this agrees on the current symbol; mid-frame edits dangle the
  // tail at most until the next frame.
  doc::validateCompositionPath();

  // Projection rebuild BEFORE the AudioReaction cooker, so extOut lands on the fresh graph.
  if (g_builtRev != doc::libRevision()) {
    // Refresh the dynamic compound-spec table HERE, at the frame boundary — never from the
    // command stack mid-frame, where the inspector/canvas hold NodeSpec* into the table
    // (swap = use-after-free, refuter N2 #1). The UI reads one-frame-stale compound specs
    // after an edit, same frame semantics as TiXL.
    refreshCompoundSpecs(doc::g_lib);
    g_residentGraph = buildEvalGraph(doc::g_lib, doc::g_lib.rootId);
    g_builtRev = doc::libRevision();
  }

  // Advance the two-clock transport by the REAL frame duration (S5). The BPM home is the lib's
  // composition settings (saved/loaded) — pull it onto the transport each frame so a load /
  // inspector edit takes effect (cheap, no edge to chase).
  g_transport.bpm = doc::g_lib.composition.bpm;
  const double dtSecs = measureDeltaSeconds();
  g_transport.advance(dtSecs);

  // Both clocks in BARS (P3, the resident eval ctx is bars-native): localTime = the PLAYHEAD
  // (automation samples this), localFxTime = the WALL CLOCK (stateful sims sample this; keeps
  // running while paused). AudioReaction wants its time in SECONDS (TiXL LocalFxTime, seconds) so
  // it reads fxTime converted back through the BPM — a paused composition keeps its hit timing live.
  const double posBars = g_transport.position;
  const double fxBars = g_transport.fxTime;
  const double fxSecs = g_transport.secondsFromBars(fxBars);

  const SpectrumSnapshot spec = audio_monitor::spectrum();

  // Cook every AudioReaction instance (TiXL parity): resolve its params through the resident
  // drivers (override/default/wire — the slice-2b seam), run the stateful algorithm on the
  // live spectrum, write its 3 outputs onto the resident node's extOut — the resident cook's
  // value wires read these (evalResidentFloat's no-evaluate path), and the UI face reads
  // them back through residentOut(). State keys off the resident PATH, so it survives
  // rebuilds AND stays per-instance inside compounds.
  {
    static std::map<std::string, AudioReactionState> s_arState;
    ResidentEvalCtx rctx;
    rctx.localTime = (float)posBars;    // playhead (bars) — automation sampling reads this
    rctx.localFxTime = (float)fxBars;   // wall clock (bars) — the Time op's evaluate() reads this
    rctx.frameIndex = g_frameIndex;
    rctx.lib = &doc::g_lib;             // S3 接通: Automation drivers resolve curves THROUGH the lib
    for (ResidentNode& rn : g_residentGraph.nodes) {
      if (rn.opType != "AudioReaction") continue;
      std::map<std::string, float> P = resolveResidentFloatInputs(g_residentGraph, rn, rctx);
      AudioReactionParams p;
      p.amplitude = P["Amplitude"];
      p.inputBand = (int)(P["InputBand"] + 0.5f);
      p.windowCenter = P["WindowCenter"];
      p.windowWidth = P["WindowWidth"];
      p.windowEdge = P["WindowEdge"];
      p.threshold = P["Threshold"];
      p.minTimeBetweenHits = P["MinTimeBetweenHits"];
      p.output = (int)(P["Output"] + 0.5f);
      p.bias = P["Bias"];
      p.reset = P["Reset"] > 0.5f;
      // Wall clock (seconds): AudioReaction is a stateful sim — its hit timing must NOT freeze on
      // pause (L8 fxTime brother). Frozen only when both clocks frozen (no idle motion).
      const AudioReactionOut o = cookAudioReaction(spec, p, fxSecs, s_arState[rn.path]);
      rn.extOut[0] = o.level;
      rn.extOut[1] = o.wasHit ? 1.0f : 0.0f;
      rn.extOut[2] = (float)o.hitCount;
    }
  }

  ++g_frameIndex;
  // The GPU EvaluationContext (16-byte) carries the WALL CLOCK (fxTime) as `time`: GPU-side Time/
  // particle sims are the fxTime consumers (暫停畫面不死). Automation (a CPU value-graph concern)
  // reads the playhead via the resident ctx above — it never reaches the shader through here.
  EvaluationContext ctx{};
  ctx.frameIndex = g_frameIndex;
  ctx.time = (float)fxSecs;       // wall clock, seconds (sims keep running while paused)
  ctx.deltaTime = (float)dtSecs;

  // The resident cook fills ITS two-clock ctx (localTime/localFxTime, bars) from the transport
  // via these params, so automation-driven Float inputs sampled inside the cook walk the curve at
  // the playhead. (point_graph_resident reads them off the args — no more ctx.time placeholder.)
  pg.cookResident(g_residentGraph, ctx, /*reg=*/nullptr, targetPath,
                  (float)posBars, (float)fxBars, &doc::g_lib);
}

// --- Transport control surface (UI/selftest drive the playback head through these) ---
void transportPlay()  { g_transport.play(); }
void transportPause() { g_transport.pause(); }
void transportToggle() { g_transport.toggle(); }
void transportScrub(double bars) { g_transport.scrub(bars); }
bool transportPlaying() { return g_transport.playing(); }
double transportPosition() { return g_transport.position; }
double transportFxTime() { return g_transport.fxTime; }
double transportBpm() { return doc::g_lib.composition.bpm; }
void transportSetBpm(double bpm) {
  if (bpm <= 0.0) return;                 // guard div-by-zero; the inspector clamps too
  doc::g_lib.composition.bpm = bpm;       // the persistence home (saved in the v2 file)
  g_transport.bpm = bpm;
}

}  // namespace sw::framecook
