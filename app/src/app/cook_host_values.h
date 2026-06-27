// app/cook_host_values — the per-frame PRODUCTION cook of the HOST-VALUE currencies (String /
// host-scalar / ColorList), extracted out of frame_cook.cpp's run() so that file stays at-or-below
// its line-count cap (ARCHITECTURE.md rule 4 ratchet) while the stateful-string seam threads a new
// cross-frame state store through it. Behaviour is byte-identical to the inline block it replaced —
// it just moved the `{ ResidentEvalCtx hsCtx; cookStringNodes/cookHostScalarNodes/cookColorListNodes }`
// body (frame_cook.cpp) into one named helper, carrying its function-local cross-frame state statics
// (s_stringState / s_colorListState) inside.
//
// Zone: app (the production per-frame driver). Depends on runtime only.
#pragma once
#include <cstdint>

namespace sw {
struct ResidentEvalGraph;
struct SymbolLibrary;
struct CompositionSettings;
class PointGraph;  // point_graph.h (PointToMatrix/GetPointDataFromList point-into-frame emit)
}

namespace sw::framecook {

// Cook the host-value currencies for one frame, in the SAME once-per-frame slot as the AudioReaction /
// stateful-value cookers: build the resident eval ctx from the two BARS clocks, then run
//   cookStringNodes      (String currency — incl. the cross-frame s_stringState accumulator store),
//   cookHostScalarNodes  (FloatList→Float bridge: FloatListLength / PickFloatFromList / StringLength),
//   cookColorListNodes   (ColorList currency — incl. the cross-frame s_colorListState store).
// Producers settle before consumers (String before host-scalar, same as the old inline order). The
// cross-frame state stores are function-local statics OWNED by this helper (the EXACT mirror of the
// s_svState/s_arState pattern), so they survive frame→frame and never leak into a runtime global.
//
//   posBars = the PLAYHEAD (bars; automation-driven list params sample this).
//   fxBars  = the WALL CLOCK (bars).
//   lib     = the symbol library (Automation drivers on list-param inputs resolve through this).
//   reqW/reqH = the frame-level requested resolution (window size; PointGraph::windowResolution()).
//               Threaded onto ResidentEvalCtx so cookValueOutputNodes can emit RequestedResolution's
//               Width/Height/AspectRatio (value-output-rail Phase 1). 0/0 = unseeded → emits 0.
//
// It ALSO runs the per-frame [SetBpm] triggered-pull (= TiXL PlaybackUtils.cs:74-78): after the host-
// value cook, pull the BpmProvider singleton; on the ONE armed frame after a SetBpm edge it writes
// lib->composition.bpm (the settings home = settings.Playback.Bpm) and RETURNS TRUE; every non-armed
// frame leaves comp.bpm UNCHANGED and returns false (the triggered-pull, NOT a per-frame overwrite).
// The caller bumps g_transport.bpm + dirties the lib on a true return (cs:80 settings→Playback.Bpm).
// Folded in here (not a separate frame_cook call) so frame_cook.cpp stays at-or-below its line-count
// cap (ARCHITECTURE rule 4 ratchet — NO grandfather bump). lib must be non-null for the pull to write.
bool cookHostValueNodes(ResidentEvalGraph& g, float posBars, float fxBars, SymbolLibrary* lib,
                        uint32_t reqW = 0, uint32_t reqH = 0);

// value-output-rail Phase 4 — the POINT-INTO-FRAME value emit, run AFTER pg.cookResident (when this
// frame's point buffers exist). Builds the PointAccessor over `pg` (residentCookedPoints, zero-blit Shared
// read) and runs cookPointValueOutputNodes (PointToMatrix / GetPointDataFromList). SEPARATE from
// cookHostValueNodes (which runs BEFORE the point cook) — these ops need a point INTO this frame. Additive:
// reads finished buffers, no cook-core touch. One frame_cook call, after the resident cook. posBars/fxBars
// /lib build the resident eval ctx so a WIRED ItemIndex (GetPointDataFromList) resolves at the playhead.
void cookPointValueFromGraph(ResidentEvalGraph& g, PointGraph& pg, float posBars, float fxBars,
                             SymbolLibrary* lib);

// Per-frame [SetBpm] consumer (exposed for the --selftest-setbpm golden): the triggered PULL of the
// BpmProvider singleton onto the composition BPM. Returns true iff it wrote comp.bpm this call.
bool pullSetBpmRate(CompositionSettings& comp);

}  // namespace sw::framecook
