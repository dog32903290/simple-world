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
//     in[0]  = OverrideTime     (nonzero overrides the clock)
//     in[1]  = Shape            (enum-on-Float; clamped + truncated)
//     in[2]  = Rates.x   in[3] = Rates.y   in[4] = Rates.z
//     in[5]  = RateFactor       (masterRate, scalar)
//     in[6]  = Phases.x  in[7] = Phases.y  in[8] = Phases.z
//     in[9]  = Amplitudes.x in[10]= Amplitudes.y in[11]= Amplitudes.z
//     in[12] = AmplitudeFactor  (scalar, multiplies all 3 amplitudes)
//     in[13] = Offsets.x in[14]= Offsets.y in[15]= Offsets.z
//     in[16] = Bias             (scalar)
//     in[17] = Ratio            (scalar)
//     in[18] = AllowSpeedFactor (enum-on-Float)                       (n = 19 inputs)
//   Output ports Result.x/.y/.z follow the 19 inputs at spec indices 19/20/21 → comp = outIdx - n.
//   time = OverrideTime != 0 ? OverrideTime : ctx.localFxTime (bars; fork-animvec3-overridetime-nonzero-single-clock).
//
// FORKS (named):
//   - fork-animvec3-stateless-value-rail: AnimVec3 is pure-time (no WasHit) ⇒ a VALUE op on the
//     NodeSpec::evaluate rail, NOT the stateful cook seam where AnimValue lives. The _normalizedTime*
//     fields are UI-preview-only (curve editor) and have no eval effect — dropped. Not a math fork.
//   - fork-animvec3-overridetime-nonzero-single-clock: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). The
//     standing single-clock convention (AnimValue/AnimVec2) maps "has input connection" → "the
//     OverrideTime constant is non-zero": time = overrideTime != 0 ? overrideTime : ctx.localFxTime.
//     With the .t3 default OverrideTime=0 this is exactly ctx.localFxTime (the default authoring case),
//     byte-EXACT to TiXL; a non-zero OverrideTime constant overrides the bars clock. Diverges from TiXL
//     ONLY in the narrow "OverrideTime connected AND driven to exactly 0.0" case — unreachable without
//     owner-locked cook-core per-port connection plumbing. Same fork as AnimVec2 / PerlinNoise3.
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
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>

#include "runtime/Particle.h"             // EvaluationContext full definition (eval ctx + localFxTime)
#include "runtime/anim_math.h"            // AnimMath shape engine (calcValueForNormalizedTime) — the
                                          // PURE per-component shape value, shared by the Anim* family.
#include "runtime/value_op_registry.h"    // ValueOp self-registration
// (the heavy flat+resident self-test body lives in the sibling value_op_animvec3_golden.cpp)

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
  if (n < 19) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y, 2 = Result.z (outputs follow n inputs)
  if (comp < 0 || comp > 2) return 0.0f;

  const float overrideTime  = in[0];
  // Shape enum (.t3 default 1=Ramps); clamp to [0, count-1] like TiXL Shape.GetValue.Clamp.
  int shapeIdx = (int)std::lround(in[1]);
  if (shapeIdx < 0) shapeIdx = 0;
  else if (shapeIdx > anim_math::kShapeCount - 1) shapeIdx = anim_math::kShapeCount - 1;
  const anim_math::Shapes shape = (anim_math::Shapes)shapeIdx;

  const float rates[3]      = {in[2], in[3], in[4]};
  const float masterRate    = in[5];                       // RateFactor (scalar)
  const float phases[3]     = {in[6], in[7], in[8]};
  const float amps0[3]      = {in[9], in[10], in[11]};     // Amplitudes (Vec3, pre-factor)
  const float amplitudeF    = in[12];                      // AmplitudeFactor (scalar)
  const float offsets[3]    = {in[13], in[14], in[15]};
  const float bias          = in[16];
  const float ratio         = in[17];
  // in[18] = AllowSpeedFactor enum: every value resolves to rfc=1 on the value rail (no FloatVariables
  // map). Retained for parity; clamp is documented in the fork. (Read so the gather stays aligned.)
  (void)in[18];

  // rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(AllowSpeedFactor):
  //   None→1; FactorA→FloatVariables["SpeedFactorA"] (MISS→1); FactorB→"SpeedFactorB" (MISS→1).
  // The value-eval rail has no FloatVariables → always the MISS branch → 1.
  const double rateFactorFromContext = 1.0;

  // OverrideTime nonzero overrides the bars clock (fork-animvec3-overridetime-nonzero-single-clock).
  const double time = (overrideTime != 0.0f) ? (double)overrideTime : (double)ctx.localFxTime;

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
    // OverrideTime FIRST (TiXL [Input] order); nonzero overrides the clock (fork-animvec3-overridetime-nonzero-single-clock).
    // Shape/AllowSpeedFactor are Widget::Enum selectors (AnimValue precedent).
    {"AnimVec3", "AnimVec3",
     {{"OverrideTime",    "OverrideTime",    "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Shape",           "Shape",           "Float", true, 1.0f, 0.0f, 12.0f, Widget::Enum,
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


}  // namespace sw
