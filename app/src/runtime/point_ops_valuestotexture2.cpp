// ValuesToTexture2 tex op — the SECOND FloatList CONSUMER on the cpu-upload own-texture rail, which
// this op COMPLETES to 4/4: ValuesToTexture[FloatList·gain/offset/pow] + GradientsToTexture[Gradient] +
// CurvesToTexture[Curve] + ValuesToTexture2[FloatList·input-range/output-range/gain-bias remap]. It reads
// N host FloatLists + scalar/Vec2 params and turns them into a data-sized R32_Float texture (1 float per
// texel = the remapped value). Named point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS) picks it up —
// identical to ValuesToTexture / GradientsToTexture / CurvesToTexture.
//
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/process/ValuesToTexture2.cs (ported VERBATIM
// below — every line ref is to that file) + ValuesToTexture2.t3 default mirror:
//   - Values      = MultiInputSlot<List<float>> (:179)         → our "Values" FloatList MultiInput port.
//   - InputRange  = InputSlot<Vector2>          (:182)  .t3 (0,1) → Vec2 head (InputRange.x/.y).
//   - Clamp       = InputSlot<bool>             (:185)  .t3 false → Float Widget::Bool param.
//   - GainAndBias = InputSlot<Vector2>          (:188)  .t3 (0.5,0.5) → Vec2 head (GainAndBias.x/.y).
//   - OutputRange = InputSlot<Vector2>          (:191)  .t3 (0,1) → Vec2 head (OutputRange.x/.y).
//   - Direction   = InputSlot<int> {Horizontal,Vertical} (:193-200) .t3 0 → Float Widget::Enum.
//   - useHorizontal = Direction == 0            (:31).
//   - drop null/empty lists; if no non-empty list → return (:39-55).
//   - sampleCount = MAX list length over all lists (:66-68); 0 → return (:70-71).
//   - per element (NormalizeAndMapValue, :157-168):
//        orgValue   = i < list.Count ? list[i] : float.NaN        (:159)  ★NB the short-cell is NaN,
//                                                                          NOT 0 (≠ ValuesToTexture's 0!)
//        normalized = (orgValue - InputRange.X) / (InputRange.Y - InputRange.X)   (:160)
//        if Clamp: normalized = normalized.Clamp(0,1).ApplyGainAndBias(GainAndBias.X, GainAndBias.Y) (:163)
//        v          = normalized * (OutputRange.Y - OutputRange.X) + OutputRange.X (:165)
//   - Horizontal fill: row-major, one ROW per list (outer=list, inner=sample) (:92-101).
//   - Vertical   fill: column-major, one COLUMN per list (outer=sample, inner=list) (:103-119).
//   - width  = useHorizontal ? sampleCount : listCount (:121).
//     height = useHorizontal ? listCount   : sampleCount(:122).
//   - format = Format.R32_Float, bytesPerTexel = sizeof(float) = 4 (:140,:147).
//
// ★HOW ValuesToTexture2 DIFFERS FROM ValuesToTexture (cite both .cs):
//   - TRANSFORM: VT1 = pow((list[i] + Offset) * Gain, Pow) with a pow-guard early-return (VT1.cs:62-63,84).
//     VT2 = an input-range→output-range REMAP with optional Clamp + ApplyGainAndBias gain/bias shaping
//     (VT2.cs:157-168). VT2 has NO pow-guard / NO scalar early-return — every (non-empty-list) cook emits.
//   - SHORT CELL: VT1 fills missing cells (i >= list.Count) with 0 (VT1.cs:84/:95). VT2 fills them with
//     float.NaN (VT2.cs:159) → those texels carry NaN. (We mirror this exactly; the golden uses
//     EQUAL-length lists so every texel is finite + hand-checkable, and pins the short-cell NaN
//     separately so the divergence from VT1 is proven, not assumed.)
//   - PARAMS: VT1 = 3 scalar Floats (Offset/Gain/Pow). VT2 = 3 Vector2 (InputRange/OutputRange/GainAndBias)
//     + 1 bool (Clamp). Same Values MultiInput + Direction enum + R32_Float own-texture rail.
//
// ★ApplyGainAndBias (MathUtils.cs:65-88, only applied when Clamp): b=bias.Clamp(0,1), g=gain.Clamp(0,1);
//   value>0.999→1; value<0.00001→0; else g<0.5 ? GetBias(b)∘GetSchlickBias(g) : GetSchlickBias(g)∘GetBias(b).
//   GetBias(b,x)=x/((1/b-2)(1-x)+1) (:44-47); GetSchlickBias(g,x) splits at 0.5 (:49-63). NOTE the .t3
//   default GainAndBias=(0.5,0.5) makes ApplyGainAndBias the IDENTITY (g=0.5 ⇒ GetSchlickBias(0.5)=id and
//   GetBias(0.5)=id since 1/0.5-2=0); the golden's clamp leg uses a NON-default g<0.5 to exercise the math.
//
// ★TEX-OUTPUT FORK (named, same as the 3 siblings): ValuesToTexture2 allocates its OWN Texture2DDescription
//   (VT2.cs:131-143) sized to the DATA, format R32_Float — NOT the tex-walker's RGBA8/resolution-pinned
//   ensureTex output. We mirror this: marked registerTexOpOwnsOutput + registerTexOpOwnFormat(...,1), so the
//   cook driver hands ownTexHost/ownTexW/ownTexH and allocates the op-owned R32_Float texture via
//   ensureOwnedTex (parked in texBuf → released on realloc + in ~PointGraph → no per-cook leak). ADDITIVE;
//   FORCED by TiXL parity (not a taste call).
//
// ★RESIDENT OWN-TEX SEAM (FloatList currency LANDED on the production cookResident path — R-2): the prior
//   resident own-tex gate fired only for Gradient/Curve currency (FloatList was excluded — ValuesToTexture
//   was flat-only). This op's landing EXPORTS cookResidentFloatList (resident_eval_graph.h, defined in
//   resident_host_scalar_cook.cpp) and broadens the gate (point_graph_resident.cpp) to gather the FloatList
//   currency too, so BOTH ValuesToTexture and ValuesToTexture2 now ride cookResident with REAL wired
//   FloatsToList producers (the resident golden leg below proves it — NOT flat-only self-deception).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"              // EvaluationContext
#include "runtime/graph.h"                     // Graph/Node/Connection/pinId (goldens)
#include "runtime/graph_bridge.h"              // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp (spec+selftest+registerTexOp sinks)
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, cookVecN, registerTexOpOwns*
#include "runtime/resident_eval_graph.h"       // ResidentEvalGraph / buildEvalGraph

namespace sw {

int runValuesToTexture2SelfTest(bool injectBug);
// Test-only injection seam (the golden's RED case corrupts the REAL cook output, NOT the expected value):
// when set, the op writes a sentinel (-1) into texel(0,0) so the readback diverges regardless of the true
// value at that texel. Off in production.
bool& valuesToTexture2InjectBug() {
  static bool b = false;
  return b;
}

namespace {

// MathUtils.cs:44-47 GetBias.
inline float getBias(float b, float x) { return x / (((1.0f / b - 2.0f) * (1.0f - x)) + 1.0f); }
// MathUtils.cs:49-63 GetSchlickBias.
inline float getSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * getBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * getBias(1.0f - g, x) + 0.5f;
  }
  return x;
}
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
// MathUtils.cs:65-88 ApplyGainAndBias (this float value, float gain, float bias).
inline float applyGainAndBias(float value, float gain, float bias) {
  float b = clamp01(bias);
  float g = clamp01(gain);
  if (value > 0.999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = getBias(b, value);
    value = getSchlickBias(g, value);
  } else {
    value = getSchlickBias(g, value);
    value = getBias(b, value);
  }
  return value;
}

// cookValuesToTexture2: read inputLists (the N gathered FloatLists) + InputRange/OutputRange/GainAndBias
// (Vec2) + Clamp (bool) + Direction, apply the TiXL remap (VT2.cs:157-168), compute dims, write
// *ownTexHost as 1 float/texel (R32_Float). The driver uploads it to an R32_Float texture.
void cookValuesToTexture2(TexCookCtx& c) {
  if (!c.ownTexHost || !c.ownTexW || !c.ownTexH) return;
  *c.ownTexW = 0;
  *c.ownTexH = 0;
  c.ownTexHost->clear();

  // Collect non-empty input lists (VT2.cs:39-55 drops null/empty). The driver expanded the Values
  // MultiInput into one entry per wired source, in wire-declaration order.
  std::vector<const std::vector<float>*> lists;
  if (c.inputLists)
    for (const std::vector<float>& v : *c.inputLists)
      if (!v.empty()) lists.push_back(&v);
  const int listCount = (int)lists.size();
  if (listCount == 0) return;  // :42-43 / :53-54 (no non-empty list → no texture)

  // sampleCount = the LONGEST list's length (:66-68).
  int sampleCount = 0;
  for (const std::vector<float>* l : lists) sampleCount = std::max(sampleCount, (int)l->size());
  if (sampleCount == 0) return;  // :70-71

  float inR[2] = {0.0f, 1.0f};
  float outR[2] = {0.0f, 1.0f};
  float gb[2] = {0.5f, 0.5f};
  cookVecN(c, "InputRange", inR, 2, inR);     // :33 / .t3 (0,1)
  cookVecN(c, "OutputRange", outR, 2, outR);  // :34 / .t3 (0,1)
  cookVecN(c, "GainAndBias", gb, 2, gb);      // :35 / .t3 (0.5,0.5)
  const bool clamp = cookParam(c, "Clamp", 0.0f) >= 0.5f;             // :36 / .t3 false
  const bool useHorizontal = cookParam(c, "Direction", 0.0f) < 0.5f;  // :31 (0 == Horizontal)

  // NormalizeAndMapValue (:157-168). Missing cell (i >= list.Count) → NaN (:159) — ★the VT2-vs-VT1
  // divergence (VT1 uses 0). normalized=(v-inMin)/(inMax-inMin); if Clamp → clamp01.ApplyGainAndBias;
  // v = normalized*(outMax-outMin)+outMin.
  auto cell = [&](const std::vector<float>* list, int i) -> float {
    float org = i < (int)list->size() ? (*list)[i] : std::numeric_limits<float>::quiet_NaN();  // :159
    float normalized = (org - inR[0]) / (inR[1] - inR[0]);                                      // :160
    if (clamp) normalized = applyGainAndBias(clamp01(normalized), gb[0], gb[1]);                // :163
    return normalized * (outR[1] - outR[0]) + outR[0];                                          // :165
  };

  std::vector<float>& out = *c.ownTexHost;
  out.reserve((size_t)listCount * sampleCount);
  if (useHorizontal) {
    // Row-major: one ROW per list (:92-101) — outer=list, inner=sample.
    for (const std::vector<float>* l : lists)
      for (int i = 0; i < sampleCount; ++i) out.push_back(cell(l, i));
  } else {
    // Column-major: one COLUMN per list (:103-119) — outer=sample, inner=list.
    for (int i = 0; i < sampleCount; ++i)
      for (const std::vector<float>* l : lists) out.push_back(cell(l, i));
  }

  *c.ownTexW = useHorizontal ? (uint32_t)sampleCount : (uint32_t)listCount;  // :121
  *c.ownTexH = useHorizontal ? (uint32_t)listCount : (uint32_t)sampleCount;  // :122

  // Test-only: corrupt the REAL cook output — sentinel (-1) into texel(0,0) so the RED case bites here
  // regardless of the expected value at that texel. Off in production.
  if (valuesToTexture2InjectBug() && !out.empty()) out[0] = -1.0f;
}

}  // namespace

// Self-registration. ImageFilterOp feeds registerTexOp + the spec sink (findSpec) + the selftest sink
// (--selftest-valuestotexture2). Marked OWN-TEXTURE (R32_Float, 1 float/texel) so the tex-walker routes
// it through the ownTexHost path with the op-chosen format.
//   Ports: Values = FloatList MultiInput; out = Texture2D; InputRange/OutputRange/GainAndBias = Vec2 heads
//          (component .x carries Widget::Vec + vecArity=2, .y is a plain Float); Clamp = Bool; Direction =
//          Float Widget::Enum {Horizontal,Vertical}. Defaults mirror ValuesToTexture2.t3.
static const ImageFilterOp _reg_valuestotexture2{
    {"ValuesToTexture2", "ValuesToTexture2",
     {{"Values", "Values", "FloatList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Texture2D", false},
      {"InputRange.x", "InputRange", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"InputRange.y", "InputRange.y", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"OutputRange.x", "OutputRange", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"OutputRange.y", "OutputRange.y", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, false, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Slider},
      {"Clamp", "Clamp", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"Horizontal", "Vertical"}, true}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "ValuesToTexture2", cookValuesToTexture2, "valuestotexture2", runValuesToTexture2SelfTest};

// Mark OWN-TEXTURE + its format (R32_Float, 1 float/texel) at static-init (mirrors the siblings).
// registerTexOpOwnsOutput + registerTexOpOwnFormat are idempotent.
namespace {
struct OwnTexRegistrar {
  OwnTexRegistrar() {
    registerTexOpOwnsOutput("ValuesToTexture2");
    registerTexOpOwnFormat("ValuesToTexture2", /*floatsPerTexel=*/1);  // R32_Float
  }
};
static const OwnTexRegistrar _reg_valuestotexture2_owns;
}  // namespace

// ===================== --selftest-valuestotexture2 (golden) ===========================
// Three legs:
//   (1) FLAT CLOSED-FORM: cook ValuesToTexture2 directly with hand-built inputLists + read ownTexHost.
//       Asserts each texel == the HAND-COMPUTED remap. Three sub-cases: (a) the DEFAULT-param identity
//       (InputRange=(0,1), OutputRange=(0,1), Clamp=false ⇒ out == in), (b) a NON-trivial remap
//       (InputRange=(0,2)→OutputRange=(0,10), Clamp=false ⇒ out = 5·in), (c) the Clamp ON path (exercises
//       clamp01 + ApplyGainAndBias g<0.5). Also pins the VT2-vs-VT1 short-cell NaN.
//   (2) RESIDENT (production) PATH ★R-2: build FloatsToList producers wired into ValuesToTexture2.Values,
//       cook via PointGraph::cookResident, read back the OP-OWNED R32_Float texture, assert each texel ==
//       the closed-form remap. Proves the FloatList own-tex cook is LIVE on the production cookResident
//       path (the resident own-tex gate was broadened to fire for the FloatList currency).
//   (3) RED: valuesToTexture2InjectBug() corrupts the REAL flat cook output (texel(0,0) → -1) so the
//       closed-form assert diverges (want FIXED at the true value, NOT flipped with the bug).
#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace {

bool vt2Nearf(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// Run cookValuesToTexture2 flat over `lists` with the given params (Horizontal), return host float buffer
// (R32_Float, 1 float/texel; row-major: list r, sample i → out[r*sampleCount + i]).
bool flatCook2(const std::vector<std::vector<float>>& lists, float inMin, float inMax, float outMin,
               float outMax, float gain, float bias, bool clamp, std::vector<float>& out, uint32_t& w,
               uint32_t& h, bool injectBug) {
  out.clear(); w = 0; h = 0;
  std::map<std::string, float> params;
  params["InputRange.x"] = inMin; params["InputRange.y"] = inMax;
  params["OutputRange.x"] = outMin; params["OutputRange.y"] = outMax;
  params["GainAndBias.x"] = gain; params["GainAndBias.y"] = bias;
  params["Clamp"] = clamp ? 1.0f : 0.0f;
  params["Direction"] = 0.0f;  // Horizontal
  TexCookCtx tc;
  tc.inputLists = &lists;
  tc.ownTexHost = &out; tc.ownTexW = &w; tc.ownTexH = &h;
  tc.params = &params;
  valuesToTexture2InjectBug() = injectBug;
  cookValuesToTexture2(tc);
  valuesToTexture2InjectBug() = false;
  return w > 0 && h > 0;
}

// The reference remap (mirror of cookValuesToTexture2's cell), used to cross-check the resident leg.
float remapRef(float v, float inMin, float inMax, float outMin, float outMax, float gain, float bias,
               bool clamp) {
  float n = (v - inMin) / (inMax - inMin);
  if (clamp) n = applyGainAndBias(clamp01(n), gain, bias);
  return n * (outMax - outMin) + outMin;
}

}  // namespace

int runValuesToTexture2SelfTest(bool injectBug) {
  bool ok = true;
  auto fail = [&](const char* msg) { std::printf("[selftest-valuestotexture2] %s FAIL\n", msg); ok = false; };

  // ---------- LEG 1: FLAT CLOSED-FORM ----------
  // Equal-length lists (so every texel is finite + hand-checkable). row0=[0,0.4,2.0], row1=[1.0,1.6,0.2].
  const std::vector<std::vector<float>> lists = {{0.0f, 0.4f, 2.0f}, {1.0f, 1.6f, 0.2f}};
  const int sampleCount = 3, listCount = 2;

  // (1a) DEFAULT-param IDENTITY: InputRange=(0,1), OutputRange=(0,1), Clamp=false ⇒ normalized=v, out=v.
  {
    std::vector<float> buf; uint32_t w = 0, h = 0;
    if (!flatCook2(lists, 0.0f, 1.0f, 0.0f, 1.0f, 0.5f, 0.5f, false, buf, w, h, injectBug))
      fail("(1a) flat cook produced no texture");
    if (ok && (w != (uint32_t)sampleCount || h != (uint32_t)listCount)) fail("(1a) flat dims wrong");
    if (ok) {
      // Hand: out == in. row0=[0,0.4,2.0], row1=[1.0,1.6,0.2] (identity).
      const float want[2][3] = {{0.0f, 0.4f, 2.0f}, {1.0f, 1.6f, 0.2f}};
      for (int r = 0; r < listCount && ok; ++r)
        for (int i = 0; i < sampleCount; ++i)
          if (!vt2Nearf(buf[(size_t)r * sampleCount + i], want[r][i])) {
            std::printf("[selftest-valuestotexture2] (1a) texel(%d,%d)=%.4f want %.4f\n", i, r,
                        buf[(size_t)r * sampleCount + i], want[r][i]);
            fail("(1a) identity mismatch");
          }
    }
    // injectBug corrupted texel(0,0) → -1 (≠ 0.0) — the (1a) assert already bit. Done for -bug.
    if (injectBug) {
      std::printf("[selftest-valuestotexture2] %s\n", ok ? "PASS" : "FAIL");
      return ok ? 0 : 1;
    }
  }

  // (1b) NON-TRIVIAL REMAP: InputRange=(0,2) → OutputRange=(0,10), Clamp=false ⇒ out = (v/2)*10 = 5·v.
  {
    std::vector<float> buf; uint32_t w = 0, h = 0;
    if (!flatCook2(lists, 0.0f, 2.0f, 0.0f, 10.0f, 0.5f, 0.5f, false, buf, w, h, false))
      fail("(1b) flat cook produced no texture");
    if (ok) {
      // Hand (5·v): row0 = [0, 2.0, 10.0], row1 = [5.0, 8.0, 1.0].
      const float want[2][3] = {{0.0f, 2.0f, 10.0f}, {5.0f, 8.0f, 1.0f}};
      for (int r = 0; r < listCount && ok; ++r)
        for (int i = 0; i < sampleCount; ++i)
          if (!vt2Nearf(buf[(size_t)r * sampleCount + i], want[r][i])) {
            std::printf("[selftest-valuestotexture2] (1b) texel(%d,%d)=%.4f want %.4f\n", i, r,
                        buf[(size_t)r * sampleCount + i], want[r][i]);
            fail("(1b) remap mismatch");
          }
    }
  }

  // (1c) CLAMP ON path: InputRange=(0,1), OutputRange=(0,1), Clamp=true, GainAndBias=(0.25,0.5) (g<0.5 →
  //   exercises GetBias∘GetSchlickBias). The op's cell must equal remapRef byte-for-byte (op faithfulness),
  //   AND the values must be clamped into [0,1] (out-of-[0,1] inputs squashed to the endpoints). Lists:
  //   [-0.5, 0.3, 1.5] → normalized [-0.5, 0.3, 1.5] → clamp01 [0, 0.3, 1] → ApplyGainAndBias.
  //   ★ interior probe uses 0.3 (NOT 0.5): 0.5 is a FIXED POINT of ApplyGainAndBias at bias=0.5
  //     (getBias(0.5,·)=id, getSchlickBias(0.25,0.5)=0.5), so it cannot distinguish "ran" from "skipped".
  //   Hand-compute interior (g=0.25,b=0.5,x=0.3): getBias(0.5,0.3)=0.3 (b=0.5 ⇒ identity); then
  //     getSchlickBias(0.25,0.3): 0.3<0.5 → x'=0.6, getBias(0.25,0.6)=0.6/((1/0.25-2)·0.4+1)=0.6/1.8=0.3333,
  //     result=0.5·0.3333=0.16667. So interior want ≈ 0.16667 (≠ 0.3 raw → proves the shaping ran).
  {
    const std::vector<std::vector<float>> cl = {{-0.5f, 0.3f, 1.5f}};
    std::vector<float> buf; uint32_t w = 0, h = 0;
    if (!flatCook2(cl, 0.0f, 1.0f, 0.0f, 1.0f, 0.25f, 0.5f, true, buf, w, h, false))
      fail("(1c) clamp flat cook produced no texture");
    if (ok) {
      for (int i = 0; i < 3; ++i) {
        float want = remapRef(cl[0][i], 0.0f, 1.0f, 0.0f, 1.0f, 0.25f, 0.5f, true);
        if (!vt2Nearf(buf[i], want)) {
          std::printf("[selftest-valuestotexture2] (1c) clamp texel %d=%.5f want %.5f\n", i, buf[i], want);
          fail("(1c) clamp/gainbias != remapRef");
        }
      }
      // Endpoints: normalized(-0.5)→clamp01→0→ApplyGainAndBias(0)=0 (value<0.00001 short-circuit). And
      // normalized(1.5)→clamp01→1→ApplyGainAndBias(1)=1 (value>0.999 short-circuit). Hand-pinned.
      if (ok && !vt2Nearf(buf[0], 0.0f)) fail("(1c) low endpoint != 0 (clamp not applied)");
      if (ok && !vt2Nearf(buf[2], 1.0f)) fail("(1c) high endpoint != 1 (clamp not applied)");
      // Interior hand value 0.16667 (≠ raw 0.3) — pins that ApplyGainAndBias actually ran.
      if (ok && !vt2Nearf(buf[1], 0.16667f, 1e-3f)) fail("(1c) interior != hand 0.16667 (ApplyGainAndBias)");
      if (ok && vt2Nearf(buf[1], 0.3f, 1e-3f)) fail("(1c) interior == raw 0.3 (ApplyGainAndBias did not run)");
    }
  }

  // (1d) SHORT-CELL NaN (★VT2-vs-VT1 divergence, VT2.cs:159): UNequal lists row0=[1,2] (len 2),
  //   row1=[3] (len 1). sampleCount=2; texel(1,1) (row1, col1) is the short cell → orgValue=NaN →
  //   out=NaN. (VT1 would write 0 there.) Assert it is NaN (isnan), and the finite cells are finite.
  {
    const std::vector<std::vector<float>> uneq = {{1.0f, 2.0f}, {3.0f}};
    std::vector<float> buf; uint32_t w = 0, h = 0;
    if (!flatCook2(uneq, 0.0f, 1.0f, 0.0f, 1.0f, 0.5f, 0.5f, false, buf, w, h, false))
      fail("(1d) short-cell cook produced no texture");
    if (ok && (w != 2u || h != 2u)) fail("(1d) dims != 2x2");
    if (ok) {
      // Row-major 2x2: [row0col0, row0col1, row1col0, row1col1] = [1, 2, 3, NaN].
      if (!std::isnan(buf[3])) fail("(1d) short cell != NaN (VT2 must use NaN, not VT1's 0)");
      if (!vt2Nearf(buf[0], 1.0f) || !vt2Nearf(buf[1], 2.0f) || !vt2Nearf(buf[2], 3.0f))
        fail("(1d) finite cells wrong");
    }
  }

  // ---------- LEG 2: RESIDENT (production) PATH — R-2 (device-backed own-tex readback) ----------
  // Wire two FloatsToList producers (row0=[0,0.4,2.0], row1=[1.0,1.6,0.2]) into ValuesToTexture2.Values
  // via the SAME flat-graph shape the VT1 golden uses, then cook through cookResident. InputRange=(0,2),
  // OutputRange=(0,10) ⇒ out = 5·v. Read back the op-owned R32_Float texture; assert == 5·v.
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

    Graph g;
    Node vt; vt.id = 10; vt.type = "ValuesToTexture2";
    vt.params["InputRange.x"] = 0.0f; vt.params["InputRange.y"] = 2.0f;
    vt.params["OutputRange.x"] = 0.0f; vt.params["OutputRange.y"] = 10.0f;
    vt.params["GainAndBias.x"] = 0.5f; vt.params["GainAndBias.y"] = 0.5f;
    vt.params["Clamp"] = 0.0f;
    vt.params["Direction"] = 0.0f;  // Horizontal
    g.nodes.push_back(vt);
    const int vtValuesPin = pinId(10, /*Values port*/ 0);

    auto addFloatsToList = [&](int ftlId, const std::vector<float>& vals, int constBase) {
      Node ftl; ftl.id = ftlId; ftl.type = "FloatsToList";
      g.nodes.push_back(ftl);
      const int ftlInputPin = pinId(ftlId, /*Input port*/ 0);
      int cid = constBase;
      for (float v : vals) {
        Node cn; cn.id = cid; cn.type = "Const"; cn.params["value"] = v;
        g.nodes.push_back(cn);
        g.connections.push_back({1000 + cid, pinId(cid, /*out*/ 1), ftlInputPin});
        ++cid;
      }
      g.connections.push_back({2000 + ftlId, pinId(ftlId, /*out*/ 1), vtValuesPin});
    };
    // Wire-declaration order = row order: FloatsToList #1 (row0) before #2 (row1).
    addFloatsToList(1, {0.0f, 0.4f, 2.0f}, /*constBase=*/100);
    addFloatsToList(2, {1.0f, 1.6f, 0.2f}, /*constBase=*/200);

    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"10");

    MTL::Texture* tex = pg.target();
    const uint32_t wantW = 3, wantH = 2;
    const uint32_t tw = tex ? (uint32_t)tex->width() : 0;
    const uint32_t th = tex ? (uint32_t)tex->height() : 0;
    if (!tex || tw != wantW || th != wantH) {
      std::printf("[selftest-valuestotexture2] resident dims=%ux%u want %ux%u FAIL\n", tw, th, wantW, wantH);
      ok = false;
    } else {
      std::vector<float> px((size_t)tw * th, -999.0f);
      tex->getBytes(px.data(), tw * sizeof(float), MTL::Region::Make2D(0, 0, tw, th), 0);
      // 5·v: row0=[0,2,10], row1=[5,8,1].
      const float want[2][3] = {{0.0f, 2.0f, 10.0f}, {5.0f, 8.0f, 1.0f}};
      for (uint32_t r = 0; r < th && ok; ++r)
        for (uint32_t i = 0; i < tw; ++i) {
          float got = px[(size_t)r * tw + i];
          if (!vt2Nearf(got, want[r][i])) {
            std::printf("[selftest-valuestotexture2] resident texel(%u,%u)=%.4f want %.4f FAIL\n", i, r,
                        got, want[r][i]);
            ok = false;
          }
        }
      if (ok) std::printf("[selftest-valuestotexture2] flat+resident 3x2 R32F remap match\n");
    }

    q->release();
    dev->release();
    pool->release();
  }

  std::printf("[selftest-valuestotexture2] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
