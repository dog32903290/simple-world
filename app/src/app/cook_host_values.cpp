#include "app/cook_host_values.h"

#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (s_colorListState — KeepColors's cross-frame accumulator store)

#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentEvalCtx + cook*Nodes prototypes

namespace sw::framecook {

// Cook the host-value currencies (String / host-scalar / ColorList) for one frame — the body lifted
// verbatim from frame_cook.cpp's run() so that file stays under its line-count cap (the inline block
// is now this one call). Byte-identical behaviour; the cross-frame state statics moved in with it.
void cookHostValueNodes(ResidentEvalGraph& g, float posBars, float fxBars, const SymbolLibrary* lib) {
  ResidentEvalCtx hsCtx;
  hsCtx.localTime = posBars;     // playhead (bars) — automation-driven list params sample this
  hsCtx.localFxTime = fxBars;    // wall clock (bars)
  hsCtx.lib = lib;               // Automation drivers on list-param inputs resolve through this

  // Cook the STRING currency ops (FloatToString / IntToString / Vec3ToString / CombineStrings) — the
  // PRODUCTION leg of the host-string cook flow (resident string-wire rail, task_32b5b6e5). Walks the
  // resident graph, gathers each string op's upstream String inputs THROUGH the resident Connection
  // drivers (the wire the flatten step projects onto String slots — was DROPPED before this rail), and
  // writes the host string onto extStrOut so a downstream resident String consumer reads the REAL
  // production string (NOT flat-only — the R-2 rule; resident_string_cook.cpp). Cooked BEFORE the
  // host-scalar pass so StringLength (which recurses cookResidentString inline anyway) reads producers
  // already settled — same producer-before-consumer cleanliness as colorlist→host-scalar.
  cookStringNodes(g, hsCtx);

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
}

}  // namespace sw::framecook
