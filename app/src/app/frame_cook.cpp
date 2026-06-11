#include "app/frame_cook.h"

#include <cstdint>
#include <map>
#include <string>

#include "app/audio_monitor.h"            // live spectrum snapshot (DSP fed by capture)
#include "app/document.h"                 // g_graph + graphRevision (mirror contract)
#include "runtime/audio_reaction.h"       // cookAudioReaction (TiXL AudioReaction parity)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph / Node
#include "runtime/graph_bridge.h"         // libFromGraph (production-swap mirror)
#include "runtime/point_graph.h"          // PointGraph::cookResident
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph
#include "runtime/spectrum_analyzer.h"    // SpectrumSnapshot

namespace sw::framecook {
namespace {

// App clock (was main's g_time/g_frameIndex — product behaviour, so it lives here).
uint32_t g_frameIndex = 0;
float g_time = 0.0f;

// PRODUCTION SWAP (compound spine): the frame cook walks this RESIDENT eval graph, not the
// flat g_graph. Rebuilt from g_graph whenever doc::graphRevision() changes (rebuild-on-edit;
// bridge paths == node ids, so per-path GPU buffers + stateful op state SURVIVE the rebuild).
// Incremental patch wiring (patch*/patchLib*) replaces the rebuild in a later cut — semantics
// already pinned by the patch goldens.
SymbolLibrary g_mirrorLib;
ResidentEvalGraph g_residentGraph;
uint64_t g_mirrorRev = 0;  // doc::graphRevision() the mirror was built from (0 = never)

}  // namespace

void run(PointGraph& pg, int viewTarget) {
  // Mirror rebuild BEFORE the AudioReaction cooker, so extOut lands on the fresh graph.
  if (g_mirrorRev != doc::graphRevision()) {
    g_mirrorLib = libFromGraph(doc::g_graph);
    g_residentGraph = buildEvalGraph(g_mirrorLib, "Root");
    g_mirrorRev = doc::graphRevision();
  }

  const float dt = 1.0f / 60.0f;
  const SpectrumSnapshot spec = audio_monitor::spectrum();

  // Cook every AudioReaction node (TiXL parity): gather its params, run the stateful
  // algorithm on the live spectrum, write its 3 outputs into outCache for evalFloat AND
  // onto the resident node's extOut — the resident cook's value wires read the LATTER
  // (evalResidentFloat's no-evaluate path).
  {
    static std::map<int, AudioReactionState> s_arState;
    for (Node& node : doc::g_graph.nodes) {
      if (node.type != "AudioReaction") continue;
      auto P = [&](const char* id, float def) {
        auto it = node.params.find(id);
        return it != node.params.end() ? it->second : def;
      };
      AudioReactionParams p;
      p.amplitude = P("Amplitude", 1.0f);
      p.inputBand = (int)(P("InputBand", 2.0f) + 0.5f);
      p.windowCenter = P("WindowCenter", 0.0f);
      p.windowWidth = P("WindowWidth", 1.0f);
      p.windowEdge = P("WindowEdge", 1.0f);
      p.threshold = P("Threshold", 0.5f);
      p.minTimeBetweenHits = P("MinTimeBetweenHits", 0.1f);
      p.output = (int)(P("Output", 3.0f) + 0.5f);
      p.bias = P("Bias", 1.0f);
      p.reset = P("Reset", 0.0f) > 0.5f;
      const AudioReactionOut o = cookAudioReaction(spec, p, g_time, s_arState[node.id]);
      node.outCache[0] = o.level;
      node.outCache[1] = o.wasHit ? 1.0f : 0.0f;
      node.outCache[2] = (float)o.hitCount;
      auto it = g_residentGraph.byPath.find(std::to_string(node.id));
      if (it != g_residentGraph.byPath.end()) {
        ResidentNode& rn = g_residentGraph.nodes[it->second];
        rn.extOut[0] = node.outCache[0];
        rn.extOut[1] = node.outCache[1];
        rn.extOut[2] = node.outCache[2];
      }
    }
  }

  ++g_frameIndex;
  g_time += dt;
  EvaluationContext ctx{};
  ctx.frameIndex = g_frameIndex;
  ctx.time = g_time;
  ctx.deltaTime = dt;

  // PRODUCTION SWAP: cook the RESIDENT graph (bridge path == node id as string). The flat
  // cook(g_graph,...) stays compiled for the goldens; --selftest-graphbridge pins the two
  // byte-identical on the real default graph + value/audio wires.
  pg.cookResident(g_residentGraph, ctx, /*reg=*/nullptr, std::to_string(viewTarget));
}

}  // namespace sw::framecook
