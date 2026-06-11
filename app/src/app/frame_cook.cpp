#include "app/frame_cook.h"

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

namespace sw::framecook {
namespace {

// App clock (was main's g_time/g_frameIndex — product behaviour, so it lives here).
uint32_t g_frameIndex = 0;
float g_time = 0.0f;

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

void run(PointGraph& pg, int viewTarget) {
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

  const float dt = 1.0f / 60.0f;
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
    rctx.localTime = g_time;
    rctx.localFxTime = g_time;
    rctx.frameIndex = g_frameIndex;
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
      const AudioReactionOut o = cookAudioReaction(spec, p, g_time, s_arState[rn.path]);
      rn.extOut[0] = o.level;
      rn.extOut[1] = o.wasHit ? 1.0f : 0.0f;
      rn.extOut[2] = (float)o.hitCount;
    }
  }

  ++g_frameIndex;
  g_time += dt;
  EvaluationContext ctx{};
  ctx.frameIndex = g_frameIndex;
  ctx.time = g_time;
  ctx.deltaTime = dt;

  // Cook the RESIDENT graph at the viewed child's path. The flat cook(g_graph,...) stays
  // compiled for the goldens; --selftest-graphbridge pins the two byte-identical on the
  // real default graph + value/audio wires.
  pg.cookResident(g_residentGraph, ctx, /*reg=*/nullptr, doc::residentPathFor(viewTarget));
}

}  // namespace sw::framecook
