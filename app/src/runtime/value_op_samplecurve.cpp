// SampleCurve op (VALUE-rail: Curve→Float). The animation-curve currency's first sampler leaf: it
// reads a Curve + a time u and exposes Result = (float)curve.GetSampledValue(u) on the value rail.
//
// ★HOST TYPE REUSE (the 承重 simplification): the Curve host type the seam called "(A) sw_curve.h" is
// ALREADY transcribed — runtime/curve.h (sw::Curve) is a complete, tested 1:1 port of TiXL's
// Core/DataTypes/Curve.cs + the four interpolators (Const/Linear/Bezier/Spline) + VDefinition + the
// four outside-curve mappers + SplineInterpolator.UpdateTangents (built by the Animator/timeline
// subsystem; --selftest-curve guards it). Adding a SECOND transcription (sw_curve.h) would be a
// duplicate that can drift from this one — so this op REUSES sw::Curve as the port currency. (No new
// host type added; ARCHITECTURE 先求簡單 — 能用現有結構解決就不加框架.)
//
// TiXL authority: external/tixl/Operators/Lib/numbers/curve/SampleCurve.cs (verbatim below):
//
//   SampleCurve.cs Update(context):
//     if (Curve == null) return;                          // cs:21-22 (slot existence guard)
//     var u = U.GetValue(context);                        // cs:23
//     var c = Curve.GetValue(context);                    // cs:24
//     CurveOutput.Value = c;                              // cs:26 (Curve passthrough)
//     if (c == null) return;                              // cs:31-32
//     Result.Value = (float)c.GetSampledValue(u);         // cs:34
//
//   Ports (SampleCurve.cs:6-42) + .t3 DefaultValue (SampleCurve.t3, load-bearing — a fresh drop must
//   match TiXL's fresh node):
//     Result      = Slot<float>  output  (cs:7)
//     CurveOutput = Slot<Curve>  output  (cs:10)  — Curve passthrough
//     Curve       = InputSlot<Curve>     (cs:39)  — embedded default = 2-key LINEAR curve:
//                     key0: Time=0.0, Value=0.0,                InType/OutType=Linear
//                     key1: Time=1.0, Value=1.0000000149011612, InType/OutType=Linear
//     U           = InputSlot<float>     (cs:42)  .t3 default 0.0
//
// REGISTRATION DESIGN (mirror of SampleGradient's value-rail half):
//   SampleCurve registers as a ValueOp — its NodeSpec carries the Result via NodeSpec.evaluate. CMake
//   globs value_op_*.cpp → no CMakeLists edit. The leaf is write-only (no shared file edited; the
//   value-op sink picks it up + valueOpSelfTests dispatches --selftest-samplecurve).
//
// FORKS (named):
//   - fork-samplecurve-result-via-default-curve: TiXL's Result reads the UPSTREAM wired Curve. evalFloat/
//     evalResidentFloat gather only Float ports; the Curve port is skipped. evaluate() therefore uses the
//     .t3-embedded default curve (2-key LINEAR 0→1) whenever the Curve input is unwired — which is ALWAYS
//     true from the value rail's perspective (the value rail cannot carry Curve currency; same structural
//     constraint as SampleGradient's Color rail). Parity is exact for the default-curve case (the golden
//     coord). When a Curve IS wired in a live graph, the value rail reads the DEFAULT curve (diverging
//     from the wired curve). Acknowledged (NOT a taste call — the value rail has no Curve currency).
//   - fork-samplecurve-curveoutput-not-cooked: CurveOutput (the Curve passthrough, cs:26) is declared but
//     not cooked — no Curve producer/consumer pair exists in this batch to exercise it. CurvesToTexture
//     (the real Curve consumer) reads its own embedded default Curve, not a wired SampleCurve.CurveOutput.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"            // EvaluationContext full def (golden ctx)
#include "runtime/curve.h"               // sw::Curve / VDefinition (the REUSED host currency type)
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (PRODUCTION gather)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runSampleCurveSelfTest(bool injectBug);

namespace {

// The .t3-embedded default Curve for SampleCurve's Curve input (SampleCurve.t3). Used by evaluate()
// when the Curve port is unwired (always, from the value rail — fork-samplecurve-result-via-default-curve).
// 2-key LINEAR curve: (0, 0) → (1, 1.0000000149011612). Built via Curve::addOrUpdate, which runs
// SplineInterpolator.UpdateTangents (Curve.cs:226 discipline) — for LINEAR keys the recomputed tangents
// are unused by sample() (the Linear/Linear branch ignores tangent angles), so the result is unaffected.
const Curve& defaultSampleCurveInput() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0; k1.value = 1.0000000149011612;  // SampleCurve.t3 key1 Value (load-bearing)
    k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
    c.addOrUpdate(0.0, k0);  // = Curve.AddOrUpdateV → updateTangents (Curve.cs:213-229)
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

// evaluate: SampleCurve's Result output on the VALUE rail.
// Port layout (in[] indices = Float input ports only; Curve port skipped by evalFloat):
//   in[0] = U (.t3 default 0.0)
//   (Curve port at spec index 1 is dataType="Curve", skipped by evalFloat — fork-result-via-default-curve)
// Output ports: Result at spec index 2, CurveOutput at index 3 (dataType="Curve", never asks evaluate).
//
// cs:23-34 VERBATIM using the .t3 default curve (Curve unwired on the value rail):
float evalSampleCurve(int outIdx, const float* in, int n, const EvaluationContext&) {
  (void)outIdx;  // only Result is a Float output; CurveOutput is dataType="Curve" (never asks evaluate)
  if (n < 1) return 0.0f;
  const double u = (double)in[0];  // U (cs:23) — promoted to double for GetSampledValue
  // c = Curve.GetValue(context) (cs:24): the .t3 embedded default (fork-result-via-default-curve).
  const Curve& c = defaultSampleCurveInput();
  return (float)c.sample(u);  // cs:34 Result.Value = (float)c.GetSampledValue(u)
}

}  // namespace

// Self-registration (ValueOp). File-scope static; independent leaf .cpp (CMake globs value_op_*.cpp).
//
// Port order (load-bearing for evalFloat's in[]):
//   0: U           Float input  (.t3 default 0.0)
//   1: Curve       Curve input  (skipped by evalFloat; would be gathered by a Curve cook driver)
//   2: Result      Float output (evaluate → (float)sample(U))
//   3: CurveOutput Curve output (passthrough; not cooked in this batch — fork-curveoutput-not-cooked)
static const ValueOp _reg_samplecurve{
    {"SampleCurve", "SampleCurve",
     {// Float input (gathered into in[0] by evalFloat; defines n=1):
      {"U", "U", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      // Curve input (dataType="Curve"): NOT gathered by evalFloat (skipped as non-Float).
      {"Curve", "Curve", "Curve", true},
      // Result output: the sampled scalar (value rail).
      {"Result", "Result", "Float", false},
      // CurveOutput passthrough (dataType="Curve"): declared for a future graph; not cooked here.
      {"CurveOutput", "CurveOutput", "Curve", false}},
     evalSampleCurve},
    "samplecurve", runSampleCurveSelfTest};

// ===================== --selftest-samplecurve (in-file golden) ===========================
// FLAT golden + ★R-2 RESIDENT-PATH leg (SampleGradient pattern: libFromGraph→buildEvalGraph→
// evalResidentFloat). References HAND-COMPUTED from the TiXL formula + the .t3 default curve.
//
// GOLDEN COORD: U=0.5, default 2-key LINEAR curve from SampleCurve.t3:
//   key0: u=0.0, value=0.0,                InInterp/OutInterp=Linear
//   key1: u=1.0, value=1.0000000149011612, InInterp/OutInterp=Linear
//
// TiXL arithmetic (Curve.cs:308-360 GetSampledValue(u=0.5)):
//   uRounded = Round(0.5, 4) = 0.5. firstU=0, lastU=1.
//   0.5 not <= 0 and not >= 1 → no Pre/Post mapper → offset=0, mappedU=0.5.
//   mappedU(0.5) not <= 0 and not >= 1 → interpolation branch.
//   upper_bound(0.5)=key1; predecessor a=key0, b=key1.
//   a.OutInterpolation=Linear AND b.InInterpolation=Linear → LinearInterpolator (cs:348):
//     a.value + (b.value - a.value) * ((u - aKey) / (bKey - aKey))
//     = 0.0 + (1.0000000149011612 - 0.0) * ((0.5 - 0.0) / (1.0 - 0.0))
//     = 1.0000000149011612 * 0.5
//     = 0.5000000074505806
//   (float) cast → 0.50000000745f.  → want = 0.5000000074505806
//
// RED injectBug: negate U → sample(-0.5). uRounded=-0.5 <= firstU(0) → preCurveMapping (Constant) →
//   mappedU=-0.5, offset=0. mappedU(-0.5) <= firstU(0) → cs:369 return offset + firstVal = 0 + 0.0 = 0.0.
//   So got=0.0 while want stays 0.5 (FIXED at true value, NOT flipped with the bug) → |0.0-0.5|=0.5 ≫ eps
//   → FAIL. The REAL term flipped is the U argument to sample() (NOT the expected value — that pins to
//   the constant TiXL value, the verify rule that benched a prior op when violated).

int runSampleCurveSelfTest(bool injectBug) {
  const float eps = 1e-6f;
  bool ok = true;

  auto check = [&](const char* tag, float got, float want) {
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    std::printf("[selftest-samplecurve] %s got=%.10f want=%.10f -> %s\n",
                tag, got, want, pass ? "PASS" : "FAIL");
  };

  // FLAT path: build a stand-alone SampleCurve node, evaluate Result via evalFloat.
  // injectBug negates U → sample(-0.5) → clamps to key0.value(0.0) → diverges from want(0.5).
  auto evalResultFlat = [&]() -> float {
    const NodeSpec* spec = findSpec("SampleCurve");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "SampleCurve";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    const int nid = g.nodes.back().id;
    g.node(nid)->params["U"] = injectBug ? -0.5f : 0.5f;  // RED term: U fed wrong
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // RESIDENT-PATH eval (★ R-2 iron rule — the PRODUCTION gather, SampleGradient pattern). evalResident-
  // Float resolves U from the resident constant driver (same value) and calls the SAME evaluate() — so
  // resident == flat == TiXL for the default-curve coord. PRIMARY TOOTH: a future change to kMaxFloatIn
  // or the resident Float-gather cap that truncated the 1-input gather → evalResidentFloat returns 0/NaN
  // → assert flips RED.
  auto evalResultResident = [&]() -> float {
    const NodeSpec* spec = findSpec("SampleCurve");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "SampleCurve";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    const int nid = g.nodes.back().id;
    g.node(nid)->params["U"] = injectBug ? -0.5f : 0.5f;
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
    return evalResidentFloat(rg, std::to_string(nid), "Result", rc);
  };

  // CASE A: U=0.5, default LINEAR curve → hand-computed 0.5000000074505806.
  // want is FIXED at the true (non-bug) TiXL value — does NOT flip with injectBug (the verify rule).
  const float wantA = 0.5000000074505806f;
  const float fA = evalResultFlat();
  check("A flat Result U=0.5 (LINEAR 0->1)", fA, wantA);

  // RESIDENT-PATH leg (proves the PRODUCTION gather; resident == flat == TiXL).
  const float rA = evalResultResident();
  check("A resident Result==TiXL", rA, wantA);
  check("A resident Result==flat", rA, fA);

  std::printf("[selftest-samplecurve] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
