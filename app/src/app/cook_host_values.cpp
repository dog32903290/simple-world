#include "app/cook_host_values.h"

#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (s_colorListState — KeepColors's cross-frame accumulator store)

#include "runtime/bpm_provider.h"          // BpmProvider (the [SetBpm] triggered-pull singleton)
#include "runtime/compound_graph.h"        // CompositionSettings (the comp.bpm sink)
#include "runtime/point_graph.h"           // PointGraph::residentCookedPoints (point-into-frame accessor)
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentEvalCtx + cook*Nodes prototypes
#include "runtime/string_op_registry.h"   // StringState (the stateful-string seam's cross-frame store)
#include "runtime/tixl_point.h"            // SwPoint (PointAccessor element type)

namespace sw::framecook {

// Cook the host-value currencies (String / host-scalar / ColorList) for one frame — the body lifted
// verbatim from frame_cook.cpp's run() so that file stays under its line-count cap (the inline block
// is now this one call). Byte-identical behaviour; the cross-frame state statics moved in with it.
bool cookHostValueNodes(ResidentEvalGraph& g, float posBars, float fxBars, SymbolLibrary* lib,
                        uint32_t reqW, uint32_t reqH) {
  ResidentEvalCtx hsCtx;
  hsCtx.localTime = posBars;     // playhead (bars) — automation-driven list params sample this
  hsCtx.localFxTime = fxBars;    // wall clock (bars)
  hsCtx.lib = lib;               // Automation drivers on list-param inputs resolve through this
  hsCtx.requestedWidth = reqW;   // value-output-rail Phase 1: frame-level window resolution for the
  hsCtx.requestedHeight = reqH;  // RequestedResolution cook-emit (cookValueOutputNodes below)

  // Cook the STRING currency ops (FloatToString / IntToString / Vec3ToString / CombineStrings) — the
  // PRODUCTION leg of the host-string cook flow (resident string-wire rail, task_32b5b6e5). Walks the
  // resident graph, gathers each string op's upstream String inputs THROUGH the resident Connection
  // drivers (the wire the flatten step projects onto String slots — was DROPPED before this rail), and
  // writes the host string onto extStrOut so a downstream resident String consumer reads the REAL
  // production string (NOT flat-only — the R-2 rule; resident_string_cook.cpp). Cooked BEFORE the
  // host-scalar pass so StringLength (which recurses cookResidentString inline anyway) reads producers
  // already settled — same producer-before-consumer cleanliness as colorlist→host-scalar.
  //
  // s_stringState = the per-node CROSS-FRAME STRING accumulator store (HasStringChanged's `_lastString`),
  // keyed by resident path — function-local static, the EXACT mirror of s_colorListState / s_svState. It
  // survives between frames so HasStringChanged compares against the PREVIOUS frame's string on the
  // PRODUCTION resident path; a stateless string op never touches its slot (so this static stays empty for
  // a graph without a stateful string op).
  static std::map<std::string, StringState> s_stringState;
  cookStringNodes(g, hsCtx, &s_stringState);

  // Cook the FloatList→Float BRIDGE host-scalar ops (FloatListLength / PickFloatFromList / StringLength)
  // — the PRODUCTION leg of the list-routing seam. Same once-per-frame extOut-mirror slot: walks the
  // resident graph, gathers each host-scalar op's upstream FloatList/String inputs through the resident
  // Connection drivers, and writes the scalar onto extOut[0] so the subsequent cookResident's
  // evalResidentFloat reads the bridged value (was 0 — flat-only — before this).
  cookHostScalarNodes(g, hsCtx);

  // Cook the COLORLIST currency ops (ColorsToList / the stateful KeepColors) — the PRODUCTION leg of the
  // vec4-list cook flow. Same once-per-frame slot: walks the resident graph, gathers each colorlist op's
  // upstream component scalars through the resident Connection drivers, and writes the host color list
  // onto extColorOut so a downstream resident colorlist consumer reads the real production list (NOT
  // flat-only — the R-2 rule; resident_colorlist_cook.cpp). Reuses hsCtx (same resident eval clocks).
  // s_colorListState = the per-node CROSS-FRAME accumulator store (KeepColors's `_list`), keyed by
  // resident path — function-local static, the EXACT mirror of s_svState/s_arState. It survives between
  // frames so KeepColors accumulates on the PRODUCTION path; a stateless colorlist op never touches its
  // slot (so this static stays empty for a graph without KeepColors).
  static std::map<std::string, std::vector<simd::float4>> s_colorListState;
  cookColorListNodes(g, hsCtx, s_colorListState);

  // Cook the VALUE-OUTPUT rail ops (value-output-rail Phase 1: RequestedResolution) — the cook-emit
  // family whose value comes from the COOK CONTEXT (ctx.requestedWidth/Height), not from inputs. Same
  // once-per-frame extOut-mirror slot: walks the resident graph and writes each op's outputs onto
  // extOut[0..N-1] so a downstream resident Float consumer reads the real frame resolution. Stateless
  // (no cross-frame store) — purely a function of the ctx fields seeded above.
  cookValueOutputNodes(g, hsCtx);

  // Cook the MATRIX value-output rail (value-output-rail Phase 3: TransformMatrix) — a 4×4 matrix = a
  // 4-element Vector4[], emitted onto the EXISTING extColorOut vec4 channel (resident_matrix_output_cook).
  // Same once-per-frame slot family as cookColorListNodes; stateless (a pure function of the resolved SRT
  // Float inputs). PointToMatrix's emit is deferred (needs a point into this frame-level pass).
  cookMatrixOutputNodes(g, hsCtx);

  // [SetBpm] triggered-pull (PlaybackUtils.cs:74-78), folded in after the host-value cook so frame_cook
  // stays under its line-count cap. Returns whether comp.bpm changed → the caller bumps the transport.
  return lib ? pullSetBpmRate(lib->composition) : false;
}

// value-output-rail Phase 4 — the point-into-frame emit, run AFTER pg.cookResident (frame_cook.cpp), when
// this frame's point buffers EXIST. Builds the PointAccessor over the live PointGraph (residentCookedPoints
// reads the cooked Shared buffer host-side, zero blit) and runs cookPointValueOutputNodes (PointToMatrix /
// GetPointDataFromList). SEPARATE from cookHostValueNodes (which runs BEFORE the point cook) precisely
// because these ops need a point INTO this frame. Additive — reads finished buffers; no cook-core touch.
void cookPointValueFromGraph(ResidentEvalGraph& g, PointGraph& pg, float posBars, float fxBars,
                             SymbolLibrary* lib) {
  ResidentEvalCtx ctx;
  ctx.localTime = posBars;   // playhead (bars) — a WIRED ItemIndex automation samples this
  ctx.localFxTime = fxBars;  // wall clock (bars)
  ctx.lib = lib;             // Automation drivers on ItemIndex resolve through this
  PointAccessor acc = [&pg](const std::string& srcNodePath, uint32_t& outCount) -> const SwPoint* {
    return pg.residentCookedPoints(srcNodePath, outCount);
  };
  cookPointValueOutputNodes(g, ctx, acc);
}

// = TiXL PlaybackUtils.cs:74-78 (the per-frame [SetBpm] consumer). The triggered PULL: returns false
// + leaves comp.bpm UNCHANGED on every non-armed frame (the BpmProvider.TryGetNewBpmRate false leg —
// the make-or-break: NOT a per-frame overwrite); on the ONE frame after a SetBpm edge it returns the
// clamped rate ONCE (clear-on-read) and writes comp.bpm. comp.bpm is the settings persistence home
// (= settings.Playback.Bpm); frame_cook re-pulls comp.bpm onto g_transport.bpm at the top of each
// frame, so the written rate flows comp→transport on the next cook (PlaybackUtils.cs:80 analog).
bool pullSetBpmRate(CompositionSettings& comp) {
  float newBpm;
  if (!BpmProvider::instance().tryGetNewBpmRate(newBpm)) return false;  // not armed → comp.bpm untouched
  comp.bpm = (double)newBpm;  // settings.Playback.Bpm = newBpmRate (cs:77)
  return true;
}

}  // namespace sw::framecook
