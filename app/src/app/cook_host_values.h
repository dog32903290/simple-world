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
void cookHostValueNodes(ResidentEvalGraph& g, float posBars, float fxBars, const SymbolLibrary* lib);

}  // namespace sw::framecook
