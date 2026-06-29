// AnimFloatList floatlist op (floatlist self-registration seam leaf — a pure ANIMATOR PRODUCER:
// zero FloatList inputs, emits a List<float> of `OffsetNumber` AnimMath shape samples on the bars
// clock, one per index with a per-index time offset). TiXL authority:
// external/tixl/Operators/Lib/numbers/anim/animators/AnimFloatList.cs (verbatim below).
//
//   AnimFloatList.cs Update(context):   (VERBATIM)
//     var phases  = Phase.GetValue(context);
//     var rates   = Rate.GetValue(context);
//     var ratio   = Ratio.GetValue(context);
//     var rateFactorFromContext = AnimMath.GetSpeedOverrideFromContext(context, AllowSpeedFactor);
//     _shape      = (AnimMath.Shapes)Shape.GetValue(context).Clamp(0, Enum.GetNames(typeof(Shapes)).Length);
//     var amplitudes = Amplitude.GetValue(context);
//     var offsets    = Offset.GetValue(context);
//     var bias       = Bias.GetValue(context);
//     var time = OverrideTime.HasInputConnections ? OverrideTime.GetValue(context) : context.LocalFxTime;
//     var offsetNumber = OffsetNumber.GetValue(context);   // int
//     var offsetCycle  = OffsetCycle.GetValue(context);    // float
//     if (offsetNumber <= 0) { Result.Value = new List<float>(); return; }   // empty-list guard
//     List<float> floatList = new List<float>(offsetNumber);
//     for (int i = 0; i < offsetNumber; i++) {
//         float adjustedTime = (float)(time + phases + (i * offsetCycle) + offsetNumber);
//         var v = AnimMath.CalcValueForNormalizedTime(_shape,
//                     ((adjustedTime * rateFactorFromContext * rates)), 0, bias, ratio)
//                 * amplitudes + offsets;
//         floatList.Add(v);
//     }
//     Result.Value = floatList;
//
//   ★STATELESS — AnimFloatList holds NO cross-frame field. `_shape` is a UI-preview-only scratch (the
//   curve editor reads it); the Result OUTPUT is a PURE function of (LocalFxTime + the input ports). No
//   WasHit, no `+=`, no previous-frame read — every term comes from `*.GetValue(context)` or
//   context.LocalFxTime. So it rides the STATELESS FloatList rail (the 5th cook flow), NOT the stateful
//   value cook seam (where AnimValue lives because of its cross-frame WasHit tooth). Idempotent under the
//   resident pull-driven re-cook → correct on flat AND production resident with no state slot, exactly the
//   clean producer shape (the AnimVec3 stateless-rail argument, carried to the List<float> currency).
//
//   ★PER-INDEX TIME (the load-bearing line — fork-animfloatlist-adjusted-time): the per-output time is
//     adjustedTime = (float)(time + phases + i*offsetCycle + offsetNumber)
//   i.e. EACH index i adds `i*offsetCycle` AND a CONSTANT `offsetNumber` (the list length itself, added to
//   EVERY element — a TiXL quirk, transcribed verbatim, NOT a fork). The normalized time fed to the shape
//   is then  adjustedTime * rateFactorFromContext * rates  (note: phases is folded INTO time BEFORE the
//   rate multiply, like AnimVec3; NOT added after like AnimValue). componentIndex = 0 for every element
//   (AnimFloatList is single-channel; the .cs passes literal 0 to CalcValueForNormalizedTime).
//
//   AnimFloatList.t3 DefaultValues (mirrored into the PortSpec below; verified against the .t3):
//     Shape=1 (Ramps) · Bias=0.5 · Ratio=1 · AllowSpeedFactor=1 (FactorA) · OffsetNumber=… (see note)
//     · OffsetCycle=… · Rate=1 · Amplitude=1 · Phase=0 · Offset=0 · OverrideTime=0 (DROPPED port).
//   The defaults that are NOT load-bearing for parity (the golden sets every param explicitly) carry the
//   conventional Anim* values: Rate=1, Amplitude=1, Phase=0, Offset=0, Bias=0.5, Ratio=1. OffsetNumber
//   default is left at 0 → the empty-list guard fires until the user dials a count (faithful: a fresh
//   AnimFloatList with no count set produces no elements). OffsetCycle default 0 → every index shares the
//   same `offsetNumber` time shift (all elements identical) until the user spreads them — the .cs behaviour.
//
// FORKS (named):
//   - fork-animfloatlist-floatlist-rail: AnimFloatList is a PRODUCER on the host FloatList currency
//     (Slot<List<float>>), cooked by the floatlist driver (the 5th cook flow). Its output cannot ride
//     NodeSpec::evaluate (which returns ONE float); evaluate is nullptr. Same rail as FloatsToList.
//   - fork-animfloatlist-overridetime-nonzero-single-clock: TiXL branches on OverrideTime.HasInputConnections;
//     this rail has no per-port wired-probe inside the cook. The standing single-clock convention
//     (AnimValue/AnimVec2/AnimVec3) maps "has input connection" → "the OverrideTime constant is non-zero":
//     time = overrideTime != 0 ? overrideTime : ctx.LocalFxTime (bars). With the .t3 default OverrideTime=0
//     this is exactly ctx.LocalFxTime (the default authoring case), byte-EXACT to TiXL; a non-zero
//     OverrideTime constant overrides the bars clock. Diverges from TiXL ONLY in the narrow "OverrideTime
//     connected AND driven to exactly 0.0" case — unreachable here without owner-locked cook-core per-port
//     connection plumbing. Same fork as AnimVec3 / AnimVec2.
//   - fork-animfloatlist-speedfactor-from-context-var: AnimMath.GetSpeedOverrideFromContext reads
//     context.FloatVariables["SpeedFactorA"/"B"] when AllowSpeedFactor != None, defaulting to 1 on a miss.
//     The value/list rail's EvaluationContext has no FloatVariables map → rateFactorFromContext is ALWAYS
//     1 (the TryGetValue-MISS branch) — byte-exact when no SpeedFactor var is set (the default). The
//     AllowSpeedFactor port is retained (enum, clamped) for parity even though every value selects 1. Same
//     fork as AnimVec3/AnimValue.
//   - fork-animfloatlist-shape-enum-on-float-port: Shape/AllowSpeedFactor are InputSlot<int> with
//     MappedType enums in TiXL; this runtime stores them as Float and truncates (std::lround) before use.
//     Shape clamps to [0, kShapeCount-1]; AllowSpeedFactor to [0,2]. (TiXL's Shape.Clamp(0, len) over-
//     permits len=13, but a Shape selector is a valid 0..12 enum value, so [0,12] is the live behaviour.)
//   - fork-animfloatlist-offsetnumber-int-on-float-port: OffsetNumber is an int param on the Float value
//     rail (the small-count IntList fold the FloatList family already uses for WindowSize). The cast from
//     a float-valued integer param is exact; the <=0 empty guard and the loop count are integer-exact.
#include <cmath>   // std::lround
#include <vector>

#include "runtime/anim_math.h"               // AnimMath shape engine (calcValueForNormalizedTime) — the
                                             // PURE per-sample shape value, shared by the Anim* family.
#include "runtime/eval_context.h"            // EvaluationContext (LocalFxTime — the bars clock)
#include "runtime/floatlist_op_registry.h"   // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// AnimFloatList: emit OffsetNumber AnimMath shape samples, one per index, per AnimFloatList.cs Update().
//   time = OverrideTime != 0 ? OverrideTime : ctx.localFxTime (fork-animfloatlist-overridetime-nonzero-single-clock).
//   rateFactorFromContext = 1 (fork-animfloatlist-speedfactor-from-context-var; no FloatVariables on rail).
//   Per index i:
//     adjustedTime = (float)(time + phase + i*offsetCycle + offsetNumber)
//     normalizedTime = adjustedTime * 1 * rate
//     v = CalcValueForNormalizedTime(shape, normalizedTime, /*comp*/0, bias, ratio) * amplitude + offset
void cookAnimFloatList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // Resolved Float params of THIS node (the cook driver hands them via c.params; a hand-built golden ctx
  // supplies them too). Defaults mirror AnimFloatList.t3 / the conventional Anim* values.
  const float phase        = floatListParam(c.params, "Phase", 0.0f);
  const float rate         = floatListParam(c.params, "Rate", 1.0f);
  const float ratio        = floatListParam(c.params, "Ratio", 1.0f);
  const float amplitude    = floatListParam(c.params, "Amplitude", 1.0f);
  const float offset        = floatListParam(c.params, "Offset", 0.0f);
  const float bias         = floatListParam(c.params, "Bias", 0.5f);
  const float offsetCycle  = floatListParam(c.params, "OffsetCycle", 0.0f);

  // Shape enum (.t3 default 1=Ramps); clamp to [0, count-1] like TiXL Shape.GetValue.Clamp.
  int shapeIdx = (int)std::lround(floatListParam(c.params, "Shape", 1.0f));
  if (shapeIdx < 0) shapeIdx = 0;
  else if (shapeIdx > anim_math::kShapeCount - 1) shapeIdx = anim_math::kShapeCount - 1;
  const anim_math::Shapes shape = (anim_math::Shapes)shapeIdx;

  // AllowSpeedFactor enum: resolves to rfc=1 on this rail (no FloatVariables map). Read for parity.
  (void)floatListParam(c.params, "AllowSpeedFactor", 1.0f);
  const double rateFactorFromContext = 1.0;

  // OffsetNumber (int on Float rail). Empty-list guard: offsetNumber <= 0 → empty output.
  const int offsetNumber = (int)floatListParam(c.params, "OffsetNumber", 0.0f);
  if (offsetNumber <= 0) return;  // .cs: Result.Value = new List<float>(); return;

  // time = OverrideTime != 0 ? OverrideTime : ctx.LocalFxTime (bars). c.ctx is wired by both the flat
  // (point_graph_hostvalue_cook.cpp) and resident (resident_host_scalar_cook.cpp cookResidentFloatList)
  // drivers; a hand-built golden ctx supplies it directly. nullptr ctx → clock 0 (a defensive default;
  // the production drivers never pass it). OverrideTime nonzero overrides the clock entirely.
  const float overrideTime = floatListParam(c.params, "OverrideTime", 0.0f);
  const double time = (overrideTime != 0.0f)
                          ? (double)overrideTime
                          : (c.ctx ? (double)c.ctx->localFxTime : 0.0);

  c.output->reserve((size_t)offsetNumber);
  for (int i = 0; i < offsetNumber; ++i) {
    // adjustedTime = (float)(time + phase + i*offsetCycle + offsetNumber)  — VERBATIM. The whole sum is
    // computed in double then cast to float (the .cs `(float)(...)` cast), matching TiXL's rounding.
    const float adjustedTime =
        (float)((double)time + (double)phase + ((double)i * (double)offsetCycle) + (double)offsetNumber);
    // normalizedTime = adjustedTime * rateFactorFromContext(=1) * rate  (phase already folded into time).
    const double normalizedTime = (double)adjustedTime * rateFactorFromContext * (double)rate;
    const float v =
        anim_math::calcValueForNormalizedTime(shape, normalizedTime, /*componentIndex=*/0, bias, ratio) *
            amplitude +
        offset;
    c.output->push_back(v);
  }

  // Test-only: corrupt the REAL output (drop last) so the golden's RED bites on the actual cook path.
  if (floatListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first (FloatList output). NO FloatList input and NO Float MultiInput (a pure animator
//   producer — every term is a single scalar param or LocalFxTime). All inputs are pinless params (the
//   curve-editor authoring shape), in AnimFloatList.cs declaration-adjacent order. OverrideTime FIRST
//   (TiXL [Input] order); nonzero overrides the clock (fork-animfloatlist-overridetime-nonzero-single-clock).
//   Shape/AllowSpeedFactor are Widget::Enum selectors (AnimVec3 precedent). PortSpec field order:
//   id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const FloatListOp _reg_animfloatlist{
    {"AnimFloatList", "AnimFloatList",
     {{"out", "out", "FloatList", false},
      {"OverrideTime", "OverrideTime", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Shape", "Shape", "Float", true, 1.0f, 0.0f, 12.0f, Widget::Enum,
       {"Endless", "Ramps", "Saws", "KickSaws", "Square", "ZigZag", "Wave", "Sin",
        "PerlinNoise", "PerlinNoiseSigned", "Random", "RandomSigned", "Steps"},
       /*pinless=*/true},
      {"OffsetNumber", "OffsetNumber", "Float", true, 0.0f, 0.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"OffsetCycle", "OffsetCycle", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Rate", "Rate", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Amplitude", "Amplitude", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Phase", "Phase", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Offset", "Offset", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Bias", "Bias", "Float", true, 0.5f, 0.0001f, 1.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Ratio", "Ratio", "Float", true, 1.0f, 0.0001f, 100000.0f, Widget::Slider, {}, /*pinless=*/true},
      {"AllowSpeedFactor", "AllowSpeedFactor", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
       {"None", "FactorA", "FactorB"}, /*pinless=*/true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookAnimFloatList};

}  // namespace sw
