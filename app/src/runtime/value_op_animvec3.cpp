// AnimVec3 value op (value-op self-registration seam leaf — numbers/anim/animators). 3-channel
// twin of AnimValue (node_registry_math.cpp / stateful_value_ops.cpp): per-component AnimMath shape
// animator on the bars clock, driven by Vec3 Rates/Phases/Amplitudes/Offsets + scalar
// RateFactor/AmplitudeFactor/Bias/Ratio/Shape. 3 Float out ports (Result.x/.y/.z = Vector3).
// TiXL authority: Operators/Lib/numbers/anim/animators/AnimVec3.cs + AnimVec3.t3 (verbatim below).
//
//   AnimVec3.cs Update(context):   (VERBATIM)
//     var phases               = Phases.GetValue(context);          // Vector3
//     var masterRate           = RateFactor.GetValue(context);      // float (scalar)
//     var rates                = Rates.GetValue(context);           // Vector3
//     var ratio                = Ratio.GetValue(context);           // float
//     var rateFactorFromContext= AnimMath.GetSpeedOverrideFromContext(context, AllowSpeedFactor);
//     _shape = (AnimMath.Shapes)Shape.GetValue(context).Clamp(0, Enum.GetNames(typeof(Shapes)).Length);
//     var amplitudeFactor      = AmplitudeFactor.GetValue(context); // float (scalar)
//     var amplitudes           = Amplitudes.GetValue(context) * amplitudeFactor;  // Vector3 * float
//     var offsets              = Offsets.GetValue(context);         // Vector3
//     var bias                 = Bias.GetValue(context);            // float
//     var time = OverrideTime.HasInputConnections ? OverrideTime.GetValue(context)
//                                                 : context.LocalFxTime;
//     // Don't use vector to keep double precision:
//     _normalizedTimeX = (time + phases.X) * masterRate * rateFactorFromContext * rates.X;
//     _normalizedTimeY = (time + phases.Y) * masterRate * rateFactorFromContext * rates.Y;
//     _normalizedTimeZ = (time + phases.Z) * masterRate * rateFactorFromContext * rates.Z;
//     Result.Value = new Vector3(
//       AnimMath.CalcValueForNormalizedTime(_shape,_normalizedTimeX,0,bias,ratio)*amplitudes.X+offsets.X,
//       AnimMath.CalcValueForNormalizedTime(_shape,_normalizedTimeY,1,bias,ratio)*amplitudes.Y+offsets.Y,
//       AnimMath.CalcValueForNormalizedTime(_shape,_normalizedTimeZ,2,bias,ratio)*amplitudes.Z+offsets.Z);
//
//   ★STATELESS — this is a VALUE_OP, NOT a stateful op (unlike AnimValue). The _normalizedTimeX/Y/Z
//   and _shape fields are public-but-UI-ONLY (the curve editor reads them to draw the preview); the
//   Result OUTPUT is a PURE function of (time + the input ports). There is NO WasHit, no `+=`, no read
//   of a previous-frame value — nothing persists across frames. Confirmed against the .cs: every term
//   comes from `*.GetValue(context)` or `context.LocalFxTime`. So AnimVec3 rides the VALUE rail
//   (NodeSpec::evaluate, glob-conflict-free), not the stateful_value_ops cook seam. (AnimValue is
//   stateful only because of its cross-frame WasHit integer tooth; AnimVec3 has no such output.)
//
//   ★PER-OP MATH DIVERGENCE vs AnimValue (do NOT copy AnimValue's term order): AnimVec3 folds Phase
//   INTO time BEFORE the rate multiply — `normalizedTime = (time + phase) * masterRate * rfc * rate`.
//   AnimValue does `normalizedTime = time * rfc * rate + phase` (phase added AFTER, no masterRate).
//   AnimVec3 additionally has masterRate (RateFactor scalar) AND per-component rates (Rates Vec3), and
//   amplitudes = Amplitudes(Vec3) * amplitudeFactor(scalar). Reproduced VERBATIM below per component.
//
//   AnimVec3.t3 DefaultValues (mirrored into the PortSpec below):
//     Shape          = 1   (= Ramps; the .t3 selector VALUE, NOT the C# field default Endless=0)
//     Rates          = (1, 1, 1)
//     RateFactor     = 1
//     Phases         = (0, 0, 0)
//     Amplitudes     = (1, 1, 1)
//     AmplitudeFactor= 1
//     Offsets        = (0, 0, 0)
//     Bias           = 0.5
//     Ratio          = 1
//     AllowSpeedFactor = 1   (= FactorA)
//     OverrideTime   = 0   (DROPPED port — see fork-animvec3-overridetime-always-localfxtime)
//
// EVAL-SIDE LAYOUT (single-wire path — NO multiInput; the simple Graph/Node/evalFloat golden like
//   value_op_oscillatevec3.cpp / value_op_perlinnoise3.cpp, NOT the resident multiInput gather):
//   each Vector3 input decomposes into 3 consecutive Float ports (x,y,z) per the established
//   fork-vec*-decompose-arity convention. in[] is gathered in port order. Port/in[] layout:
//     in[0]  = Shape            (enum-on-Float; clamped + truncated)
//     in[1]  = Rates.x   in[2] = Rates.y   in[3] = Rates.z
//     in[4]  = RateFactor       (masterRate, scalar)
//     in[5]  = Phases.x  in[6] = Phases.y  in[7] = Phases.z
//     in[8]  = Amplitudes.x in[9]= Amplitudes.y in[10]= Amplitudes.z
//     in[11] = AmplitudeFactor  (scalar, multiplies all 3 amplitudes)
//     in[12] = Offsets.x in[13]= Offsets.y in[14]= Offsets.z
//     in[15] = Bias             (scalar)
//     in[16] = Ratio            (scalar)
//     in[17] = AllowSpeedFactor (enum-on-Float)                       (n = 18 inputs)
//   Output ports Result.x/.y/.z follow the 18 inputs at spec indices 18/19/20 → comp = outIdx - n.
//   time = ctx.localFxTime (bars; fork-animvec3-overridetime-always-localfxtime).
//
// FORKS (named):
//   - fork-animvec3-stateless-value-rail: AnimVec3 is pure-time (no WasHit) ⇒ a VALUE op on the
//     NodeSpec::evaluate rail, NOT the stateful cook seam where AnimValue lives. The _normalizedTime*
//     fields are UI-preview-only (curve editor) and have no eval effect — dropped. Not a math fork.
//   - fork-animvec3-overridetime-always-localfxtime: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). This
//     port implements the NORMAL case (OverrideTime UNWIRED) VERBATIM: time = ctx.localFxTime. The
//     OverrideTime input is therefore DROPPED from the port list (an unread Float would otherwise
//     corrupt the gather order). Byte-EXACT to TiXL whenever OverrideTime is unwired (default authoring
//     case). Same fork as OscillateVec3 / PerlinNoise3 / the AnimValue single-clock fork.
//   - fork-animvec3-speedfactor-from-context-var: AnimMath.GetSpeedOverrideFromContext reads
//     context.FloatVariables["SpeedFactorA"/"B"] when AllowSpeedFactor != None, defaulting to 1 on a
//     miss. The value-eval EvaluationContext has no FloatVariables map (the context-var YELLOW seam is
//     not wired into the flat/resident value rail), so rateFactorFromContext is ALWAYS 1 here — exactly
//     the TryGetValue-MISS branch (byte-exact when no SpeedFactorA/B var is set, the default case). The
//     AllowSpeedFactor port is retained (enum, clamped) for parity even though every value selects 1.
//     Same fork as AnimValue (its SpeedFactor seam defaults to 1 absent a ContextVarMap entry).
//   - fork-animvec3-shape-enum-on-float-port: Shape/AllowSpeedFactor are InputSlot<int> with
//     MappedType enums in TiXL; this runtime stores them as Float and truncates (std::lround) before
//     use (whole-number Widget::Enum selectors are byte-identical to TiXL). Shape is clamped to
//     [0, kShapeCount-1]; AllowSpeedFactor to [0,2]. (TiXL's Shape.Clamp(0, len) over-permits len=13,
//     but a Shape selector is always a valid 0..12 enum value, so [0,12] clamp is the live behaviour.)
//   - fork-animvec3-vec3-as-3-floats: every Vector3 input/output is 3 Float ports (no Vector3 type on
//     this runtime). Not an eval fork — the component mapping is byte-identical to TiXL. Same
//     convention as OscillateVec3 / PerlinNoise3 / BlendVector3.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"             // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/anim_math.h"            // AnimMath shape engine (calcValueForNormalizedTime) — the
                                          // PURE per-component shape value, shared by the Anim* family.
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (the PRODUCTION gather)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runAnimVec3SelfTest(bool injectBug);

namespace {

// in[] = [Shape, Rates.x/y/z, RateFactor, Phases.x/y/z, Amplitudes.x/y/z, AmplitudeFactor,
//         Offsets.x/y/z, Bias, Ratio, AllowSpeedFactor]   (18 inputs).
// time = ctx.localFxTime (fork-animvec3-overridetime-always-localfxtime).
// rateFactorFromContext = 1 (fork-animvec3-speedfactor-from-context-var; no FloatVariables on rail).
// Per component c:
//   normalizedTime = (time + phases[c]) * masterRate * 1 * rates[c]
//   Result[c]      = CalcValueForNormalizedTime(shape, normalizedTime, c, bias, ratio)*amplitudes[c]+offsets[c]
//                    where amplitudes[c] = Amplitudes[c] * amplitudeFactor.
float evalAnimVec3(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 18) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y, 2 = Result.z (outputs follow n inputs)
  if (comp < 0 || comp > 2) return 0.0f;

  // Shape enum (.t3 default 1=Ramps); clamp to [0, count-1] like TiXL Shape.GetValue.Clamp.
  int shapeIdx = (int)std::lround(in[0]);
  if (shapeIdx < 0) shapeIdx = 0;
  else if (shapeIdx > anim_math::kShapeCount - 1) shapeIdx = anim_math::kShapeCount - 1;
  const anim_math::Shapes shape = (anim_math::Shapes)shapeIdx;

  const float rates[3]      = {in[1], in[2], in[3]};
  const float masterRate    = in[4];                       // RateFactor (scalar)
  const float phases[3]     = {in[5], in[6], in[7]};
  const float amps0[3]      = {in[8], in[9], in[10]};      // Amplitudes (Vec3, pre-factor)
  const float amplitudeF    = in[11];                      // AmplitudeFactor (scalar)
  const float offsets[3]    = {in[12], in[13], in[14]};
  const float bias          = in[15];
  const float ratio         = in[16];
  // in[17] = AllowSpeedFactor enum: every value resolves to rfc=1 on the value rail (no FloatVariables
  // map). Retained for parity; clamp is documented in the fork. (Read so the gather stays aligned.)
  (void)in[17];

  // rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(AllowSpeedFactor):
  //   None→1; FactorA→FloatVariables["SpeedFactorA"] (MISS→1); FactorB→"SpeedFactorB" (MISS→1).
  // The value-eval rail has no FloatVariables → always the MISS branch → 1.
  const double rateFactorFromContext = 1.0;

  // OverrideTime unwired → time = ctx.localFxTime (bars).
  const double time = (double)ctx.localFxTime;

  // amplitudes = Amplitudes * amplitudeFactor (per component).
  const float amplitude = amps0[comp] * amplitudeF;

  // _normalizedTime[c] = (time + phases[c]) * masterRate * rateFactorFromContext * rates[c]
  // (double precision kept, mirroring TiXL's "Don't use vector to keep double precision").
  const double normalizedTime =
      ((double)time + (double)phases[comp]) * (double)masterRate * rateFactorFromContext * (double)rates[comp];

  return anim_math::calcValueForNormalizedTime(shape, normalizedTime, comp, bias, ratio) * amplitude +
         offsets[comp];
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_animvec3{
    // AnimVec3 (TiXL Lib.numbers.anim.animators.AnimVec3): per-component AnimMath shape animator on the
    // bars clock. Port order MUST match evalAnimVec3's in[] read. Defaults from AnimVec3.t3.
    // OverrideTime is intentionally absent (fork-animvec3-overridetime-always-localfxtime).
    // Shape/AllowSpeedFactor are Widget::Enum selectors (AnimValue precedent).
    {"AnimVec3", "AnimVec3",
     {{"Shape",           "Shape",           "Float", true, 1.0f, 0.0f, 12.0f, Widget::Enum,
       {"Endless", "Ramps", "Saws", "KickSaws", "Square", "ZigZag", "Wave", "Sin",
        "PerlinNoise", "PerlinNoiseSigned", "Random", "RandomSigned", "Steps"}},
      {"Rates.x",         "Rates",           "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Rates.y",         "Rates.y",         "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Rates.z",         "Rates.z",         "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"RateFactor",      "RateFactor",      "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Phases.x",        "Phases",          "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Phases.y",        "Phases.y",        "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Phases.z",        "Phases.z",        "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Amplitudes.x",    "Amplitudes",      "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Amplitudes.y",    "Amplitudes.y",    "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Amplitudes.z",    "Amplitudes.z",    "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"AmplitudeFactor", "AmplitudeFactor", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Offsets.x",       "Offsets",         "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Offsets.y",       "Offsets.y",       "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Offsets.z",       "Offsets.z",       "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Bias",            "Bias",            "Float", true, 0.5f, 0.0001f, 1.0f, Widget::Slider},
      {"Ratio",           "Ratio",           "Float", true, 1.0f, 0.0001f, 100000.0f, Widget::Slider},
      {"AllowSpeedFactor","AllowSpeedFactor","Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
       {"None", "FactorA", "FactorB"}},
      {"Result.x",        "Result.x",        "Float", false},
      {"Result.y",        "Result.y",        "Float", false},
      {"Result.z",        "Result.z",        "Float", false}},
     evalAnimVec3},
    "animvec3", runAnimVec3SelfTest};

// --- AnimVec3 MATH golden -----------------------------------------------------------------------
// Builds a 1-node AnimVec3 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/.y/.z via BOTH the flat path (evalFloat) AND the PRODUCTION resident path (libFromGraph →
// buildEvalGraph → evalResidentFloat — mirror of value_op_perlinnoise3.cpp's resident leg, closing
// the R-2 flat-only gap). Expected values are HAND-COMPUTED from the TiXL formula (arithmetic shown
// in each case), reproduced by an INDEPENDENT float32 reference (/tmp/animvec3_ref2.py in the build
// dossier) — NOT self-referential to this file's helpers.
//
// injectBug DETACHES the bars clock: production evaluate() reads ctx.localFxTime, but under injectBug
// the golden feeds ctx.localFxTime = 0 (and rc.localFxTime = 0 on the resident leg). The FIXED wants
// below are the LIVE (clock-running) values and are INDEPENDENT of injectBug — when the bug fires,
// got (clock=0) ≠ want (clock=live) → RED → exit 1. The two clock points are deliberately chosen so
// fmod(live·rate,1) ≠ fmod(0·...,1) for EVERY component (non-integer rates), so the divergence is
// real (a degenerate Sin-periodicity choice would let time=0 alias the live value and NOT bite).
int runAnimVec3SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // FLAT eval (evalFloat) of AnimVec3 at a given localFxTime with explicit params, named output port.
  auto evalAV = [&](float localFxTime, const char* outName,
                    float shape,
                    float rateX, float rateY, float rateZ, float rateFactor,
                    float phX, float phY, float phZ,
                    float ampX, float ampY, float ampZ, float ampFactor,
                    float offX, float offY, float offZ,
                    float bias, float ratio, float allowSpeed) -> float {
    const NodeSpec* spec = findSpec("AnimVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Shape"]            = shape;
    g.node(nid)->params["Rates.x"]          = rateX;
    g.node(nid)->params["Rates.y"]          = rateY;
    g.node(nid)->params["Rates.z"]          = rateZ;
    g.node(nid)->params["RateFactor"]       = rateFactor;
    g.node(nid)->params["Phases.x"]         = phX;
    g.node(nid)->params["Phases.y"]         = phY;
    g.node(nid)->params["Phases.z"]         = phZ;
    g.node(nid)->params["Amplitudes.x"]     = ampX;
    g.node(nid)->params["Amplitudes.y"]     = ampY;
    g.node(nid)->params["Amplitudes.z"]     = ampZ;
    g.node(nid)->params["AmplitudeFactor"]  = ampFactor;
    g.node(nid)->params["Offsets.x"]        = offX;
    g.node(nid)->params["Offsets.y"]        = offY;
    g.node(nid)->params["Offsets.z"]        = offZ;
    g.node(nid)->params["Bias"]             = bias;
    g.node(nid)->params["Ratio"]            = ratio;
    g.node(nid)->params["AllowSpeedFactor"] = allowSpeed;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong shape value
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // RESIDENT-PATH eval (★ the PRODUCTION gather — value ops flow through the resident engine in the
  // running app. evalResidentFloat gathers AnimVec3's 18 Float inputs into its own in[kMaxFloatIn]
  // array. Mirror of value_op_perlinnoise3.cpp's resident leg / list_routing_golden's
  // cookResidentThenEval: build the SAME single-node graph, libFromGraph → buildEvalGraph (resident
  // path == node id string) → evalResidentFloat the named output slot. rc.localFxTime is detached
  // under injectBug (mirror of the flat leg) so the two paths stay in lock-step. The TOOTH that
  // matters for THIS leg is the cap/gather: if a future cap regression truncated the 18-input gather,
  // evalResidentFloat would NaN (the loud over-cap guard) → the NaN-aware assert flips RED.
  auto evalAVResident = [&](float localFxTime, const char* outSlot,
                            float shape,
                            float rateX, float rateY, float rateZ, float rateFactor,
                            float phX, float phY, float phZ,
                            float ampX, float ampY, float ampZ, float ampFactor,
                            float offX, float offY, float offZ,
                            float bias, float ratio, float allowSpeed) -> float {
    const NodeSpec* spec = findSpec("AnimVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Shape"]            = shape;
    g.node(nid)->params["Rates.x"]          = rateX;
    g.node(nid)->params["Rates.y"]          = rateY;
    g.node(nid)->params["Rates.z"]          = rateZ;
    g.node(nid)->params["RateFactor"]       = rateFactor;
    g.node(nid)->params["Phases.x"]         = phX;
    g.node(nid)->params["Phases.y"]         = phY;
    g.node(nid)->params["Phases.z"]         = phZ;
    g.node(nid)->params["Amplitudes.x"]     = ampX;
    g.node(nid)->params["Amplitudes.y"]     = ampY;
    g.node(nid)->params["Amplitudes.z"]     = ampZ;
    g.node(nid)->params["AmplitudeFactor"]  = ampFactor;
    g.node(nid)->params["Offsets.x"]        = offX;
    g.node(nid)->params["Offsets.y"]        = offY;
    g.node(nid)->params["Offsets.z"]        = offZ;
    g.node(nid)->params["Bias"]             = bias;
    g.node(nid)->params["Ratio"]            = ratio;
    g.node(nid)->params["AllowSpeedFactor"] = allowSpeed;
    // PRODUCTION chain (list_routing_golden's cookResidentThenEval shape): flat Graph → SymbolLibrary
    // (child id == node id ⇒ resident path == id-as-string) → resident eval graph → evalResidentFloat.
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime   = 0.0f;
    rc.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock (mirror of flat leg)
    rc.frameIndex  = 0;
    rc.lib         = &lib;
    return evalResidentFloat(rg, std::to_string(nid), outSlot, rc);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;  // NaN-aware: fabs(NaN-want) is NaN, never < e → RED
    ok = ok && pass;
    printf("[selftest-animvec3] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A ★HASH-LIVE PRIMARY (Shape=Sin(7), localFxTime=2.0 bars):
  //   RateFactor(masterRate)=1.3, Rates=(0.7,1.1,1.9), Phases=(0.1,0.2,0.3), Ratio=1, Bias=0.5,
  //   Amplitudes=(2,3,4)·AmplitudeFactor(1.5) = (3,4.5,6), Offsets=(10,20,30), AllowSpeedFactor=FactorA.
  //   rateFactorFromContext = 1 (no FloatVariables on the value rail).
  //   normalizedTime[c] = (2.0 + phase[c]) * 1.3 * 1 * rate[c]:
  //     x: (2.0+0.1)*1.3*0.7 = 1.911     ; Sin: schlickBiasNeg(sin((fmod(1.911,1)+0.25)*2π·),0.5)=0.847677171
  //        Result.x = 0.847677171*3  + 10 = 12.543031692
  //     y: (2.0+0.2)*1.3*1.1 = 3.146     ; Sin shape = 0.607930720 ; *4.5 + 20 = 22.735689163
  //     z: (2.0+0.3)*1.3*1.9 = 5.681     ; Sin shape = -0.420086861; *6   + 30 = 27.479478836
  //   (Sin shape := schlickBiasWithNegative(mapShape(7, clamp(fmod(nt,1)/ratio,0,1)), bias);
  //    mapShape(7,f)=sin((f+0.25)*2*3.141592). Independent float32 reference reproduces these.)
  //   injectBug → ctx.localFxTime=0 → nt=(0+phase)*1.3*rate → DIFFERENT fmod → wrong Sin:
  //     x got≈12.5228, y got≈18.9908, z got≈29.6609  ≠ wants → A flips RED (non-integer rates ensure
  //     the time=0 point does NOT alias the live point — the divergence is real, not tautological).
  {
    const char* tag = "A Shape=Sin t=2.0 (HASH-LIVE)";
    const float wx = 12.543031692f, wy = 22.735689163f, wz = 27.479478836f;
    // FLAT leg.
    const float fx = evalAV(2.0f, "Result.x", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float fy = evalAV(2.0f, "Result.y", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float fz = evalAV(2.0f, "Result.z", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    check("A x FLAT==TiXL", fx, wx, eps);
    check("A y FLAT==TiXL", fy, wy, eps);
    check("A z FLAT==TiXL", fz, wz, eps);
    (void)tag;
    // RESIDENT leg (★ PRODUCTION gather, closes the R-2 gap): resident == flat == TiXL for each comp.
    const float rx = evalAVResident(2.0f, "Result.x", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float ry = evalAVResident(2.0f, "Result.y", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float rz = evalAVResident(2.0f, "Result.z", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    check("A x RESIDENT==TiXL", rx, wx, eps);
    check("A y RESIDENT==TiXL", ry, wy, eps);
    check("A z RESIDENT==TiXL", rz, wz, eps);
    check("A x RESIDENT==flat", rx, fx, eps);
    check("A y RESIDENT==flat", ry, fy, eps);
    check("A z RESIDENT==flat", rz, fz, eps);
  }

  // CASE B (t3 DEFAULTS, Shape=Ramps(1), localFxTime=1.234 bars): proves every default is mirrored
  //   from AnimVec3.t3 (params pre-seeded from p.def, then NOT overridden). Defaults: RateFactor=1,
  //   Rates=(1,1,1), Phases=(0,0,0), Amplitudes=(1,1,1), AmplitudeFactor=1, Offsets=(0,0,0),
  //   Bias=0.5, Ratio=1, AllowSpeedFactor=FactorA(1) → rfc=1.
  //   normalizedTime[c] = (1.234 + 0)*1*1*1 = 1.234 for all c.
  //   Ramps shape = schlickBias(mapShape(1, clamp(fmod(1.234,1)/1, 0,1)), 0.5);  mapShape(1,f)=f.
  //     fmod(1.234f,1)=0.233999968 (float32) ; schlickBias(x,0.5)=x identity → 0.233999968.
  //   Result.x/y/z = 0.233999968*1 + 0 = 0.233999968 (all three identical at defaults).
  //   injectBug → ctx.localFxTime=0 → nt=0 → fmod(0,1)=0 → Ramps=0 → got=0 ≠ 0.234 → B flips RED.
  {
    auto evalDef = [&](const char* outName) -> float {
      const NodeSpec* spec = findSpec("AnimVec3");
      if (!spec) return -999.0f;
      Graph g;
      Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
      for (const auto& p : spec->ports)
        if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;  // pure t3 defaults
      g.nodes.push_back(nd);
      int nid = g.nodes.back().id;
      int outIdx = -1;
      for (size_t i = 0; i < spec->ports.size(); ++i)
        if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
      EvaluationContext ctx{};
      ctx.localFxTime = injectBug ? 0.0f : 1.234f;
      return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
    };
    // UNCONDITIONAL anchors (t3 defaults, localFxTime=1.234). fmod(1.234f,1)=0.233999968f (float32);
    // schlickBias(x,0.5) is the identity ⇒ Result = 0.233999968 (independent float32 reference).
    check("B x defaults (Ramps,Shape=1,Bias=0.5)", evalDef("Result.x"), 0.233999968f, eps);
    check("B y defaults (Amplitudes.y=1,Offsets.y=0)", evalDef("Result.y"), 0.233999968f, eps);
    check("B z defaults (RateFactor=1,Rates.z=1)", evalDef("Result.z"), 0.233999968f, eps);
  }

  return ok ? 0 : 1;
}

}  // namespace sw
