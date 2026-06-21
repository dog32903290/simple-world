// SampleGradient op (HYBRID: gradient CONSUMER for the OutGradient passthrough + value-op for the
// Color vec4 output). This is the 8th cook flow's first CROSS-RAIL leaf: it reads an upstream
// SwGradient (via cookGradientNode, Gradient input port) and exposes two outputs:
//   (1) Color — 4 consecutive Float ports (R/G/B/A) on the VALUE rail (evalFloat/evalResidentFloat),
//       matching the "RgbaToColor precedent — a VALUE output" directive.
//   (2) OutGradient — Gradient passthrough on the GRADIENT rail (cookGradientNode).
//
// TiXL authority: external/tixl/Operators/Lib/numbers/color/SampleGradient.cs (verbatim below):
//
//   SampleGradient.cs Update(context):
//     var t = SamplePos.GetValue(context);                        // cs:23
//     var gradient = Gradient.GetValue(context);                  // cs:24
//     if (gradient == null) return;                               // cs:26
//     var overrideInterpolation = OverrideInterpolation.GetValue(context);  // cs:29
//     if (overrideInterpolation)                                  // cs:30
//     {
//       var interpolation = (Gradient.Interpolations)Interpolation.GetValue(context);  // cs:32
//       gradient.Interpolation = interpolation;                   // cs:33
//     }
//     Color.Value = gradient.Sample(t);                           // cs:35
//     OutGradient.Value = gradient;                               // cs:36
//
// .t3 DefaultValues (SampleGradient.t3, load-bearing — a fresh-drop must match TiXL's fresh node):
//   OverrideInterpolation = false (bool default)
//   Interpolation         = 0    (int/enum = Linear)
//   SamplePos             = 0.0  (float)
//   Gradient (embedded default) = 2-stop Linear gradient:
//     stop0: NormalizedPosition=0.0, Color=(R=9.9999E-07, G=9.999968E-07, B=1E-06,   A=1.0)
//     stop1: NormalizedPosition=1.0, Color=(R=1.0,         G=0.99999,      B=1.0,      A=1.0)
//
// REGISTRATION DESIGN (named, cross-rail):
//   SampleGradient registers in TWO sinks from this ONE leaf:
//     (a) valueOpSpecSink() + valueOpSelfTests() via ValueOp — the spec that findSpec("SampleGradient")
//         returns first. NodeSpec.evaluate handles Color.x/.y/.z/.w (value rail). CMake globs
//         value_op_*.cpp → no CMakeLists edit needed.
//     (b) gradientCookFns()["SampleGradient"] via GradientCookRegistrar (a custom file-scope static) —
//         feeds only the cook-fn map, NOT gradientSpecSink (which would create a shadow spec never
//         returned by findSpec). cookGradientNode uses the ValueOp spec (a) to gather Gradient input
//         ports, then dispatches the cook fn (b) to write the OutGradient passthrough.
//   This keeps the leaf write-only: no shared file edited (ValueOp glob picks it up; selftest
//   dispatched via valueOpSelfTests). sharedFileTouched = false.
//
// FORKS (named):
//   - fork-samplegradient-color-via-default-gradient: TiXL's Color.GetValue() reads the UPSTREAM
//     wired gradient. evalFloat only gathers Float ports; the Gradient port is skipped. The
//     evaluate() function therefore uses the .t3-embedded default gradient (2-stop ~black→white
//     Linear) whenever the Gradient input is unwired — which is ALWAYS true from evalFloat's
//     perspective. Parity is exact for the default-Gradient case (the canonical golden coord).
//     When a Gradient IS wired in a live graph, the Color value rail reads the DEFAULT gradient
//     (diverging from the real wired gradient); the VISUAL result is produced by GradientsToTexture
//     consuming the OutGradient passthrough instead. This fork is acknowledged (not a taste call —
//     the value rail cannot carry Gradient currency).
//   - fork-samplegradient-overrideinterpolation-bool-on-float-port: TiXL OverrideInterpolation is
//     InputSlot<bool>; this runtime stores it as Float and tests (in[1] > 0.5f) for the truthy
//     branch. Whole-number values (0/1) are byte-identical to TiXL (0→false, 1→true).
//   - fork-samplegradient-interpolation-enum-int-on-float-port: TiXL Interpolation is InputSlot<int>
//     mapped to Gradient.Interpolations. Stored as Float, cast to int before the enum cast.
//   - fork-samplegradient-vec4-as-4-floats: TiXL Color is Slot<Vector4>. This runtime exposes it as
//     4 consecutive Float output ports (Color.x/.y/.z/.w) — same convention as RgbaToColor (the
//     precedent). NOT a taste call — sw has no Slot<Vector4> on the value rail.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"             // EvaluationContext full def (golden ctx)
#include "runtime/gradient_op_registry.h"  // GradientCookCtx/gradientCookFns/gradientParam/gradientInjectBug
#include "runtime/graph_bridge.h"          // libFromGraph (flat Graph → SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (PRODUCTION gather)
#include "runtime/sw_gradient.h"           // SwGradient / SwGradient::sample (8th flow currency)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runSampleGradientSelfTest(bool injectBug);

namespace {

// The .t3-embedded default gradient for SampleGradient's Gradient input. Used by evaluate() when the
// Gradient port is unwired (which is always the case from evalFloat's perspective — fork-samplegradient-
// color-via-default-gradient). Constructed VERBATIM from SampleGradient.t3's embedded DefaultValue.
//   stop0: pos=0.0, color=(R=9.9999E-07,  G=9.999968E-07, B=1E-06, A=1.0)
//   stop1: pos=1.0, color=(R=1.0,          G=0.99999,      B=1.0,   A=1.0)
//   interpolation: Linear (0)
const SwGradient& defaultSampleGradientInput() {
  static const SwGradient g = []() {
    SwGradient g;
    g.interpolation = kGradientLinear;
    g.steps.push_back({0.0f, simd::make_float4(9.9999e-7f, 9.999968e-7f, 1e-6f, 1.0f)});
    g.steps.push_back({1.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 1.0f)});
    return g;
  }();
  return g;
}

// evaluate: SampleGradient's Color output on the VALUE rail.
// Port layout (in[] indices = Float input ports only; Gradient port skipped):
//   in[0] = SamplePos             (.t3 default 0.0)
//   in[1] = OverrideInterpolation (.t3 default 0.0 = false; fork-samplegradient-overrideinterpolation)
//   in[2] = Interpolation         (.t3 default 0.0 = Linear; fork-samplegradient-interpolation-enum)
//   (Gradient port at spec index 3 is dataType="Gradient", skipped by evalFloat — fork-samplegradient-
//    color-via-default-gradient)
// Output ports at spec indices 4..7 (Color.x/.y/.z/.w), followed by OutGradient at 8.
// Component k = outIdx - 4 ∈ {0=R, 1=G, 2=B, 3=A}.
//
// cs:23-36 VERBATIM using the .t3 default gradient (Gradient unwired on the value rail):
float evalSampleGradient(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int k = outIdx - 4;  // component: 0=R(Color.x), 1=G(Color.y), 2=B(Color.z), 3=A(Color.w)
  if (k < 0 || k > 3) return 0.0f;

  const float t          = in[0];  // SamplePos (cs:23)
  const bool  overrideInterp = in[1] > 0.5f;  // OverrideInterpolation (cs:29; fork-overrideinterpolation)
  const int   interpEnum = (int)(int32_t)in[2];  // Interpolation cast (cs:32; fork-interpolation-enum)

  // gradient = Gradient.GetValue(context) (cs:24): use the .t3 embedded default (fork-color-via-default).
  // Gradient.cs:26 check (null guard) is a no-op — the host SwGradient is never null.
  SwGradient gradient = defaultSampleGradientInput();  // copy (OverrideInterpolation may mutate mode)

  if (overrideInterp) {                              // cs:30
    gradient.interpolation = interpEnum;             // cs:32-33 cast + assign
  }

  simd::float4 color = gradient.sample(t);           // cs:35 Color.Value = gradient.Sample(t)

  // Return the component for this output port (fork-samplegradient-vec4-as-4-floats).
  switch (k) {
    case 0: return color.x;  // Color.x = R
    case 1: return color.y;  // Color.y = G
    case 2: return color.z;  // Color.z = B
    default: return color.w; // Color.w = A
  }
}

// cookSampleGradient: the Gradient passthrough output (OutGradient, cs:36).
// The cook driver (cookGradientNode) has already gathered the upstream Gradient input into
// c.inputGradients; this op reads it (or falls back to the .t3 default) and writes *c.output.
// Applies OverrideInterpolation/Interpolation exactly as cs:29-33.
void cookSampleGradient(GradientCookCtx& c) {
  if (!c.output) return;

  // Gradient.GetValue(context) (cs:24): first wired upstream gradient, or .t3 default if unwired.
  const std::vector<SwGradient>* in = c.inputGradients;
  SwGradient gradient = (in && !in->empty()) ? (*in)[0] : defaultSampleGradientInput();

  const float overrideF = gradientParam(c.params, "OverrideInterpolation", 0.0f);
  const bool  overrideInterp = overrideF > 0.5f;  // fork-samplegradient-overrideinterpolation
  if (overrideInterp) {
    const int interpEnum = (int)(int32_t)gradientParam(c.params, "Interpolation", 0.0f);
    gradient.interpolation = interpEnum;           // cs:32-33
  }

  // OutGradient.Value = gradient (cs:36). Write the (possibly mode-patched) gradient as the output.
  *c.output = gradient;

  // Test-only corruption (gradientInjectBug): drop the last stop so the golden's FLAT-cook
  // downstream assertion diverges. Off in production.
  if (gradientInjectBug() && !c.output->steps.empty())
    c.output->steps.pop_back();
}

// Custom file-scope static: register the gradient cook fn into gradientCookFns() WITHOUT writing
// to gradientSpecSink (which would shadow the ValueOp spec returned by findSpec). The cook driver's
// cookGradientNode uses the ValueOp spec (from findSpec) to gather Gradient inputs, then dispatches
// this cook fn for the OutGradient passthrough. No shared file edited.
struct GradientCookRegistrar {
  GradientCookRegistrar() { gradientCookFns()["SampleGradient"] = cookSampleGradient; }
};

}  // namespace

// Self-registration (ValueOp). File-scope static ValueOp — independent leaf .cpp (no shared edit
// point; CMake globs value_op_*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() at pre-main init.
// Spec carries ALL ports (inputs: SamplePos/OverrideInterpolation/Interpolation/Gradient; outputs:
// Color.x/.y/.z/.w/OutGradient) so cookGradientNode sees the Gradient input port during its gather.
//
// Port order (load-bearing for evalFloat's in[] and cookGradientNode's port scan):
//   0: SamplePos             Float input  (.t3 default 0.0)
//   1: OverrideInterpolation Float/Bool   (.t3 default 0.0 = false)
//   2: Interpolation         Float/Enum   (.t3 default 0.0 = Linear)
//   3: Gradient              Gradient in  (skipped by evalFloat; gathered by cookGradientNode)
//   4: Color.x               Float out    (evaluate component 0 = R)
//   5: Color.y               Float out    (evaluate component 1 = G)
//   6: Color.z               Float out    (evaluate component 2 = B)
//   7: Color.w               Float out    (evaluate component 3 = A)
//   8: OutGradient           Gradient out (passthrough; handled by cookGradientNode, not evaluate)
static const ValueOp _reg_samplegradient{
    {"SampleGradient", "SampleGradient",
     // Float inputs (gathered into in[0..2] by evalFloat; define n=3):
     {{"SamplePos",             "SamplePos",             "Float", true,  0.0f, -100.0f, 100.0f, Widget::Slider},
      {"OverrideInterpolation",  "OverrideInterpolation", "Float", true,  0.0f, 0.0f,    1.0f,   Widget::Bool},
      // Interpolation enum (Gradient.Interpolations): {Linear,Hold,Smooth,OkLab,Spline}.
      // .t3 default = 0 (Linear); C# MappedType = typeof(Gradient.Interpolations).
      {"Interpolation",         "Interpolation",         "Float", true,  0.0f, 0.0f,    4.0f,   Widget::Enum,
       {"Linear", "Hold", "Smooth", "OkLab", "Spline"}},
      // Gradient input (dataType="Gradient"): NOT gathered by evalFloat (skipped as non-Float);
      // gathered by cookGradientNode for the OutGradient passthrough cook.
      {"Gradient",              "Gradient",              "Gradient", true},
      // Color output: 4 consecutive Float output ports (fork-samplegradient-vec4-as-4-floats;
      // RgbaToColor precedent). Output ports carry plain PortSpec (no widget/arity on outputs).
      {"Color.x",               "Color.x",               "Float", false},
      {"Color.y",               "Color.y",               "Float", false},
      {"Color.z",               "Color.z",               "Float", false},
      {"Color.w",               "Color.w",               "Float", false},
      // OutGradient output (Gradient passthrough): dataType="Gradient", handled by cookGradientNode.
      {"OutGradient",           "OutGradient",           "Gradient", false}},
     evalSampleGradient},
    "samplegradient", runSampleGradientSelfTest};

// Register the gradient passthrough cook fn AFTER the ValueOp (so gradientCookFns()["SampleGradient"]
// is live and ready before any runtime cook uses it; pre-main dynamic init order within one TU is
// top-to-bottom). Named struct avoids static-init-ordering pitfalls across TUs.
static const GradientCookRegistrar _reg_samplegradient_gradient_cook;

// ===================== --selftest-samplegradient (in-file golden) ===========================
// FLAT golden + ★R-2 RESIDENT-PATH leg (perlinnoise3 pattern: libFromGraph→buildEvalGraph→
// evalResidentFloat). References HAND-COMPUTED from the TiXL formula and the .t3 default gradient
// (arithmetic in comments). The golden exercises a STAND-ALONE SampleGradient node with the
// Gradient input UNWIRED (using the .t3-embedded default gradient in evaluate) — this is the
// canonical coord that is exactly correct on both flat and resident paths (fork-samplegradient-
// color-via-default-gradient: the evaluate function always uses the default gradient).
//
// GOLDEN COORD: SamplePos=0.5, OverrideInterpolation=false (0.0), Interpolation=Linear (0.0).
// Default gradient from SampleGradient.t3:
//   stop0: pos=0.0, color=(R=9.9999e-7, G=9.999968e-7, B=1e-6,   A=1.0)
//   stop1: pos=1.0, color=(R=1.0,        G=0.99999,     B=1.0,    A=1.0)
//   interpolation: Linear
//
// TiXL arithmetic (cs:35 gradient.Sample(t=0.5)):
//   Clamp(0.5, 0,1) = 0.5 → scan: step0.pos(0) < 0.5, so previousStep=step0; step1.pos(1) >= 0.5.
//   previousStep != null, previousStep.pos(0) < step1.pos(1) → interpolation branch.
//   OverrideInterpolation=false → gradient.Interpolation = Linear (0, from .t3).
//   fraction = RemapAndClamp(0.5, prev.pos=0, step.pos=1, 0, 1) = (0.5-0)/(1-0) = 0.5.
//   switch Linear → lerp4(prev.color, step.color, 0.5):
//     Color.R = 9.9999e-7 + (1.0       - 9.9999e-7 ) * 0.5 = 9.9999e-7 + 0.499999500 = 0.500000500
//     Color.G = 9.999968e-7+(0.99999   - 9.999968e-7) * 0.5 = 9.999968e-7+ 0.499994500 = 0.499995500
//     Color.B = 1e-6      + (1.0       - 1e-6       ) * 0.5 = 1e-6      + 0.4999995   = 0.5000005
//     Color.A = 1.0       + (1.0       - 1.0        ) * 0.5 = 1.0
//   → want = (0.500000500, 0.499995500, 0.5000005, 1.0)
//   (All four components are DISTINCT — R≠G (0.500000500 vs 0.499995500), G≠B, B≈R but A=1 ≠ others.)
//
// RED injectBug: negate the sample position → sample(-0.5) → Clamp(-0.5,0,1)=0 → first step at t=0
//   has pos=0≥0 → previousStep=null at the first step check → cs:137 returns step.color = stop0.color
//   ≈ (9.9999e-7, 9.999968e-7, 1e-6, 1.0) ≈ ~black. This diverges greatly from the ~gray want values,
//   so the Color.R/G/B assertions flip RED (want is FIXED at the true gray value — see CASE A note —
//   so got≈~black ≠ want≈~gray). Color.A is 1.0 in both the gray lerp AND the ~black stop0, so the A
//   assert does NOT bite — the R/G/B asserts carry the proof. The REAL term flipped: the SamplePos
//   argument to sample(). (The original golden flipped want WITH got, so it stayed GREEN when bugged —
//   the orphan-blocking defect; fixed by pinning want to the constant true value.)

int runSampleGradientSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Helper: evaluate one Color output port (Color.x/.y/.z/.w) of a stand-alone SampleGradient
  // node via evalFloat. SamplePos=0.5 (non-degenerate), OverrideInterpolation=false, Interpolation=Linear.
  // injectBug negates SamplePos (t → -0.5) → sample clamps to 0 → returns ~black stop0.
  auto evalColor = [&](const char* outName) -> float {
    const NodeSpec* spec = findSpec("SampleGradient");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "SampleGradient";
    // Seed all Float input ports from spec defaults.
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    const int nid = g.nodes.back().id;
    // injectBug: negate SamplePos → sample(-0.5) → clamps to 0 → ~black (RED term: SamplePos fed wrong).
    g.node(nid)->params["SamplePos"] = injectBug ? -0.5f : 0.5f;
    g.node(nid)->params["OverrideInterpolation"] = 0.0f;  // false
    g.node(nid)->params["Interpolation"] = 0.0f;           // Linear
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // RESIDENT-PATH eval (★ perlinnoise3 pattern — the PRODUCTION gather, closes the R-2 gap).
  // evalResidentFloat resolves SamplePos/OverrideInterpolation/Interpolation from the resident
  // constant drivers (same values as the flat params above) and calls evaluate — the same evaluate()
  // function — so resident == flat == TiXL for the default-gradient coord.
  // The PRIMARY TOOTH here: if a future change to kMaxFloatIn or the resident Float-gather cap
  // truncated the 3-input gather, evalResidentFloat would return 0 or NaN → the assert flips RED.
  auto evalColorResident = [&](const char* outSlot) -> float {
    const NodeSpec* spec = findSpec("SampleGradient");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "SampleGradient";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    const int nid = g.nodes.back().id;
    g.node(nid)->params["SamplePos"] = injectBug ? -0.5f : 0.5f;
    g.node(nid)->params["OverrideInterpolation"] = 0.0f;
    g.node(nid)->params["Interpolation"] = 0.0f;
    // PRODUCTION chain (perlinnoise3 shape): flat Graph → SymbolLibrary → resident eval graph →
    // evalResidentFloat. Path == node id as string (libFromGraph contract: child id == node id).
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
    return evalResidentFloat(rg, std::to_string(nid), outSlot, rc);
  };

  auto check = [&](const char* tag, float got, float want) {
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    std::printf("[selftest-samplegradient] %s got=%.9f want=%.9f -> %s\n",
                tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A: SamplePos=0.5, default 2-stop Linear gradient → hand-computed ~gray.
  // want is FIXED at the true (non-bug) TiXL value — it does NOT flip with injectBug. This is the
  // load-bearing RED tooth: under injectBug the helper negates SamplePos → sample(-0.5) clamps to 0 →
  // returns stop0.color ≈ (1e-6, 1e-6, 1e-6, 1.0) ≈ ~black, while want stays ~gray (0.5). The R/G/B
  // asserts then diverge by ~0.5 ≫ eps → FAIL. (The earlier version flipped want WITH got — got≈want
  // in both branches → the golden was GREEN even when bugged, the "flip the expected value" anti-pattern
  // the verify discipline forbids. Fixing want to the constant true value restores the bite.)
  // true want = (0.500000500, 0.499995500, 0.5000005, 1.0)
  const float wantR = 0.500000500f;
  const float wantG = 0.499995500f;
  const float wantB = 0.5000005f;
  const float wantA = 1.0f;  // A=1 (stop0.A=1, stop1.A=1, lerp=1); under bug stop0.A is also 1 → A
                             // does NOT bite — the R/G/B asserts carry the RED proof.

  // Flat path:
  const float fR = evalColor("Color.x");
  const float fG = evalColor("Color.y");
  const float fB = evalColor("Color.z");
  const float fA = evalColor("Color.w");
  check("A flat Color.R t=0.5", fR, wantR);
  check("A flat Color.G t=0.5", fG, wantG);
  check("A flat Color.B t=0.5", fB, wantB);
  check("A flat Color.A t=0.5", fA, wantA);

  // RESIDENT-PATH leg (★ R-2 iron rule — proves the PRODUCTION gather, not just flat).
  // resident == flat == TiXL for the default-gradient coord. Assert both ==TiXL AND ==flat.
  const float rR = evalColorResident("Color.x");
  const float rG = evalColorResident("Color.y");
  const float rB = evalColorResident("Color.z");
  const float rA = evalColorResident("Color.w");
  check("A resident Color.R==TiXL",    rR, wantR);
  check("A resident Color.G==TiXL",    rG, wantG);
  check("A resident Color.B==TiXL",    rB, wantB);
  check("A resident Color.A==TiXL",    rA, wantA);
  check("A resident Color.R==flat",    rR, fR);
  check("A resident Color.G==flat",    rG, fG);
  check("A resident Color.B==flat",    rB, fB);
  check("A resident Color.A==flat",    rA, fA);

  // CASE B: OverrideInterpolation=true, Interpolation=Hold (1).
  // With Hold mode: fraction is computed but then returns previousStep.color (stop0 ≈ ~black), NOT lerp.
  // stop0.color = (9.9999e-7, 9.999968e-7, 1e-6, 1.0). Arithmetic:
  //   t=0.5, scan: step0.pos=0 < 0.5 → previousStep=step0; step1.pos=1 >= 0.5.
  //   overrideInterpolation=true → gradient.Interpolation = Hold (1).
  //   cs:142 Hold branch → return previousStep.color = stop0.color.
  //   → want = (9.9999e-7, 9.999968e-7, 1e-6, 1.0)
  // (Proves the OverrideInterpolation + Interpolation enum path is wired correctly.)
  if (!injectBug) {
    const NodeSpec* spec = findSpec("SampleGradient");
    if (spec) {
      auto evalCaseB = [&](const char* outName) -> float {
        Graph g2;
        Node nd2; nd2.id = g2.nextId++; nd2.type = "SampleGradient";
        for (const auto& p : spec->ports)
          if (p.isInput && p.dataType == "Float") nd2.params[p.id] = p.def;
        g2.nodes.push_back(nd2);
        const int nid2 = g2.nodes.back().id;
        g2.node(nid2)->params["SamplePos"] = 0.5f;
        g2.node(nid2)->params["OverrideInterpolation"] = 1.0f;  // true
        g2.node(nid2)->params["Interpolation"] = 1.0f;           // Hold
        int oi = -1;
        for (size_t i = 0; i < spec->ports.size(); ++i)
          if (spec->ports[i].id == outName) { oi = (int)i; break; }
        EvaluationContext ctx2{}; ctx2.time = 0.0f;
        return oi < 0 ? -997.0f : evalFloat(g2, pinId(nid2, oi), ctx2, 0);
      };
      check("B flat Color.R hold t=0.5 (OverrideInterp=true,Hold)", evalCaseB("Color.x"), 9.9999e-7f);
      check("B flat Color.G hold t=0.5", evalCaseB("Color.y"), 9.999968e-7f);
      check("B flat Color.B hold t=0.5", evalCaseB("Color.z"), 1e-6f);
      check("B flat Color.A hold t=0.5", evalCaseB("Color.w"), 1.0f);
    }
  }

  std::printf("[selftest-samplegradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
