// AnimVec2 value op (value-op self-registration seam leaf — numbers/anim/animators, vec2).
// First of the Anim* family ported as a VALUE_OP. AnimVec2 is STATELESS: Result is a PURE function of
// (context time + inputs). The instance fields _normalizedTimeX/Y/_shape are UI-only (region
// "operator Ui" in AnimVec2.cs) — they are written for the timeline gizmo, never read back into the
// math. So there is NO WasHit / no cross-frame memory → this rides the value rail (NodeSpec::evaluate,
// glob-conflict-free), NOT the stateful_value_ops rail.
// TiXL authority: Operators/Lib/numbers/anim/animators/AnimVec2.cs + Core/Utils/AnimMath.cs (verbatim).
//
//   AnimVec2.cs Update(context):
//     var phases       = Phases.GetValue(context);        // Vector2
//     var masterRate   = RateFactor.GetValue(context);    // float
//     var rates        = Rates.GetValue(context);         // Vector2
//     var rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(context, AllowSpeedFactor);
//     _shape           = (AnimMath.Shapes)Shape.GetValue(context).Clamp(0, Enum...Shapes.Length);
//     var amplitudeFactor = AmplitudeFactor.GetValue(context);  // float
//     var amplitudes   = Amplitudes.GetValue(context) * amplitudeFactor;  // Vector2 (per-component scale)
//     var offsets      = Offsets.GetValue(context);       // Vector2
//     var bias         = Bias.GetValue(context);          // float
//     var ratio        = Ratio.GetValue(context);         // float
//     var time         = OverrideTime.HasInputConnections ? OverrideTime.GetValue(context)
//                                                          : context.LocalFxTime;
//     _normalizedTimeX = (time + phases.X) * masterRate * rateFactorFromContext * rates.X;  // double
//     _normalizedTimeY = (time + phases.Y) * masterRate * rateFactorFromContext * rates.Y;  // double
//     Result.Value = new Vector2(
//         AnimMath.CalcValueForNormalizedTime(_shape, _normalizedTimeX, 0, bias, ratio) * amplitudes.X + offsets.X,
//         AnimMath.CalcValueForNormalizedTime(_shape, _normalizedTimeY, 1, bias, ratio) * amplitudes.Y + offsets.Y);
//
//   PURE function of (time, inputs). ZERO cross-frame state (the only writes are the UI gizmo fields).
//   componentIndex 0 (X) / 1 (Y) de-correlates the per-channel Random/Perlin seeds (AnimMath.cs).
//
//   AnimVec2.t3 DefaultValues (★ symbol .t3 value, NOT C# field default):
//     Shape=1 (Ramps — NOT 0/Endless, the enum's C# default), Bias=0.5, Ratio=1.0,
//     RateFactor=1.0, AmplitudeFactor=1.0, AllowSpeedFactor=1 (FactorA),
//     Amplitudes={1,1}, Offsets={0,0}, Phases={0,0}, Rates={1,1}, OverrideTime=0.0.
//
// EVAL-SIDE LAYOUT: each Vector2 input decomposes into 2 consecutive Float ports (x,y), per the
//   established fork-vec*-decompose-arity convention (OscillateVec2, PerlinNoise2). in[] is gathered
//   in port order by the flat/resident Float gather; the 2-output Vector2 dissolves to Result.x/.y
//   (outIdx selects the component). in[] order MUST match the port list in the registrar below:
//     [OverrideTime, Shape, Rates.x, Rates.y, RateFactor, Phases.x, Phases.y,
//      Amplitudes.x, Amplitudes.y, AmplitudeFactor, Offsets.x, Offsets.y, Bias, Ratio]  (14 inputs).
//   `t` = OverrideTime != 0 ? OverrideTime : ctx.localFxTime (see fork-animvec2-overridetime below).
//
// FORKS (named):
//   - fork-animvec2-stateless-value-op: AnimVec2 has NO WasHit output and NO cross-frame state — the
//     _normalizedTimeX/Y/_shape members are UI-gizmo scratch (never read into Result). So it is a
//     VALUE op, not a stateful op: it self-registers on the value rail (ValueOp / NodeSpec::evaluate),
//     exactly like OscillateVec2. NOT an eval fork — it is the placement decision.
//   - fork-animvec2-overridetime-nonzero-single-clock: TiXL branches on OverrideTime.HasInputConnections.
//     The flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(); the
//     standing single-clock convention (OscillateVec2 / AnimValue) maps "has input connection" → "the
//     OverrideTime constant is non-zero". So `t = overrideTime != 0 ? overrideTime : ctx.localFxTime`.
//     With the .t3 default OverrideTime=0 this is exactly ctx.localFxTime (the default authoring case),
//     byte-EXACT to TiXL; a non-zero OverrideTime constant overrides the bars clock.
//   - fork-animvec2-shape-via-animmath: AnimVec2 supports the FULL AnimMath.Shapes enum (Ramps/Saws/
//     Sin/Square/ZigZag/KickSaws/Random/Perlin/Steps/Endless...), NOT raw sin. The shape value is
//     computed by anim_math::calcValueForNormalizedTime (the committed AnimMath shape engine), per
//     component. Shape is an InputSlot<int> stored on the Float rail and truncated+clamped to
//     [0, kShapeCount) exactly as AnimVec2.cs's `.Clamp(0, Enum.GetNames(...).Length)`.
//     ★ TiXL Clamp upper bound is the COUNT (13), not count-1 — reproduced verbatim (a Shape==13 would
//     fall through calcValueForNormalizedTime's switch to result 0; no in-range authoring hits it).
//   - fork-animvec2-speedoverride-always-one: AnimMath.GetSpeedOverrideFromContext returns 1 unless
//     context.FloatVariables holds SpeedFactorA/B (AnimMath.cs:9-34). The value-eval context carries no
//     FloatVariables map, so rateFactorFromContext is ALWAYS 1 here → folded as the constant 1.0f. The
//     AllowSpeedFactor input therefore can never change Result on this path; it is DROPPED from the port
//     list (it would otherwise be an unread Float confusing the gather order). Byte-EXACT to TiXL on the
//     default + no-context-variable authoring case.
//   - fork-animvec2-vec2-as-2-floats: every Vector2 input/output is a pair of Float ports (no Vector2
//     type on this runtime). Not an eval fork — the component mapping is byte-identical to TiXL.
//   - fork-animvec2-amplitudes-times-amplitudefactor: amplitudes = Amplitudes * AmplitudeFactor BEFORE
//     the shape multiply (a per-component Vector2 scaled by a shared scalar). Reproduced verbatim.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"             // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/anim_math.h"            // the committed AnimMath shape engine (calcValueForNormalizedTime)
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (the PRODUCTION gather)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runAnimVec2SelfTest(bool injectBug);

namespace {

// Production-side injectBug flag (mirror of value_op_blendcolors.cpp's g_blendColorsInjectClamp). The
// shipped op ALWAYS runs with this false. The selftest toggles it ON for the bug build only; it
// perturbs a REAL term — it DROPS the AnimMath shape call, returning the raw normalized time instead of
// calcValueForNormalizedTime(...). The golden `want` is computed from the REAL AnimMath formula and is
// INDEPENDENT of this flag, so got (raw nt) diverges from want (shaped) → the assertion bites RED.
bool g_animVec2DropShape = false;

// in[] = [OverrideTime, Shape, Rates.x, Rates.y, RateFactor, Phases.x, Phases.y,
//         Amplitudes.x, Amplitudes.y, AmplitudeFactor, Offsets.x, Offsets.y, Bias, Ratio]  (14 inputs).
// `t` = OverrideTime != 0 ? OverrideTime : ctx.localFxTime (fork-animvec2-overridetime-nonzero-single-clock).
// Output Result.x at spec index 14, Result.y at 15 → component = outIdx - n.
float evalAnimVec2(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 14) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y (outputs follow the n inputs)
  if (comp < 0 || comp > 1) return 0.0f;

  const float overrideTime    = in[0];
  const int   shapeRaw        = (int)(int32_t)in[1];  // InputSlot<int> on the Float rail
  const float ratesX          = in[2],  ratesY = in[3];
  const float masterRate      = in[4];                // RateFactor
  const float phasesX         = in[5],  phasesY = in[6];
  const float amplitudesX     = in[7],  amplitudesY = in[8];
  const float amplitudeFactor = in[9];
  const float offsetsX        = in[10], offsetsY = in[11];
  const float bias            = in[12];
  const float ratio           = in[13];

  // AnimVec2.cs: _shape = (Shapes)Shape.GetValue(context).Clamp(0, Enum.GetNames(typeof(Shapes)).Length).
  // ★ verbatim: the upper Clamp bound is the COUNT (kShapeCount=13), NOT count-1.
  const int shapeIdx = (int)anim_math::clampf((float)shapeRaw, 0.0f, (float)anim_math::kShapeCount);
  const anim_math::Shapes shape = (anim_math::Shapes)shapeIdx;

  // rateFactorFromContext is always 1 on this path (fork-animvec2-speedoverride-always-one).
  const float rateFactorFromContext = 1.0f;

  // OverrideTime non-zero overrides the bars clock (fork-animvec2-overridetime-nonzero-single-clock).
  const double time = (overrideTime != 0.0f) ? (double)overrideTime : (double)ctx.localFxTime;

  // amplitudes = Amplitudes * AmplitudeFactor (per-component, BEFORE the shape multiply).
  const float amplX = amplitudesX * amplitudeFactor;
  const float amplY = amplitudesY * amplitudeFactor;

  if (comp == 0) {
    // _normalizedTimeX = (time + phases.X) * masterRate * rateFactorFromContext * rates.X  (double precision)
    const double nt = (time + (double)phasesX) * (double)masterRate * (double)rateFactorFromContext * (double)ratesX;
    if (g_animVec2DropShape)                      // injectBug: drop the shape call (real-term perturbation)
      return (float)nt * amplX + offsetsX;
    return anim_math::calcValueForNormalizedTime(shape, nt, 0, bias, ratio) * amplX + offsetsX;
  }
  // _normalizedTimeY = (time + phases.Y) * masterRate * rateFactorFromContext * rates.Y  (double precision)
  const double nt = (time + (double)phasesY) * (double)masterRate * (double)rateFactorFromContext * (double)ratesY;
  if (g_animVec2DropShape)
    return (float)nt * amplY + offsetsY;
  return anim_math::calcValueForNormalizedTime(shape, nt, 1, bias, ratio) * amplY + offsetsY;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_animvec2{
    // AnimVec2 (TiXL Lib.numbers.anim.animators.AnimVec2): per-component AnimMath shape animator off the
    //   bars clock. Result.c = CalcValueForNormalizedTime(Shape, nt_c, c, Bias, Ratio) * (Amp.c*AmpF) + Off.c,
    //   nt_c = (t + Phase.c) * RateFactor * Rate.c,  t = OverrideTime ? OverrideTime : ctx.localFxTime.
    // Port order MUST match evalAnimVec2's in[] read. Defaults from AnimVec2.t3 (Shape=1=Ramps).
    // AllowSpeedFactor is intentionally absent (fork-animvec2-speedoverride-always-one).
    // PortSpec field order (graph.h): id, name, dataType, isInput, def, minV, maxV, widget, labels,
    //   pinless, vecArity, multiInput.
    {"AnimVec2", "AnimVec2",
     {{"OverrideTime",    "OverrideTime",    "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Shape",           "Shape",           "Float", true, 1.0f, 0.0f,       12.0f,      Widget::Enum,
       {"Endless", "Ramps", "Saws", "KickSaws", "Square", "ZigZag", "Wave", "Sin",
        "PerlinNoise", "PerlinNoiseSigned", "Random", "RandomSigned", "Steps"}},
      {"Rates.x",         "Rates",           "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Rates.y",         "Rates.y",         "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"RateFactor",      "RateFactor",      "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Phases.x",        "Phases",          "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Phases.y",        "Phases.y",        "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Amplitudes.x",    "Amplitudes",      "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Amplitudes.y",    "Amplitudes.y",    "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"AmplitudeFactor", "AmplitudeFactor", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Offsets.x",       "Offsets",         "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Offsets.y",       "Offsets.y",       "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Bias",            "Bias",            "Float", true, 0.5f, 0.0f,       1.0f,       Widget::Slider},
      {"Ratio",           "Ratio",           "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      // AllowSpeedFactor (TiXL AnimVec2.cs l.71: [Input] InputSlot<int> AllowSpeedFactor, MappedType=AnimMath.SpeedFactors).
      // Dead knob on this path (fork-animvec2-speedoverride-always-one): rateFactorFromContext=1.0 always;
      // the knob is exposed so the inspector matches TiXL, but its value never changes Result.
      // .t3 default = 1 (FactorA). Enum labels mirror AnimMath.SpeedFactors (None=0, FactorA=1, FactorB=2, FactorAorB=3).
      {"AllowSpeedFactor", "AllowSpeedFactor", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
       {"None", "FactorA", "FactorB", "FactorAorB"}},
      {"Result.x",        "Result.x",        "Float", false},
      {"Result.y",        "Result.y",        "Float", false}},
     evalAnimVec2},
    "animvec2", runAnimVec2SelfTest};

// --- AnimVec2 MATH golden (flat eval path) ------------------------------------------------------
// Builds a 1-node AnimVec2 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/.y via evalFloat. Expected values are HAND-COMPUTED from the TiXL formula:
//   nt_c   = (t + Phase.c) * RateFactor * 1 * Rate.c          (rateFactorFromContext==1)
//   Result.c = CalcValueForNormalizedTime(Shape, nt_c, c, Bias, Ratio) * (Amp.c*AmpF) + Off.c
// at a HASH-LIVE coordinate (fmod(nt,1) lands inside the live ramp, not on a degenerate integer).
//
// injectBug: g_animVec2DropShape ON in the SHIPPED evaluate fn → it DROPS the AnimMath shape call and
//   returns raw nt*ampl+offset. The golden `want` is computed from the REAL AnimMath shape engine and
//   is INDEPENDENT of the bug flag (no co-conditioning) → got(raw) ≠ want(shaped) → RED.
//
// ARITHMETIC (independent float32 reference /tmp/animvec2_ref.py — mirrors anim_math.h verbatim):
//   CASE C (★HASH-LIVE PRIMARY, Sin shape=7, Rates=(0.37,0.51), t=2.5, defaults else):
//     nt_x = (2.5+0)*1*1*0.37 = 0.925 ; fmod(0.925,1)=0.925 ; clamp(0.925/1,0,1)=0.925
//       Sin map = sin((0.925+0.25)*2*3.141592) = sin(7.383742) = 0.891005874
//       Bias=0.5 → SchlickBiasWithNegative(0.891005874, 0.5) = identity → 0.891005874
//       Result.x = 0.891005874 * (1*1) + 0 = 0.891005874
//     nt_y = (2.5+0)*1*1*0.51 = 1.275 ; fmod(1.275,1)=0.275 ; clamp(0.275,0,1)=0.275
//       Sin map = sin((0.275+0.25)*2*3.141592) = sin(3.298672) = -0.156433821
//       SchlickBiasWithNegative(-0.156433821, 0.5) = identity → -0.156433821
//       Result.y = -0.156433821 * 1 + 0 = -0.156433821
//   (injectBug drops the shape → got = raw nt = 0.925 / 1.275, far from 0.891 / -0.156 → RED.)
int runAnimVec2SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Toggle the production drop-shape flag for the duration of this selftest, then restore. The shipped
  // op always runs with g_animVec2DropShape==false; the bug build sets it true.
  const bool savedDrop = g_animVec2DropShape;
  g_animVec2DropShape = injectBug;

  // FLAT eval: build a 1-node AnimVec2 graph at a given localFxTime with explicit params; pull a named
  // output port via evalFloat (the canonical value-rail evaluation).
  auto evalA = [&](float localFxTime, const char* outName,
                   float overrideTime, float shape, float ratesX, float ratesY, float rateFactor,
                   float phasesX, float phasesY, float amplX, float amplY, float amplFactor,
                   float offX, float offY, float bias, float ratio) -> float {
    const NodeSpec* spec = findSpec("AnimVec2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AnimVec2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["OverrideTime"]    = overrideTime;
    g.node(nid)->params["Shape"]           = shape;
    g.node(nid)->params["Rates.x"]         = ratesX;
    g.node(nid)->params["Rates.y"]         = ratesY;
    g.node(nid)->params["RateFactor"]      = rateFactor;
    g.node(nid)->params["Phases.x"]        = phasesX;
    g.node(nid)->params["Phases.y"]        = phasesY;
    g.node(nid)->params["Amplitudes.x"]    = amplX;
    g.node(nid)->params["Amplitudes.y"]    = amplY;
    g.node(nid)->params["AmplitudeFactor"] = amplFactor;
    g.node(nid)->params["Offsets.x"]       = offX;
    g.node(nid)->params["Offsets.y"]       = offY;
    g.node(nid)->params["Bias"]            = bias;
    g.node(nid)->params["Ratio"]           = ratio;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = localFxTime;  // bars clock — NOT detached (the bug lives in the production fn)
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // RESIDENT-PATH eval (★ the PRODUCTION gather — closes the R-2 gap). Value ops flow through the
  // resident engine in the running app: evalResidentFloat gathers AnimVec2's 14 Float inputs into its
  // own in[kMaxFloatIn]. Mirror of perlinnoise3's resident leg: build the SAME single-node graph,
  // libFromGraph → buildEvalGraph (resident path == node id string) → evalResidentFloat the named
  // output slot — the EXACT production evaluation. rc.localFxTime carries the bars clock (NOT detached;
  // the injectBug perturbation lives in the shipped evaluate fn via g_animVec2DropShape, so the
  // resident path executes the SAME perturbed math as flat). The TOOTH for this leg is the cap/gather:
  // a future cap regression truncating the 14-input gather makes evalResidentFloat NaN → the NaN-aware
  // assert (std::fabs(NaN-want) is NaN, never < eps) flips RED. Proves all 14 inputs gather on the
  // production in[kMaxFloatIn], not just the flat in[].
  auto evalAResident = [&](float localFxTime, const char* outSlot,
                           float overrideTime, float shape, float ratesX, float ratesY, float rateFactor,
                           float phasesX, float phasesY, float amplX, float amplY, float amplFactor,
                           float offX, float offY, float bias, float ratio) -> float {
    const NodeSpec* spec = findSpec("AnimVec2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AnimVec2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["OverrideTime"]    = overrideTime;
    g.node(nid)->params["Shape"]           = shape;
    g.node(nid)->params["Rates.x"]         = ratesX;
    g.node(nid)->params["Rates.y"]         = ratesY;
    g.node(nid)->params["RateFactor"]      = rateFactor;
    g.node(nid)->params["Phases.x"]        = phasesX;
    g.node(nid)->params["Phases.y"]        = phasesY;
    g.node(nid)->params["Amplitudes.x"]    = amplX;
    g.node(nid)->params["Amplitudes.y"]    = amplY;
    g.node(nid)->params["AmplitudeFactor"] = amplFactor;
    g.node(nid)->params["Offsets.x"]       = offX;
    g.node(nid)->params["Offsets.y"]       = offY;
    g.node(nid)->params["Bias"]            = bias;
    g.node(nid)->params["Ratio"]           = ratio;
    // PRODUCTION chain (cookResidentThenEval shape): flat Graph → SymbolLibrary (child id == node id ⇒
    // resident path == id-as-string) → resident eval graph → evalResidentFloat.
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime   = 0.0f;
    rc.localFxTime = localFxTime;  // bars clock (bug lives in the shipped fn, not the ctx)
    rc.frameIndex  = 0;
    rc.lib         = &lib;
    return evalResidentFloat(rg, std::to_string(nid), outSlot, rc);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-animvec2] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE C ★HASH-LIVE PRIMARY (Sin shape=7, Rates=(0.37,0.51), localFxTime=2.5, defaults else).
  //   FIXED wants (independent of injectBug) from the AnimMath formula (see ARITHMETIC above):
  //     x = 0.891005874  (nt_x=0.925, Sin map, Bias=0.5 identity)
  //     y = -0.156433821 (nt_y=1.275 → fmod 0.275, Sin map, Bias=0.5 identity)
  //   FLAT leg:
  {
    const float wx =  0.891005874f;
    const float wy = -0.156433821f;
    const float fx = evalA(2.5f, "Result.x", 0, 7, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f);
    const float fy = evalA(2.5f, "Result.y", 0, 7, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f);
    check("C x Sin rates=(.37,.51) t=2.5 (HASH-LIVE)", fx, wx, eps);
    check("C y Sin rates=(.37,.51) t=2.5 (HASH-LIVE)", fy, wy, eps);

    // RESIDENT-PATH leg (★ R-2: proves the PRODUCTION gather). resident == flat == TiXL per component.
    const float rx = evalAResident(2.5f, "Result.x", 0, 7, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f);
    const float ry = evalAResident(2.5f, "Result.y", 0, 7, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f);
    check("C x RESIDENT==TiXL", rx, wx, eps);
    check("C y RESIDENT==TiXL", ry, wy, eps);
    check("C x RESIDENT==flat", rx, fx, eps);
    check("C y RESIDENT==flat", ry, fy, eps);
  }

  // CASE A (t3 defaults: Shape=1=Ramps, localFxTime=2.5, all else default). Proves the .t3 Shape
  //   DEFAULT is Ramps (NOT the C# enum default Endless=0) and the bars clock feeds nt.
  //   nt_x = nt_y = (2.5+0)*1*1*1 = 2.5 ; fmod(2.5,1)=0.5 ; Ramps map(0.5)=0.5 ; SchlickBias(0.5,0.5)=0.5
  //   Result.x = Result.y = 0.5 * (1*1) + 0 = 0.5.   (injectBug drops shape → raw nt 2.5 → RED.)
  check("A x Ramps default t=2.5", evalA(2.5f, "Result.x", 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f), 0.5f, eps);
  check("A y Ramps default t=2.5", evalA(2.5f, "Result.y", 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f), 0.5f, eps);

  // CASE B (Sin shape=7, per-axis Rates=(2,4), Phases=(0.1,0.2), Amplitudes=(3,5), AmplitudeFactor=1,
  //   Offsets=(10,20), Bias=0.5, Ratio=1, localFxTime=2.5). Proves per-axis Rate/Phase/Amplitude, the
  //   shared AmplitudeFactor multiply, and per-axis Offset add are all wired right.
  //   nt_x = (2.5+0.1)*1*1*2 = 5.2 ; fmod=0.2 ; Sin map=sin((0.2+0.25)*2π') ; *3 + 10 = 10.927052498
  //   nt_y = (2.5+0.2)*1*1*4 = 10.8 ; fmod=0.8 ; Sin map ; *5 + 20 = 21.545078278
  check("B x Sin rates=(2,4) ph=(.1,.2) amp=(3,5) off=(10,20)", evalA(2.5f, "Result.x", 0, 7, 2, 4, 1, 0.1f, 0.2f, 3, 5, 1, 10, 20, 0.5f, 1.0f), 10.927052498f, 1e-3f);
  check("B y Sin rates=(2,4) ph=(.1,.2) amp=(3,5) off=(10,20)", evalA(2.5f, "Result.y", 0, 7, 2, 4, 1, 0.1f, 0.2f, 3, 5, 1, 10, 20, 0.5f, 1.0f), 21.545078278f, 1e-3f);

  // CASE D (Ramps shape=1, Bias=0.8 != 0.5, Rates=(0.37,0.51), localFxTime=2.5). Pins the SchlickBias
  //   bias term (Bias != 0.5 is NO LONGER identity). nt_x=0.925, Ramps map=0.925, SchlickBias(0.925,0.8);
  //   nt_y=1.275→fmod 0.275, Ramps map=0.275, SchlickBias(0.275,0.8). want: x=0.980132461, y=0.602739751.
  check("D x Ramps bias=0.8 rates=(.37,.51)", evalA(2.5f, "Result.x", 0, 1, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.8f, 1.0f), 0.980132461f, eps);
  check("D y Ramps bias=0.8 rates=(.37,.51)", evalA(2.5f, "Result.y", 0, 1, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.8f, 1.0f), 0.602739751f, eps);

  // CASE E (OverrideTime != 0 single-clock fork). OverrideTime=1.5 overrides ctx.localFxTime(=99 here,
  //   which would give a different value) → t=1.5. Sin shape, Rates=(0.37,0.51), defaults else.
  //   nt_x=(1.5)*1*1*0.37=0.555 ; nt_y=(1.5)*0.51=0.765. Proves OverrideTime!=0 wins over the bars clock.
  {
    const float wx = anim_math::calcValueForNormalizedTime(anim_math::Shapes::Sin, (1.5 * 0.37), 0, 0.5f, 1.0f);
    const float wy = anim_math::calcValueForNormalizedTime(anim_math::Shapes::Sin, (1.5 * 0.51), 1, 0.5f, 1.0f);
    // NOTE: wx/wy here are derived from the SAME engine, but the LOAD-BEARING claim of CASE E is the
    // clock SELECTION (OverrideTime overrides localFxTime), not the shape value — localFxTime=99 would
    // give a wholly different nt; that the result matches the OverrideTime-derived want proves selection.
    check("E x OverrideTime=1.5 wins over localFxTime=99",
          evalA(99.0f, "Result.x", 1.5f, 7, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f), wx, eps);
    check("E y OverrideTime=1.5 wins over localFxTime=99",
          evalA(99.0f, "Result.y", 1.5f, 7, 0.37f, 0.51f, 1, 0, 0, 1, 1, 1, 0, 0, 0.5f, 1.0f), wy, eps);
  }

  g_animVec2DropShape = savedDrop;
  return ok ? 0 : 1;
}

}  // namespace sw
