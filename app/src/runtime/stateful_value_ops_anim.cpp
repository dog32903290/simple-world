// runtime/stateful_value_ops_anim — the Anim* animator family:
//   AnimValue / AnimInt / AnimBoolean.  Result via the anim_math shape engine; WasHit/TriggerOutput
//   is the cross-frame integer edge. Each op carries its TEETH-hook global + its public setter
//   (setAnimValueBug / setAnimIntBug / setAnimBooleanBug) so the app-side goldens can flip the bug
//   around the REAL production cook.
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/anim_math.h"  // the Anim* shape engine (calcValueForNormalizedTime / Shapes)
#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

// ============================ Anim* family — AnimValue (the canonical Result+WasHit op) ============
// --- AnimValue (TiXL Lib/numbers/anim/animators/AnimValue.cs) — the foundation of the whole Anim*
// family: an oscillator/shaper whose Result is a PURE function of inputs+context-time (delegated to
// the AnimMath shape engine, runtime/anim_math.h), and whose WasHit is the ONLY consumer of
// cross-frame state — a tooth that fires once when the integer part of normalizedTime advances.
// TiXL Update (AnimValue.cs:25-53) ported faithfully:
//   _normalizedTime = time * rateFactorFromContext * rate + phase;
//   Result          = CalcValueForNormalizedTime(shape, _normalizedTime, 0, bias, ratio)*amplitude + offset;
//   WasHit          = (int)originalTime != (int)_normalizedTime;   // originalTime = PRIOR _normalizedTime
//
// Outputs (TiXL output decl order, both DirtyFlagTrigger.Animated): Result(out[0]), WasHit(out[1]).
//   WasHit is Bool → Float 0/1 (Cut 32: no Bool port type; same dissolve as HasValueChanged.HasChanged).
// Inputs (TiXL Input decl order): OverrideTime, Shape(enum), Rate, Ratio, Phase, Amplitude, Offset,
//   Bias, AllowSpeedFactor(enum). .t3 defaults (AnimValue.t3, RE-READ & confirmed): Rate=1, Shape=1
//   (Ramps — the .t3 selector value, NOT the C# field default Endless=0), Phase=0, Amplitude=1,
//   Ratio=1, Offset=0, Bias=0.5, AllowSpeedFactor=1 (FactorA), OverrideTime=0.
//
// State (the cross-frame tooth): s[0] = _normalizedTime of the PRIOR cook. init=false on the very
//   first cook (TiXL _normalizedTime field-inits to 0, so originalTime=0 on frame 1 — faithful: we
//   read s[0] which is zero-initialized, so no `init` seeding needed; s[0] starts 0 exactly like
//   TiXL's _normalizedTime field). Only WasHit reads this; Result is pure (no state).
//
// FORKS (named) — same family precedent as Ease/HasValueChanged/HasTimeChanged:
//   • SINGLE-CLOCK time source. TiXL: `time = OverrideTime.HasInputConnections ? OverrideTime :
//     context.LocalFxTime`. The cook seam hands ONE clock via `time` (= wall fx seconds, the
//     single-clock substitution for context.LocalFxTime the whole time-op family already uses), and
//     resolveResidentFloatInputs gives the step fn only the RESOLVED Float map — it cannot see
//     `HasInputConnections` (a connected-input-feeding-0 is indistinguishable from the .t3 default 0).
//     So OverrideTime is honored when NONZERO and falls back to the seam `time` when 0. This is exact
//     for the two dominant cases (OverrideTime unconnected → default 0 → seam time; OverrideTime
//     driven nonzero → that value) and diverges ONLY in the narrow "OverrideTime connected and
//     feeding exactly 0.0" case, which reads seam time instead. NAMED, accepted: the seam has no
//     connection-presence channel, so this is the minimal faithful substitute (本質 seam constraint,
//     not a math change).
//   • The `Math.Abs(LocalFxTime - _lastUpdateTime) > double.Epsilon` WasHit double-eval guard is
//     DROPPED — frame_cook cooks each node exactly once per frame (Damp/Spring/Ease/CountInt
//     precedent), so the same-frame double-update that guard prevents cannot occur. WasHit is
//     therefore recomputed every cook from (originalTime, _normalizedTime), which is what TiXL does
//     once per real frame. No _lastUpdateTime stored.
//   • SpeedFactor context-var read. TiXL AnimMath.GetSpeedOverrideFromContext reads
//     context.FloatVariables["SpeedFactorA"/"SpeedFactorB"] (default 1 when absent) per the
//     AllowSpeedFactor enum (None=0/FactorA=1/FactorB=2). We read the SAME host-side ContextVarMap
//     the Set*FloatVar seam populates (`vars`), so a SetFloatVar("SpeedFactorA",k) upstream scales the
//     rate exactly as TiXL — wired through the existing context-var YELLOW seam, no new channel. When
//     `vars` is null (the many selftest callers that don't pass it) or the key is unset → factor 1.0,
//     TiXL's own TryGetValue-miss default.
// AnimValue TEETH hook (file-local; 0 = production, set by the --selftest-animvalue golden ONLY via
// setAnimValueBug). It corrupts a REAL production term so the golden's FIXED expected values bite:
//   1 = DROP the state write (st.s[0] never advances) → originalTime stays 0 → the cross-frame WasHit
//       tooth never fires after frame 1 (and Result is unaffected — Result is pure, so this bites
//       ONLY the state-dependent output, proving the state write is load-bearing).
//   2 = DROP the AnimMath call (Result forced to the raw normalizedTime, no shape/bias/amp/offset) →
//       the Result golden bites while WasHit (state) stays correct.
// Defaults 0 so the production cook (cookStatefulValueNodes) and every other caller are unchanged.
// The expected values in the golden are computed from the TiXL formula and are INDEPENDENT of this
// flag (no co-conditioning) — the flag breaks the live computation, the wants stay put.
int g_animValueBug = 0;

void stepAnimValue(const std::map<std::string, float>& in, float /*dt*/, float time,
                   StatefulValueState& st, float out[3], const TransportSnapshot&,
                   ContextVarMap* vars, const std::string&) {
  const float rate = getIn(in, "Rate", 1.0f);
  const float ratio = getIn(in, "Ratio", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);
  const float amplitude = getIn(in, "Amplitude", 1.0f);
  const float offset = getIn(in, "Offset", 0.0f);
  const float bias = getIn(in, "Bias", 0.5f);

  // Shape enum (.t3 default 1=Ramps); clamp to [0, count-1] like TiXL Shape.GetValue.Clamp.
  int shapeIdx = (int)std::lround(getIn(in, "Shape", 1.0f));
  if (shapeIdx < 0) shapeIdx = 0;
  else if (shapeIdx > anim_math::kShapeCount - 1) shapeIdx = anim_math::kShapeCount - 1;
  const anim_math::Shapes shape = (anim_math::Shapes)shapeIdx;

  // rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(AllowSpeedFactor):
  //   None(0) → 1 ; FactorA(1) → FloatVariables["SpeedFactorA"] (default 1) ; FactorB(2) → "SpeedFactorB".
  int speedSel = (int)std::lround(getIn(in, "AllowSpeedFactor", 1.0f));
  if (speedSel < 0) speedSel = 0;
  else if (speedSel > 2) speedSel = 2;  // Clamp(0, len-1)
  float rateFactorFromContext = 1.0f;
  if (vars && (speedSel == 1 || speedSel == 2)) {
    const char* key = (speedSel == 1) ? "SpeedFactorA" : "SpeedFactorB";
    auto it = vars->floatVars.find(key);
    if (it != vars->floatVars.end()) rateFactorFromContext = it->second;  // TryGetValue hit; miss → 1
  }

  // SINGLE-CLOCK time source (named fork above): OverrideTime when nonzero, else the seam clock.
  const float overrideTime = getIn(in, "OverrideTime", 0.0f);
  const double t = (overrideTime != 0.0f) ? (double)overrideTime : (double)time;

  const double originalTime = (double)st.s[0];  // prior frame's _normalizedTime (zero-init on frame 1)
  const double normalizedTime = t * (double)rateFactorFromContext * (double)rate + (double)phase;
  if (g_animValueBug != 1) st.s[0] = (float)normalizedTime;  // bug 1: DROP the state write (real defect)

  out[0] = (g_animValueBug == 2)  // bug 2: DROP the AnimMath call (real defect — raw nt, no shaping)
               ? (float)normalizedTime
               : anim_math::calcValueForNormalizedTime(shape, normalizedTime, 0, bias, ratio) * amplitude + offset;
  out[1] = ((int)originalTime != (int)normalizedTime) ? 1.0f : 0.0f;  // WasHit (Bool→Float)
}

// ============================ Anim* family — AnimInt (integer Result + WasHit) ====================
// --- AnimInt (TiXL Lib/numbers/anim/animators/AnimInt.cs) — the integer sibling of AnimValue: same
// normalizedTime clock, but Result is the INTEGER part of normalizedTime (optionally wrapped with a
// POSITIVE modulo) rather than an AnimMath-shaped float. WasHit is the SAME cross-frame integer tooth.
// TiXL Update (AnimInt.cs:30-49) ported faithfully:
//   _normalizedTime = time * rateFactorFromContext * rate + phase;
//   result          = (int)_normalizedTime;
//   Result          = modulo != 0 ? result.Mod(modulo) : result;   // .Mod = POSITIVE modulo
//   WasHit          = (int)originalTime != (int)_normalizedTime;    // originalTime = PRIOR _normalizedTime
//
// `.Mod` is MathUtils.cs:273 — the POSITIVE (floored) integer modulo: `x = val % repeat; if (x<0)
//   x = repeat + x; return x;` and `repeat==0 → 0` (but the modulo!=0 guard means we never call it
//   with 0). Result is NOT C `%` (which keeps the sign of the dividend) — for negative time it wraps
//   into [0, modulo). Reproduced 1:1 below.
//
// Outputs (TiXL output decl order, both DirtyFlagTrigger.Animated): Result(out[0]), WasHit(out[1]).
//   Result is int → Float (carried as a Float port; the int math is exact for the anim-time range).
//   WasHit is Bool → Float 0/1 (Cut 32 — no Bool port type; same dissolve as AnimValue.WasHit).
// Inputs (TiXL Input decl order): Modulo(int), OverrideTime(float), Rate(float), Phase(float),
//   AllowSpeedFactor(int enum). .t3 defaults (AnimInt.t3, RE-READ & confirmed): Modulo=0, Rate=1.0,
//   Phase=0.0, AllowSpeedFactor=1 (FactorA), OverrideTime=0.0.
//
// State (the cross-frame tooth): s[0] = _normalizedTime of the PRIOR cook (zero-init on frame 1,
//   exactly like TiXL's _normalizedTime field). Only WasHit reads it; Result is pure.
//
// FORKS (named) — IDENTICAL family forks to AnimValue (same seam, same justification):
//   • SINGLE-CLOCK time source. TiXL: `time = OverrideTime.HasInputConnections ? OverrideTime :
//     context.LocalFxTime`. The seam hands ONE resolved clock; the step fn can't see
//     HasInputConnections, so OverrideTime is honored when NONZERO and falls back to the seam `time`
//     when 0. Exact for the two dominant cases; diverges only in the "OverrideTime connected feeding
//     exactly 0.0" case (本質 seam constraint, not a math change).
//   • The `_lastUpdateFrame == Playback.FrameCount` frame-dedup guard is DROPPED — frame_cook cooks
//     each node exactly ONCE per frame (Damp/Ease/AnimValue precedent), so the same-frame double-cook
//     that guard prevents cannot occur. WasHit is recomputed every cook from (originalTime, nt), which
//     is what TiXL does once per real frame. No _lastUpdateFrame stored.
//   • SpeedFactor context-var read. Same host-side ContextVarMap channel as AnimValue
//     (FloatVariables["SpeedFactorA"/"SpeedFactorB"], default 1 on miss) per the AllowSpeedFactor enum.
// AnimInt TEETH hook (file-local; 0 = production, set ONLY by the --selftest-animint golden via
// setAnimIntBug). It corrupts a REAL production term so the golden's FIXED expected values bite:
//   1 = DROP the state write (st.s[0] never advances) → originalTime stays 0 → the cross-frame WasHit
//       tooth breaks (Result is pure, so this bites ONLY the state output — proving the write is
//       load-bearing).
//   2 = DROP the Modulo wrap (Result forced to the raw (int)nt, ignoring modulo) → the modulo Result
//       golden bites while WasHit (state) stays correct.
// The expected values in the golden are computed from the TiXL formula and are INDEPENDENT of this
// flag (no co-conditioning).
int g_animIntBug = 0;

// Positive (floored) integer modulo — MathUtils.Mod (MathUtils.cs:273) ported VERBATIM. repeat==0
// returns 0 (guarded by the modulo!=0 caller, but kept faithful). Differs from C `%` for negatives.
static int posMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

void stepAnimInt(const std::map<std::string, float>& in, float /*dt*/, float time,
                 StatefulValueState& st, float out[3], const TransportSnapshot&,
                 ContextVarMap* vars, const std::string&) {
  const float rate = getIn(in, "Rate", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);
  const int modulo = (int)std::lround(getIn(in, "Modulo", 0.0f));

  // rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(AllowSpeedFactor):
  //   None(0) → 1 ; FactorA(1) → FloatVariables["SpeedFactorA"] (default 1) ; FactorB(2) → "SpeedFactorB".
  int speedSel = (int)std::lround(getIn(in, "AllowSpeedFactor", 1.0f));
  if (speedSel < 0) speedSel = 0;
  else if (speedSel > 2) speedSel = 2;  // Clamp(0, len-1)
  float rateFactorFromContext = 1.0f;
  if (vars && (speedSel == 1 || speedSel == 2)) {
    const char* key = (speedSel == 1) ? "SpeedFactorA" : "SpeedFactorB";
    auto it = vars->floatVars.find(key);
    if (it != vars->floatVars.end()) rateFactorFromContext = it->second;  // TryGetValue hit; miss → 1
  }

  // SINGLE-CLOCK time source (named fork above): OverrideTime when nonzero, else the seam clock.
  const float overrideTime = getIn(in, "OverrideTime", 0.0f);
  const double t = (overrideTime != 0.0f) ? (double)overrideTime : (double)time;

  const double originalTime = (double)st.s[0];  // prior frame's _normalizedTime (zero-init on frame 1)
  const double normalizedTime = t * (double)rateFactorFromContext * (double)rate + (double)phase;
  if (g_animIntBug != 1) st.s[0] = (float)normalizedTime;  // bug 1: DROP the state write (real defect)

  const int result = (int)normalizedTime;
  // bug 2: DROP the modulo wrap (real defect — raw (int)nt, ignoring the positive modulo).
  out[0] = (g_animIntBug == 2) ? (float)result
                               : (float)((modulo != 0) ? posMod(result, modulo) : result);
  out[1] = ((int)originalTime != (int)normalizedTime) ? 1.0f : 0.0f;  // WasHit (Bool→Float)
}

// ============================ Anim* family — AnimBoolean (the pure TriggerOutput edge) ============
// --- AnimBoolean (TiXL Lib/numbers/anim/animators/AnimBoolean.cs) — the INVERSE of AnimValue/AnimInt:
// it has NO Result output at all; its ONLY output, TriggerOutput, IS the WasHit cross-frame edge.
// TiXL Update (AnimBoolean.cs:23-37) ported faithfully:
//   time            = context.LocalFxTime;                         // NO OverrideTime input here
//   _normalizedTime = time * rateFactorFromContext * rate + phase;
//   TriggerOutput   = (int)originalTime != (int)_normalizedTime;   // originalTime = PRIOR _normalizedTime
//
// Output (single, DirtyFlagTrigger.Animated): TriggerOutput(out[0]) = the integer tooth (Bool→Float
//   0/1, Cut 32). There is NO pure Result — this op exists solely to fire when (int)time advances.
// Inputs (TiXL Input decl order): Rate(float), Phase(float), AllowSpeedFactor(int enum). .t3 defaults
//   (AnimBoolean.t3, RE-READ & confirmed): Rate=1.0, ★AllowSpeedFactor=0 (None — DIFFERENT from
//   AnimValue/AnimInt which default to 1/FactorA), Phase=0.0. NO Modulo, NO OverrideTime, NO Ratio.
//
// State: s[0] = _normalizedTime of the PRIOR cook (zero-init on frame 1, like TiXL's field).
//
// FORKS (named):
//   • NO OverrideTime input → NO single-clock fork needed: time is ALWAYS the seam clock (the
//     single-clock substitution for context.LocalFxTime the whole time-op family uses). This is the
//     ONE Anim* op that is exact on the clock (TiXL itself reads only context.LocalFxTime).
//   • The `_lastUpdateFrame == Playback.FrameCount` frame-dedup guard is DROPPED (once-per-frame cook,
//     same as AnimValue/AnimInt).
//   • SpeedFactor context-var read — same channel as AnimValue/AnimInt.
// AnimBoolean TEETH hook (file-local; 0 = production, set ONLY by --selftest-animboolean via
// setAnimBooleanBug):
//   1 = DROP the state write (st.s[0] never advances) → originalTime frozen at 0 → the TriggerOutput
//       tooth breaks (fires/doesn't-fire wrong after frame 1). This op has ONLY the state output, so
//       bug 1 is the natural corruption — it proves the cross-frame state write is load-bearing.
//   2 = FREEZE the edge to 0 (TriggerOutput forced low, ignoring the comparison) → bites the frames
//       where the tooth SHOULD fire while leaving the no-fire frames correct (an independent defect
//       from bug 1, so the golden's want=1 frames bite even if state happens to look right).
// Expected values are computed from the TiXL formula and are INDEPENDENT of this flag.
int g_animBooleanBug = 0;

void stepAnimBoolean(const std::map<std::string, float>& in, float /*dt*/, float time,
                     StatefulValueState& st, float out[3], const TransportSnapshot&,
                     ContextVarMap* vars, const std::string&) {
  const float rate = getIn(in, "Rate", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);

  // rateFactorFromContext per AllowSpeedFactor (.t3 default 0=None → factor 1). Same channel as above.
  int speedSel = (int)std::lround(getIn(in, "AllowSpeedFactor", 0.0f));
  if (speedSel < 0) speedSel = 0;
  else if (speedSel > 2) speedSel = 2;  // Clamp(0, len-1)
  float rateFactorFromContext = 1.0f;
  if (vars && (speedSel == 1 || speedSel == 2)) {
    const char* key = (speedSel == 1) ? "SpeedFactorA" : "SpeedFactorB";
    auto it = vars->floatVars.find(key);
    if (it != vars->floatVars.end()) rateFactorFromContext = it->second;  // TryGetValue hit; miss → 1
  }

  // No OverrideTime — time is ALWAYS the seam clock (faithful: TiXL reads only context.LocalFxTime).
  const double t = (double)time;
  const double originalTime = (double)st.s[0];  // prior frame's _normalizedTime (zero-init on frame 1)
  const double normalizedTime = t * (double)rateFactorFromContext * (double)rate + (double)phase;
  if (g_animBooleanBug != 1) st.s[0] = (float)normalizedTime;  // bug 1: DROP the state write (real defect)

  // bug 2: FREEZE the edge to 0 (real defect — ignore the comparison entirely).
  out[0] = (g_animBooleanBug == 2) ? 0.0f
                                   : (((int)originalTime != (int)normalizedTime) ? 1.0f : 0.0f);  // TriggerOutput
}

}  // namespace

// AnimValue teeth hook setter (the global lives in the anonymous namespace above; this gives the
// app-side --selftest-animvalue golden a handle to flip it around the REAL production cook).
void setAnimValueBug(int mode) { g_animValueBug = mode; }

// AnimInt / AnimBoolean teeth hook setters (globals in the anonymous namespace above; handles for the
// app-side --selftest-animint / --selftest-animboolean goldens to flip around the REAL prod cook).
void setAnimIntBug(int mode) { g_animIntBug = mode; }
void setAnimBooleanBug(int mode) { g_animBooleanBug = mode; }

static const StatefulOpReg _reg_AnimValue{"AnimValue", stepAnimValue};
static const StatefulOpReg _reg_AnimInt{"AnimInt", stepAnimInt};
static const StatefulOpReg _reg_AnimBoolean{"AnimBoolean", stepAnimBoolean};

}  // namespace sw
