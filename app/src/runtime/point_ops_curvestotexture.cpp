// CurvesToTexture tex op (the Curve CONSUMER + the tex-output fork — completes the cpu-upload rail
// 3/4: ValuesToTexture[FloatList] + GradientsToTexture[Gradient] + CurvesToTexture[Curve]). It reads N
// host Curves + scalar params (SampleSize/Direction) and turns them into a data-sized R32_Float texture
// (1 float/texel = sampled curve value). Named point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS)
// picks it up — identical to ValuesToTexture / GradientsToTexture.
//
// ★HOST TYPE REUSE: the Curve currency is sw::Curve (runtime/curve.h) — the already-transcribed,
// tested 1:1 port of TiXL Curve.cs + interpolators + mappers (--selftest-curve). No duplicate host type.
//
// TiXL authority: external/tixl/Operators/Lib/numbers/curve/CurvesToTexture.cs (ported VERBATIM below —
// every line ref is to that file) + CurvesToTexture.t3 default mirror:
//   - Curves         = MultiInputSlot<Curve> (:136)        → our "Curves" Curve MultiInput port.
//   - ExportGrayScale= InputSlot<bool>        (:139)        → Float param; .t3 default false (0).
//   - SampleSize     = InputSlot<int>         (:142)        → Float param; .t3 default 256.
//   - Direction      = InputSlot<int> {Horizontal,Vertical} (:144-145) → Float Widget::Enum; .t3 0.
//   - useGrayScale = ExportGrayScale          (:24).
//   - useHorizontal = Direction == 0          (:25).
//   - sampleCount = SampleSize.Clamp(1, 16384)(:26).
//   - curveCount = #wired curves (CollectedInputs.Count) OR the single slot-default curve (:28-49);
//                  0 → return (:48-49).
//   - per sample: value = (float)curve.GetSampledValue((float)sampleIndex / sampleCount) (:84/92/105/113).
//     ★NB the divisor is sampleCount, NOT (sampleCount-1) — different from GradientsToTexture (i/(N-1)).
//   - Horizontal fill: row-major, one ROW per curve (outer=curve, inner=sample) (:76-96).
//   - Vertical   fill: column-major, one COLUMN per curve (outer=sample, inner=curve) (:97-117).
//   - width  = useHorizontal ? sampleCount : curveCount (:65).
//     height = useHorizontal ? curveCount  : sampleCount(:66).
//   - format = useGrayScale ? R32G32B32A32_Float : R32_Float (:72).
//
// ★TEX-OUTPUT FORK (named, same as ValuesToTexture/GradientsToTexture): CurvesToTexture does NOT use the
//   tex-walker's ensureTex output (RGBA8Unorm, resolution-pinned). TiXL allocates its OWN
//   Texture2DDescription (:63-74) sized to the DATA, format R32_Float. We mirror this: the op is marked
//   registerTexOpOwnsOutput, so the cook driver hands it ownTexHost/ownTexW/ownTexH (NO ensureTex), the op
//   computes dims + writes the host float buffer (1 float/texel), and the DRIVER allocates the op-owned
//   R32_Float texture via ensureOwnedTex (parked in texBuf → released on realloc + in ~PointGraph → NO
//   per-cook leak). ADDITIVE; FORCED by TiXL parity (not a taste call).
//
// FORKS (named):
//   - fork-curvestotexture-r32-only (grayscale deferred): the engine's own-tex format is a per-TYPE
//     static (texOpOwnFormat — GradientsToTexture is fixed at RGBA32, ValuesToTexture at R32). TiXL's
//     ExportGrayScale toggles R32_Float ↔ R32G32B32A32_Float at RUNTIME (:72), which the static format
//     contract cannot express without a per-cook own-tex format (an engine change, out of this proving
//     op's scope). This op implements the .t3 DEFAULT path (ExportGrayScale=false → R32_Float, 1
//     float/texel) faithfully; ExportGrayScale=true (RGBA32, 4 floats/texel) is a DEFERRED fork — the
//     param is still exposed but ignored (always R32). When a per-cook own-tex format lands, the grayscale
//     branch (write value 4× + alpha 1) is trivial to add (CurvesToTexture.cs:82-89/103-110).
//   - fork-curvestotexture-embedded-default-curve: there is no Curve PRODUCER op yet, so a wired Curves
//     input has no source. TiXL's Curves.GetValue(context) (:42-45) falls back to the slot-default curve
//     when no connections. We mirror that: when c.inputCurves is empty/null (ALWAYS in production today),
//     the op cooks its .t3-embedded default curve (2-key SMOOTH 0→1). The golden injects custom curves
//     via c.inputCurves to exercise all four interpolation modes. Acknowledged (the production resident
//     path cooks the embedded default — honest, NOT the flat-only string-rail trap: no real wire dropped).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary (resident cook input)
#include "runtime/curve.h"                     // sw::Curve / VDefinition (the consumed currency)
#include "runtime/eval_context.h"              // EvaluationContext
#include "runtime/graph.h"                     // Graph/Node/pinId (resident golden)
#include "runtime/graph_bridge.h"              // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp (spec+selftest+registerTexOp sinks)
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOpOwns*
#include "runtime/resident_eval_graph.h"       // ResidentEvalGraph / buildEvalGraph

namespace sw {

int runCurvesToTextureSelfTest(bool injectBug);
// Test-only injection seam (the golden's RED case corrupts the REAL cook output, NOT the expected value):
// when set, the op writes a sentinel (-1) into texel(0,0) so the readback diverges regardless of the
// true value at that texel.
bool& curvesToTextureInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// The .t3-embedded default Curve for CurvesToTexture's Curves input (CurvesToTexture.t3). Used when the
// Curves input is unwired (always, in production — no Curve producer yet; fork-embedded-default-curve).
// 2-key SMOOTH curve: (0,0) → (1,1). Built via Curve::addOrUpdate → SplineInterpolator.UpdateTangents
// (Curve.cs:226) so the Smooth tangents are recomputed exactly as TiXL does on load (★the seam's
// tangent-recompute requirement: without it, the Smooth segment's spline math diverges).
const Curve& defaultCurvesToTextureInput() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Smooth; k0.outInterpolation = KeyInterpolation::Smooth;
    VDefinition k1;
    k1.u = 1.0; k1.value = 1.0;
    k1.inInterpolation = KeyInterpolation::Smooth; k1.outInterpolation = KeyInterpolation::Smooth;
    c.addOrUpdate(0.0, k0);  // = Curve.AddOrUpdateV → updateTangents (Curve.cs:213-229)
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

// cookCurvesToTexture: read inputCurves (the N gathered Curves, or the embedded default if none) +
// SampleSize/Direction, sample each curve at sampleCount uniform t = i/sampleCount, write *ownTexHost as
// 1 float/texel (R32_Float). The driver uploads it to an R32_Float texture (dims curveCount × sampleCount
// per useHorizontal). (ExportGrayScale ignored — fork-curvestotexture-r32-only.)
void cookCurvesToTexture(TexCookCtx& c) {
  if (!c.ownTexHost || !c.ownTexW || !c.ownTexH) return;
  *c.ownTexW = 0;
  *c.ownTexH = 0;
  c.ownTexHost->clear();

  // Collect curves. CurvesToTexture.cs:28-49: wired inputs → each non-null upstream curve; else the
  // single slot-default curve. With no Curve producer, inputCurves is empty/null in production → use the
  // embedded default (fork-embedded-default-curve). The golden injects custom curves via inputCurves.
  std::vector<const Curve*> curves;
  if (c.inputCurves && !c.inputCurves->empty()) {
    for (const Curve& cc : *c.inputCurves) curves.push_back(&cc);  // :30-37 (each wired curve)
  } else {
    curves.push_back(&defaultCurvesToTextureInput());  // :42-45 (slot-default fallback)
  }

  const int curveCount = (int)curves.size();
  if (curveCount == 0) return;  // :48-49 (no curves → no texture)

  const int sampleCount =
      (int)std::min(std::max(cookParam(c, "SampleSize", 256.0f), 1.0f), 16384.0f);  // :26 Clamp(1,16384)
  const bool useHorizontal = cookParam(c, "Direction", 0.0f) < 0.5f;               // :25 (0 == Horizontal)

  std::vector<float>& out = *c.ownTexHost;
  out.reserve((size_t)curveCount * sampleCount);

  // value = (float)curve.GetSampledValue((float)sampleIndex / sampleCount) — :84/92/105/113. ★The divisor
  // is sampleCount (NOT sampleCount-1): t ranges [0, (sampleCount-1)/sampleCount], never reaching 1.0.
  auto sampleAt = [&](const Curve* cv, int i) -> float {
    return (float)cv->sample((double)((float)i / sampleCount));
  };

  if (useHorizontal) {
    // Row-major: one ROW per curve (:76-96) — outer=curve, inner=sample.
    for (const Curve* cv : curves)
      for (int i = 0; i < sampleCount; ++i) out.push_back(sampleAt(cv, i));
  } else {
    // Column-major: one COLUMN per curve (:97-117) — outer=sample, inner=curve.
    for (int i = 0; i < sampleCount; ++i)
      for (const Curve* cv : curves) out.push_back(sampleAt(cv, i));
  }

  *c.ownTexW = useHorizontal ? (uint32_t)sampleCount : (uint32_t)curveCount;  // :65
  *c.ownTexH = useHorizontal ? (uint32_t)curveCount : (uint32_t)sampleCount;  // :66

  // Test-only: corrupt the REAL cook output — sentinel (-1) into texel(0,0) so the RED case bites here
  // regardless of the expected value at that texel. Off in production.
  if (curvesToTextureInjectBug() && !out.empty()) out[0] = -1.0f;
}

}  // namespace

// Self-registration. ImageFilterOp feeds registerTexOp + the spec sink (findSpec) + the selftest sink
// (--selftest-curvestotexture). Marked OWN-TEXTURE (R32_Float, 1 float/texel) so the tex-walker routes
// it through the ownTexHost path with the op-chosen format.
//   Ports: Curves = Curve MultiInput; ExportGrayScale (.t3 false, ignored — fork); SampleSize scalar
//          (.t3 256); Direction = Float Widget::Enum {Horizontal,Vertical}; out = Texture2D output.
// Port order matches CurvesToTexture.cs's Input declaration order is irrelevant (gather is by dataType +
// id); we follow the GradientsToTexture leaf shape (input first, then out, then scalars).
static const ImageFilterOp _reg_curvestotexture{
    {"CurvesToTexture", "CurvesToTexture",
     {{"Curves", "Curves", "Curve", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Texture2D", false},
      {"ExportGrayScale", "ExportGrayScale", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"SampleSize", "SampleSize", "Float", true, 256.0f, 1.0f, 16384.0f, Widget::Slider},
      {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"Horizontal", "Vertical"}, true}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "CurvesToTexture", cookCurvesToTexture, "curvestotexture", runCurvesToTextureSelfTest};

// Mark OWN-TEXTURE + its format (R32_Float, 1 float/texel) at static-init (mirrors ValuesToTexture's
// OwnTexRegistrar). registerTexOpOwnsOutput + registerTexOpOwnFormat are idempotent.
namespace {
struct OwnTexRegistrar {
  OwnTexRegistrar() {
    registerTexOpOwnsOutput("CurvesToTexture");
    registerTexOpOwnFormat("CurvesToTexture", /*floatsPerTexel=*/1);  // R32_Float (fork-r32-only)
  }
};
static const OwnTexRegistrar _reg_curvestotexture_owns;
}  // namespace

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

// ===================== --selftest-curvestotexture (golden) ===========================
// Three legs:
//   (1) FLAT CLOSED-FORM: call cookCurvesToTexture directly with hand-built inputCurves (Const / Linear /
//       Spline / Bezier) + read ownTexHost. Asserts each texel == the HAND-COMPUTED GetSampledValue at
//       that t (Const/Linear: exact arithmetic shown), AND pins the per-segment Bezier-vs-Spline dispatch
//       (Curve.cs:350 SegmentNeedsBezier). No Metal device (pure host).
//   (2) RESIDENT (production) PATH ★R-2: build a stand-alone CurvesToTexture node (no Curve producer
//       exists), cook via PointGraph::cookResident, read back the OP-OWNED R32_Float texture, assert each
//       texel == defaultCurvesToTextureInput().sample(i/sampleCount) (the embedded SMOOTH default). Proves
//       the own-tex cook is LIVE on the production cookResident path (NOT flat-only — the resident own-tex
//       gate was broadened to fire for the Curve currency). The tangent-recompute is load-bearing here:
//       the embedded default is SMOOTH, so the spline tangents must be recomputed (Curve.cs:226).
//   (3) RED: curvesToTextureInjectBug() corrupts the REAL flat cook output (texel(0,0) → -1) so the
//       closed-form assert diverges (want FIXED at the true value, NOT flipped with the bug).
namespace {

bool ctNearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// A Const-Out curve: key0(u=0, value=2.0, OUT=Constant), key1(u=1, value=5.0). For t in (0,1):
//   dispatch: a.OutInterpolation==Constant → constInterp(a,b,t) = |t - 1.0|<0.0001 ? 5.0 : 2.0 = 2.0.
Curve makeConstCurve() {
  Curve c;
  VDefinition k0; k0.u = 0.0; k0.value = 2.0;
  k0.inInterpolation = KeyInterpolation::Constant; k0.outInterpolation = KeyInterpolation::Constant;
  VDefinition k1; k1.u = 1.0; k1.value = 5.0;
  k1.inInterpolation = KeyInterpolation::Constant; k1.outInterpolation = KeyInterpolation::Constant;
  c.addOrUpdate(0.0, k0);
  c.addOrUpdate(1.0, k1);
  return c;
}

// A Linear curve: key0(u=0, value=0, Linear), key1(u=1, value=4, Linear). dispatch: Out=Linear &&
//   In=Linear → linearInterp = 0 + (4-0)*((t-0)/(1-0)) = 4t.
Curve makeLinearCurve() {
  Curve c;
  VDefinition k0; k0.u = 0.0; k0.value = 0.0;
  k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
  VDefinition k1; k1.u = 1.0; k1.value = 4.0;
  k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
  c.addOrUpdate(0.0, k0);
  c.addOrUpdate(1.0, k1);
  return c;
}

// A 3-key SMOOTH bump: (0,0),(0.5,1),(1,0). dispatch: a.Out=Smooth (not Const, not Linear/Linear),
//   not weighted → SplineInterpolator. Passes through the knots exactly; interior is a cubic (≠ flat lerp).
Curve makeSplineCurve() {
  Curve c;
  for (auto kv : {std::pair<double, double>{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}) {
    VDefinition k; k.u = kv.first; k.value = kv.second;
    k.inInterpolation = KeyInterpolation::Smooth; k.outInterpolation = KeyInterpolation::Smooth;
    c.addOrUpdate(kv.first, k);
  }
  return c;
}

// A WEIGHTED-Tangent curve that forces the BEZIER branch (SegmentNeedsBezier, Curve.cs:350): both
// endpoints Tangent + weighted + tensionOut/In != 1.0. Same tangent angle (0 → flat handles) but
// non-default tension → the Bezier control points shift, so the value at t differs from the Spline path.
Curve makeBezierCurve() {
  Curve c;
  VDefinition k0; k0.u = 0.0; k0.value = 0.0;
  k0.inInterpolation = KeyInterpolation::Tangent; k0.outInterpolation = KeyInterpolation::Tangent;
  k0.weighted = true; k0.tensionOut = 2.0f; k0.tensionIn = 2.0f; k0.outTangentAngle = 0.0; k0.inTangentAngle = 0.0;
  VDefinition k1; k1.u = 1.0; k1.value = 1.0;
  k1.inInterpolation = KeyInterpolation::Tangent; k1.outInterpolation = KeyInterpolation::Tangent;
  k1.weighted = true; k1.tensionOut = 2.0f; k1.tensionIn = 2.0f; k1.outTangentAngle = 0.0; k1.inTangentAngle = 0.0;
  c.addOrUpdate(0.0, k0);
  c.addOrUpdate(1.0, k1);
  return c;
}

// Same shape as makeBezierCurve but UN-weighted (tension 1.0, Smooth) — would dispatch to Spline. Used
// only to prove the Bezier dispatch is DISTINCT (the value differs), pinning SegmentNeedsBezier.
Curve makeBezierAsSplineCurve() {
  Curve c;
  VDefinition k0; k0.u = 0.0; k0.value = 0.0;
  k0.inInterpolation = KeyInterpolation::Smooth; k0.outInterpolation = KeyInterpolation::Smooth;
  VDefinition k1; k1.u = 1.0; k1.value = 1.0;
  k1.inInterpolation = KeyInterpolation::Smooth; k1.outInterpolation = KeyInterpolation::Smooth;
  c.addOrUpdate(0.0, k0);
  c.addOrUpdate(1.0, k1);
  return c;
}

// Run cookCurvesToTexture flat over `curves` at sampleCount, Horizontal, return the host float buffer
// (R32_Float, 1 float/texel; row-major: curve r, sample i → out[r*sampleCount + i]).
bool flatCook(const std::vector<Curve>& curves, int sampleCount, std::vector<float>& out,
              uint32_t& w, uint32_t& h, bool injectBug) {
  out.clear(); w = 0; h = 0;
  std::map<std::string, float> params;
  params["SampleSize"] = (float)sampleCount;
  params["Direction"] = 0.0f;  // Horizontal
  params["ExportGrayScale"] = 0.0f;
  TexCookCtx tc;
  tc.inputCurves = &curves;
  tc.ownTexHost = &out; tc.ownTexW = &w; tc.ownTexH = &h;
  tc.params = &params;
  curvesToTextureInjectBug() = injectBug;
  cookCurvesToTexture(tc);
  curvesToTextureInjectBug() = false;
  return w > 0 && h > 0;
}

}  // namespace

int runCurvesToTextureSelfTest(bool injectBug) {
  bool ok = true;
  auto fail = [&](const char* msg) { std::printf("[selftest-curvestotexture] %s FAIL\n", msg); ok = false; };

  // ---------- LEG 1: FLAT CLOSED-FORM (Const / Linear / Spline / Bezier), pure host ----------
  // Stack all four curves as rows; sampleCount=4 → t = {0, 0.25, 0.5, 0.75} (divisor = sampleCount).
  const int N = 4;
  std::vector<Curve> curves = {makeConstCurve(), makeLinearCurve(), makeSplineCurve(), makeBezierCurve()};
  std::vector<float> buf; uint32_t w = 0, h = 0;
  if (!flatCook(curves, N, buf, w, h, injectBug)) fail("flat cook produced no texture");

  if (ok && (w != (uint32_t)N || h != (uint32_t)curves.size()))
    fail("flat dims wrong");

  if (ok) {
    auto texel = [&](int row, int i) -> float { return buf[(size_t)row * N + i]; };
    // t for column i = i / N (CurvesToTexture.cs:84). N=4 → t ∈ {0, 0.25, 0.5, 0.75}.
    auto tAt = [&](int i) { return (float)i / N; };

    // ROW 0 = Const curve. For every t in [0,0.75): constInterp → a.value = 2.0 (|t-1|>0.0001).
    //   want = 2.0 at all four columns (hand-computed; the divisor never reaches t=1 so b.value(5) unused).
    for (int i = 0; i < N; ++i)
      if (!ctNearf(texel(0, i), 2.0f)) fail("const row texel != 2.0");

    // ROW 1 = Linear curve. want = 4*t = {0, 1.0, 2.0, 3.0} (hand-computed: 4 * {0,0.25,0.5,0.75}).
    const float linWant[N] = {0.0f, 1.0f, 2.0f, 3.0f};
    for (int i = 0; i < N; ++i)
      if (!ctNearf(texel(1, i), linWant[i])) {
        std::printf("[selftest-curvestotexture] linear col %d got=%.5f want=%.5f\n", i, texel(1, i), linWant[i]);
        fail("linear row mismatch");
      }

    // ROW 2 = Spline (Smooth bump). Knot at t=0 (col 0) is EXACTLY 0 (hand: sample(0)=key0.value=0).
    //   Knot at t=0.5 (col 2) is EXACTLY 1 (sample(0.5)=key1.value=1). The op must equal sample() at
    //   every t (op faithfulness, the trusted reference); pin the two knots to hand values + the rest
    //   to sample() so a misroute/wrong-t bites.
    if (!ctNearf(texel(2, 0), 0.0f)) fail("spline knot t=0 != 0");
    if (!ctNearf(texel(2, 2), 1.0f)) fail("spline knot t=0.5 != 1");
    for (int i = 0; i < N; ++i)
      if (!ctNearf(texel(2, i), (float)curves[2].sample((double)tAt(i)), 1e-4f))
        fail("spline row != getSampledValue");
    // Interior t=0.25 (col 1) is a cubic ≠ flat lerp (0.5) — proves the spline solver ran (dispatch).
    if (ok && ctNearf(texel(2, 1), 0.5f, 1e-3f)) fail("spline interior == flat lerp (not a spline)");

    // ROW 3 = Bezier (weighted Tangent). The op must equal sample() (which dispatches to Bezier).
    for (int i = 0; i < N; ++i)
      if (!ctNearf(texel(3, i), (float)curves[3].sample((double)tAt(i)), 1e-4f))
        fail("bezier row != getSampledValue");
    // ★DISPATCH PIN (Curve.cs:350 SegmentNeedsBezier): the weighted-Tangent curve at t=0.25 must DIFFER
    //   from the same-shape UN-weighted (Smooth→Spline) curve — proving SegmentNeedsBezier routed to the
    //   Bezier branch, not Spline. (Both pass through (0,0)/(1,1); the interior differs by tension.)
    {
      Curve asSpline = makeBezierAsSplineCurve();
      float bez = (float)curves[3].sample(0.25);
      float spl = (float)asSpline.sample(0.25);
      if (ctNearf(bez, spl, 1e-3f))
        fail("bezier dispatch not distinct from spline (SegmentNeedsBezier did not route)");
    }
  }

  // injectBug corrupted texel(0,0) → -1 (≠ 2.0) → the const-row assert already bit above. Done for -bug.
  if (injectBug) {
    std::printf("[selftest-curvestotexture] %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
  }

  // ---------- LEG 2: RESIDENT (production) PATH — R-2 iron rule (device-backed own-tex readback) ----------
  // Stand-alone CurvesToTexture node (no Curve producer); cooks the EMBEDDED SMOOTH default curve.
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

    const int res = 4;  // small + exact: t = {0, 0.25, 0.5, 0.75} (divisor = sampleCount = res).
    Graph g;
    Node ct; ct.id = 30; ct.type = "CurvesToTexture";
    ct.params["SampleSize"] = (float)res;
    ct.params["Direction"] = 0.0f;        // Horizontal
    ct.params["ExportGrayScale"] = 0.0f;  // R32 (fork-r32-only)
    g.nodes.push_back(ct);

    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"30");

    MTL::Texture* tex = pg.target();
    const uint32_t tw = tex ? (uint32_t)tex->width() : 0;
    const uint32_t th = tex ? (uint32_t)tex->height() : 0;
    if (!tex || tw != (uint32_t)res || th != 1u) {
      std::printf("[selftest-curvestotexture] resident dims=%ux%u want %dx1 FAIL\n", tw, th, res);
      ok = false;
    } else {
      // R32_Float: 1 float/texel, rowPitch = w*4 bytes. Read back + assert == embedded SMOOTH default.
      std::vector<float> px((size_t)tw * th, -999.0f);
      tex->getBytes(px.data(), tw * sizeof(float), MTL::Region::Make2D(0, 0, tw, th), 0);
      const Curve& ref = defaultCurvesToTextureInput();
      for (uint32_t i = 0; i < tw; ++i) {
        float t = (float)i / res;  // CurvesToTexture.cs:84 divisor
        float want = (float)ref.sample((double)t);
        if (!ctNearf(px[i], want, 2e-3f)) {
          std::printf("[selftest-curvestotexture] resident texel %u t=%.3f got=%.5f want=%.5f FAIL\n",
                      i, t, px[i], want);
          ok = false;
        }
      }
      // Knot pins (hand-computed): default SMOOTH curve has value 0 at t=0 and rises toward 1 — col 0 == 0.
      if (ok && !ctNearf(px[0], 0.0f, 2e-3f)) {
        std::printf("[selftest-curvestotexture] resident knot t=0 got=%.5f want=0 FAIL\n", px[0]);
        ok = false;
      }
      if (ok) std::printf("[selftest-curvestotexture] flat+resident %dx1 R32F curve match\n", res);
    }

    q->release();
    dev->release();
    pool->release();
  }

  std::printf("[selftest-curvestotexture] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
