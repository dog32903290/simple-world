// ValuesToTexture tex op (Slice B — the FloatList CONSUMER + the tex-output fork). It is the
// rail-crossing: it reads N host FloatLists (the 5th cook flow currency) + scalar Float params and
// turns them into an R32Float, data-sized texture (the engine's FIRST non-RGBA8, non-resolution-
// pinned texture). Named point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS) picks it up.
//
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/process/ValuesToTexture.cs (ported
// VERBATIM below — every line ref is to that file):
//   - Values  = MultiInputSlot<List<float>>  (:145) → our "Values" FloatList MultiInput input port.
//   - Offset/Gain/Pow = InputSlot<float>     (:147-154).
//   - Direction = InputSlot<int> {Horizontal,Vertical} (:156-163) → Float Widget::Enum (0/1).
//   - useHorizontal = Direction == 0          (:22).
//   - drop null/empty lists; if no non-empty list → return (:31-40).
//   - sampleCount = MAX list length over all lists (:51-54); 0 → return (:56-57).
//   - pow guard: |pow| < 0.001 → return       (:62-63).
//   - per element: pow((list[i] + offset) * gain, pow); i >= list.Count → 0 (:84 / :95).
//   - Horizontal fill: row-major, one ROW per list (outer=list, inner=sample) (:78-87).
//   - Vertical fill: column-major, one COLUMN per list (outer=sample, inner=list) (:89-98).
//   - width  = useHorizontal ? sampleCount : listCount  (:101)
//     height = useHorizontal ? listCount   : sampleCount (:102).
//   - format = Format.R32_Float, bytesPerTexel = sizeof(float) = 4 (:120,:127).
//
// ★TEX-OUTPUT FORK (named loudly): ValuesToTexture does NOT use the tex-walker's ensureTex output
//   (RGBA8Unorm, resolution-pinned to the window — point_graph_internal.h kPointTargetFormat:59,
//   ensureTex:155). TiXL allocates its OWN Texture2DDescription (ValuesToTexture.cs:104-123) sized to
//   the DATA, format R32_Float, bypassing the render sizing. We mirror this: the op is marked
//   registerTexOpOwnsOutput, so the cook driver hands it ownTexHost/ownTexW/ownTexH (NO ensureTex),
//   the op computes dims + writes the host float buffer, and the DRIVER allocates the op-owned
//   R32Float texture via Impl::ensureOwnedTex (parked in texBuf → released on realloc + in
//   ~PointGraph → NO per-cook leak). This is ADDITIVE (every other tex op is byte-identical), FORCED
//   by TiXL parity (not a taste call, not 柏為's domain), and the FIRST non-RGBA8, non-resolution-
//   pinned texture in the engine.
#include <cmath>
#include <vector>

#include "runtime/image_filter_op_registry.h"  // ImageFilterOp (spec+selftest+registerTexOp sinks)
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOpOwnsOutput

namespace sw {

int runValuesToTextureSelfTest(bool injectBug);
// Test-only injection seam (the chain golden's RED case corrupts the REAL cook path, not the
// expected value): when set, the op writes 0 into a NON-missing cell so the readback diverges.
bool& valuesToTextureInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// cookValuesToTexture: read inputLists (the N gathered FloatLists) + Offset/Gain/Pow/Direction, apply
// the TiXL transform, compute dims, write *ownTexHost (the DRIVER uploads it to an R32Float texture).
void cookValuesToTexture(TexCookCtx& c) {
  if (!c.ownTexHost || !c.ownTexW || !c.ownTexH) return;
  *c.ownTexW = 0;
  *c.ownTexH = 0;
  c.ownTexHost->clear();

  // Collect the non-empty input lists (ValuesToTexture.cs:31-40 drops null/empty). The driver already
  // expanded the Values MultiInput into one entry per wired source, in wire-declaration order.
  std::vector<const std::vector<float>*> lists;
  if (c.inputLists)
    for (const std::vector<float>& v : *c.inputLists)
      if (!v.empty()) lists.push_back(&v);
  const int listCount = (int)lists.size();
  if (listCount == 0) return;  // :39-40 (no non-empty list → no texture)

  // sampleCount = the LONGEST list's length (:51-54).
  int sampleCount = 0;
  for (const std::vector<float>* l : lists)
    sampleCount = std::max(sampleCount, (int)l->size());
  if (sampleCount == 0) return;  // :56-57

  const float offset = cookParam(c, "Offset", 0.0f);   // :60
  const float gain = cookParam(c, "Gain", 1.0f);       // :59
  const float pow = cookParam(c, "Pow", 1.0f);         // :61
  if (std::fabs(pow) < 0.001f) return;                 // :62-63 (pow guard)
  const bool useHorizontal = cookParam(c, "Direction", 0.0f) < 0.5f;  // :22 (0 == Horizontal)

  // Per-element transform, faithful to :84 / :95. Missing (i >= list.size()) → 0.
  auto cell = [&](const std::vector<float>* list, int i) -> float {
    if (i >= (int)list->size()) return 0.0f;
    return (float)std::pow(((*list)[i] + offset) * gain, pow);
  };

  std::vector<float>& out = *c.ownTexHost;
  out.reserve((size_t)listCount * sampleCount);
  if (useHorizontal) {
    // Row-major: one ROW per list (:78-87) — outer=list, inner=sample.
    for (const std::vector<float>* l : lists)
      for (int i = 0; i < sampleCount; ++i)
        out.push_back(cell(l, i));
  } else {
    // Column-major: one COLUMN per list (:89-98) — outer=sample, inner=list.
    for (int i = 0; i < sampleCount; ++i)
      for (const std::vector<float>* l : lists)
        out.push_back(cell(l, i));
  }

  *c.ownTexW = useHorizontal ? (uint32_t)sampleCount : (uint32_t)listCount;  // :101
  *c.ownTexH = useHorizontal ? (uint32_t)listCount : (uint32_t)sampleCount;  // :102

  // Test-only: corrupt the REAL cook output (write 0 into cell[0], a non-missing cell with a non-zero
  // expected value in the golden) so the RED case bites here, not by flipping the expected value.
  if (valuesToTextureInjectBug() && !out.empty()) out[0] = 0.0f;
}

}  // namespace

// Self-registration. ImageFilterOp feeds: registerTexOp("ValuesToTexture", cook) + the spec sink (so
// node_registry's findSpec sees it) + the selftest sink (run_all discovers --selftest-valuestotexture).
// It is NOT an ImageFilterComputeOp → no sizeFn / no needsWrite (those are Texture2D-filter concerns).
// We additionally mark it OWN-TEXTURE so the tex-walker routes it through the ownTexHost/R32Float path.
// Ports: Values = FloatList MultiInput (the gathered lists); Offset/Gain/Pow scalars;
//        Direction = Float Widget::Enum {Horizontal,Vertical}; out = Texture2D output.
static const ImageFilterOp _reg_valuestotexture{
    {"ValuesToTexture", "ValuesToTexture",
     {{"Values", "Values", "FloatList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Texture2D", false},
      {"Offset", "Offset", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Gain", "Gain", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Pow", "Pow", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"Horizontal", "Vertical"}, true}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "ValuesToTexture", cookValuesToTexture, "valuestotexture", runValuesToTextureSelfTest};

// Mark OWN-TEXTURE at static-init (mirrors how ImageFilterOp's ctor calls registerTexOp at static
// init). A tiny file-scope registrar — registerTexOpOwnsOutput is idempotent (set insert).
namespace {
struct OwnTexRegistrar {
  OwnTexRegistrar() { registerTexOpOwnsOutput("ValuesToTexture"); }
};
static const OwnTexRegistrar _reg_valuestotexture_owns;
}  // namespace

}  // namespace sw
