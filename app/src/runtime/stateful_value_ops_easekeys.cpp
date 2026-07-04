// runtime/stateful_value_ops_easekeys — EaseKeys family (EaseKeys / EaseVec2Keys / EaseVec3Keys):
// REPLACE the animation curve's own interpolation between keyframes with an easing function.
// When the node's Value input is animated (keyframe curves on it), the op finds the bracketing
// keys around the playhead and eases between their VALUES; otherwise it passes the input through.
//
// TiXL authority:
//   Operators/Lib/numbers/float/process/EaseKeys.cs      (scalar; curves[0])
//   Operators/Lib/numbers/vec2/process/EaseVec2Keys.cs   (per-channel curves[i])
//   Operators/Lib/numbers/vec3/process/EaseVec3Keys.cs   (structurally identical to Vec2, N=3)
//   Core/Utils/EasingFunctions.cs (ApplyEasing — already ported: stateful_value_ops_easing.h)
//   Core/DataTypes/Curve.cs FindIndexBefore/TryGetPreviousKey/TryGetNextKey (cs:80-105, 160-185)
//
//   EaseKeys.cs Update() (cs:18-61):
//     inputValue = Value.GetValue(context);              // ANIMATED read = curve sampled @ LocalTime
//     if (!TryFindCurveWithIndex(out curve))             // cs:22 = Animator.IsAnimated(child, Value)
//         { Result = inputValue; return; }               // NOT animated → passthrough (cs:24-26)
//     easeMode = Interpolation; easeDirection = Direction; currentTime = context.LocalTime;  // cs:28-30
//     if (TryFindClosestKeys(currentTime, out (prev,next))            // cs:38 (prev = last key < t,
//         && prev.OutInterpolation != KeyInterpolation.Constant) {    //  next = first key >= t)
//         _startTime = prev.U; _initialValue = (float)prev.Value; _targetValue = (float)next.Value;
//         duration = Math.Max((float)(next.U - prev.U), 0.0001f);     // cs:43-46
//     } else { Result = inputValue; return; }                         // cs:48-52 (outside keys /
//                                                                     //  Constant-out segment)
//     progress = ((float)(currentTime - _startTime) / duration).Clamp(0,1);  // cs:55-56
//     Result = MathUtils.Lerp(_initialValue, _targetValue, ApplyEasing(progress, dir, mode));  // cs:58-60
//
//   EaseVec2Keys/EaseVec3Keys: the same per channel i over curves[i] (cs:32-62), with the
//   per-channel else-branch `Result.Value[i] = inputValue[i]; continue;` (cs:49-53).
//
// SEAM (why this is not a plain step fn): the op must read the CURVES on its OWN Value input —
// def-layer Animator state the resolved-Float `in` map cannot carry. The production cook
// (frame_cook cookStatefulValueNodes → cookOne) therefore calls cookEaseKeysNode() FIRST; it
// handles the three EaseKeys types (reading rn's ResidentInput Automation drivers exactly like
// sampleAutomation: animSymbolId → Symbol.animator.resolveRef(curveRef)) and returns true, else
// returns false and the generic cookStatefulValueOp path runs (one added seam line in frame_cook).
// The registered step fns below implement ONLY TiXL's not-animated passthrough (cs:24-26) — the
// honest fallback for any caller without resident/animator context (flat rail, generic selftests).
//
// TIME: currentTime = context.LocalTime = the playhead in BARS (rctx.localTime), the SAME clock the
// Automation sampler uses (sampleAutomation samples @ ctx.localTime) — key U values are bars.
//
// FORKS (named):
//   - fork-easekeys-per-channel-animation: TiXL animates the whole VecN input as one Curve[N]
//     (AddCurvesForFloatVector); sw projects each component slot its own Automation driver keyed
//     under the group HEAD (resident_eval_flatten). A partially-animated group (impossible via the
//     Inspector gesture, = TiXL) falls back per channel — same observable as TiXL's full-array case.
//   - fork-int-bool-dissolve-to-float: Direction/Interpolation are MappedType int enums → Float
//     enum ports (family convention, same rows as Ease/EaseVec2/EaseVec3).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <iterator>  // std::prev
#include <map>
#include <string>

#include "runtime/curve.h"                        // Curve / VDefinition / KeyInterpolation
#include "runtime/curve_animator.h"               // Animator::resolveRef
#include "runtime/resident_eval_graph.h"          // ResidentNode / ResidentInput / ResidentEvalCtx
#include "runtime/stateful_value_op_registry.h"   // StatefulOpReg
#include "runtime/stateful_value_ops.h"           // StatefulValueState / setEaseKeysBug decl
#include "runtime/stateful_value_ops_easing.h"    // applyEasing (EasingFunctions.cs port)
#include "runtime/stateful_value_ops_internal.h"  // getIn / getInC / enumOf / lerpf / clamp01

namespace sw {
namespace {

// EaseKeys TEETH hook (file-local; 0 = production, set by --selftest-easekeys only via
// setEaseKeysBug). Corrupts a REAL production term so the golden's FIXED expected values bite:
//   1 = DROP the easing shaping (eased := raw progress — ApplyEasing severed): every non-Linear
//       probe reads the linear lerp instead → RED (Linear-mode probes stay green, isolating the bite
//       to the shaping term).
//   2 = SEVER the curve query (the Automation-driver read returns no curve): every animated probe
//       collapses to the passthrough → RED, proving the def-layer curve read is load-bearing.
int g_easeKeysBug = 0;

// How many channels + the Value slot id per type. Returns 0 when not an EaseKeys-family op.
int easeKeysArity(const std::string& opType) {
  if (opType == "EaseKeys") return 1;
  if (opType == "EaseVec2Keys") return 2;
  if (opType == "EaseVec3Keys") return 3;
  return 0;
}
// Component slot id: scalar = "Value"; vec = "Value.x"/".y"/".z" (node_registry vec convention).
std::string valueSlotId(int arity, int c) {
  if (arity == 1) return "Value";
  return std::string("Value") + kVecComp[c];
}

// Resolve the Curve on ONE component slot through its Automation driver (= sampleAutomation's
// resolution path: animSymbolId → Symbol.animator.resolveRef(curveRef)). nullptr when the slot is
// not Automation-driven (TiXL TryFindCurveWithIndex false → not animated).
const Curve* curveFor(const ResidentNode& rn, const ResidentEvalCtx& ctx, const std::string& slotId) {
  if (g_easeKeysBug == 2) return nullptr;  // bug 2: SEVER the curve query (real seam corruption)
  const ResidentInput* ri = rn.input(slotId);
  if (!ri || ri->driver != ResidentInput::Driver::Automation || !ctx.lib) return nullptr;
  const Symbol* sym = ctx.lib->find(ri->animSymbolId);
  if (!sym) return nullptr;
  const Curve* c = sym->animator.resolveRef(ri->curveRef);
  return (c && !c->empty()) ? c : nullptr;
}

}  // namespace

void setEaseKeysBug(int mode) { g_easeKeysBug = mode; }

// The production cook for the EaseKeys family (called by frame_cook's cookOne BEFORE the generic
// cookStatefulValueOp; returns false for every other op type). `in` = the resolved Float inputs
// (an animated Value component already carries the curve-sampled value = TiXL GetValue on an
// animated slot). Writes out[0..N-1] (the Result components, extOut order).
bool cookEaseKeysNode(ResidentNode& rn, const ResidentEvalCtx& ctx,
                      const std::map<std::string, float>& in, float out[8]) {
  const int N = easeKeysArity(rn.opType);
  if (N == 0) return false;

  const int direction = enumOf(in, "Direction");       // EaseDirection (In/Out/InOut)
  const int easeMode = enumOf(in, "Interpolation");    // Interpolations (Linear..Bounce)
  const double t = (double)ctx.localTime;              // context.LocalTime (bars, cs:30)

  for (int c = 0; c < N; ++c) {
    const std::string slot = valueSlotId(N, c);
    const float inputValue = (N == 1) ? getIn(in, "Value", 0.0f) : getInC(in, "Value", c);

    const Curve* curve = curveFor(rn, ctx, slot);
    if (!curve) { out[c] = inputValue; continue; }  // not animated → passthrough (cs:24-26)

    // TryFindClosestKeys (cs:83-96): prev = last key STRICTLY < round(t) (FindIndexBefore,
    // Curve.cs:160-185 rounds to TimePrecision), next = the following key (first >= round(t)).
    const auto& tbl = curve->table();
    const auto nextIt = tbl.lower_bound(Curve::roundTime(t));
    if (nextIt == tbl.begin() || nextIt == tbl.end()) { out[c] = inputValue; continue; }  // cs:48-52
    const auto prevIt = std::prev(nextIt);
    if (prevIt->second.outInterpolation == KeyInterpolation::Constant) {  // cs:39-40
      out[c] = inputValue;
      continue;
    }

    const double startTime = prevIt->first;                       // prev.U (cs:43)
    const float initialValue = (float)prevIt->second.value;       // (float)prev.Value (cs:44)
    const float targetValue = (float)nextIt->second.value;        // (float)next.Value (cs:45)
    float duration = (float)(nextIt->first - prevIt->first);      // cs:46
    if (duration < 0.0001f) duration = 0.0001f;                   // Math.Max(dt, 0.0001f)

    const float progress = clamp01((float)(t - startTime) / duration);  // cs:55-56
    const float eased = (g_easeKeysBug == 1)  // bug 1: DROP the easing shaping (raw progress)
                            ? progress
                            : applyEasing(progress, direction, easeMode);  // cs:58
    out[c] = lerpf(initialValue, targetValue, eased);  // MathUtils.Lerp (cs:60)
  }
  return true;
}

namespace {

// Registered step fns = TiXL's NOT-animated passthrough only (cs:24-26) — the honest behavior for
// any caller without the resident/animator context (the production cook intercepts with
// cookEaseKeysNode above and never reaches these when curves exist).
void stepEaseKeys(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                  float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  out[0] = getIn(in, "Value", 0.0f);
}
void stepEaseVec2Keys(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                      float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  for (int c = 0; c < 2; ++c) out[c] = getInC(in, "Value", c);
}
void stepEaseVec3Keys(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                      float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  for (int c = 0; c < 3; ++c) out[c] = getInC(in, "Value", c);
}

}  // namespace

static const StatefulOpReg _reg_EaseKeys{"EaseKeys", stepEaseKeys};
static const StatefulOpReg _reg_EaseVec2Keys{"EaseVec2Keys", stepEaseVec2Keys};
static const StatefulOpReg _reg_EaseVec3Keys{"EaseVec3Keys", stepEaseVec3Keys};

}  // namespace sw
