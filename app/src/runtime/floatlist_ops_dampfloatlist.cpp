// DampFloatList floatlist op (floatlist self-registration seam leaf — a cross-frame STATE consumer on the
// FLOATLIST rail, sibling of AmplifyValues). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/DampFloatList.cs (verbatim Update() below; cited inline).
//
//   DampFloatList.cs Update():
//     var inputList = Values.GetValue(context);
//     var damping   = Damping.GetValue(context);
//     var currentTime = UseAppRunTime ? Playback.RunTimeInSecs : context.LocalFxTime;
//     if (Math.Abs(currentTime - _lastEvalTime) < MinTimeElapsedBeforeEvaluation) return;   // dt-gate (1/1000)
//     if (inputList == null || inputList.Count == 0) return;                                  // empty → leave output
//     // (re)size Result + internal lists to inputList.Count
//     MatchListLength(ref _dampedValues, inputList.Count);
//     MatchListLength(ref _velocities,   inputList.Count);
//     _lastEvalTime = currentTime;
//     var method = (DampFunctions.Methods)Method.Clamp(0,1);
//     for (i = 0; i < count; i++) {
//       var velocity = _velocities[i];
//       var dampedValue = DampFunctions.DampenFloat(inputList[i], _dampedValues[i], damping, ref velocity, method);
//       ApplyDefaultIfInvalid(ref dampedValue, 0); ApplyDefaultIfInvalid(ref velocity, 0);
//       _dampedValues[i] = dampedValue; _velocities[i] = velocity;
//     }
//     Result.Value.AddRange(_dampedValues);
//   Fields: _dampedValues / _velocities / _lastEvalTime — PERSISTENT per node (cs:78-80).
//
// METHOD / .t3 DEFAULT (DampFloatList.t3): Method = 0 = DampFunctions.Methods.LinearInterpolation. For
//   Method 0, DampenFloat → LinearDamp(input, prev, damping) = MathUtils.Lerp(input, prev, damping) =
//   input + (prev - input)*damping  (MathUtils.cs:305-308 / DampFunctions LinearDamp). The `velocity` ref is
//   NOT used by Method 0 (it stays 0 → ApplyDefaultIfInvalid keeps it 0). So at the .t3 defaults this op is a
//   per-index damped follower IDENTICAL in shape to AmplifyValues' Lerp(v, avg, smoothing): the damped value
//   chases the input, closing (1-damping) of the gap each evaluated frame. (Method 1 = DampedSpring needs the
//   per-frame Playback.LastFrameDuration spring integrator + velocity — FORK-method-1-deferred below.)
//
// ★.t3 DEFAULTS (DampFloatList.t3 — OVERRIDE the C# ctor `new()` defaults):
//   Damping = 0.95 ; Method = 0 (LinearInterpolation) ; UseAppRunTime = false (→ currentTime = LocalFxTime) ;
//   Values = {5.0, 17.0} (the const default rides when no wire feeds Values; a node-to-node wire overrides).
//
// FORK (named, load-bearing): fork-dampfloatlist-dt-gate. The MinTimeElapsedBeforeEvaluation (=1/1000 bars)
//   gate is faithful: if the bars clock advanced < 1/1000 since the last evaluated frame, the op RETURNS
//   re-publishing the previous _dampedValues WITHOUT damping (no further drift on a paused/sub-tick frame).
//   `_lastEvalTime` persists in FloatListState (lastEvalTime + dampInited). With UseAppRunTime=false the clock
//   is ctx->localFxTime (BARS) — the cook driver wires fc.ctx on both flat + resident paths (AnimFloatList
//   precedent). A hand-built ctx with localFxTime advancing each frame steps the op normally.
//
// FORK (named): fork-method-1-deferred. Only Method 0 (LinearInterpolation, the .t3 default) is ported.
//   Method 1 (DampedSpring) calls MathUtils.SpringDamp with Playback.LastFrameDuration (a wall-clock frame
//   duration clamped to 1/60) — that needs the spring integrator + a real frame-duration source not on this
//   host rail. Method is clamped to [0,1] and treated as LinearInterpolation; a non-zero Method param is a
//   no-op (faithful to the default authoring; the spring variant is deferred, not silently mis-computed).
//
// FORK (named): fork-empty-leaves-output. cs returns on empty/absent input WITHOUT touching Result. The
//   driver hands a fresh *output each cook → "leave untouched" = write nothing → empty input yields empty out.
#include <cmath>  // std::isnan / std::isinf (ApplyDefaultIfInvalid guards)

#include "runtime/eval_context.h"           // EvaluationContext (localFxTime, the bars clock)
#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / FloatListState / injectBug / param
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// MathUtils.Lerp(a, b, t) = a + (b - a) * t (MathUtils.cs:305-308). LinearDamp(input, prev, damping) =
// Lerp(input, prev, damping) — DampFunctions.cs LinearDamp / DampenFloat for Methods.LinearInterpolation.
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// DampFloatList: per-index damped follower with cross-frame state, per DampFloatList.cs Update() (verbatim,
// Method 0 = LinearInterpolation = the .t3 default).
void cookDampFloatList(FloatListCookCtx& c) {
  if (!c.output) return;

  // No state slot (a hand-built ctx with no driver-owned state) → behave as a single fresh frame: a local
  // scratch state (dampInited=false → seed; empty lists → MatchListLength sizes them). Faithful to frame 0.
  FloatListState local;
  FloatListState* st = c.state ? c.state : &local;

  // cs:23 — currentTime: UseAppRunTime=false (.t3 default) → context.LocalFxTime (bars). A hand-built ctx with
  // no time reader → 0 (the gate then fires only on a repeated 0; the goldens advance localFxTime per frame).
  const double currentTime = c.ctx ? (double)c.ctx->localFxTime : 0.0;
  const float kMinTimeElapsed = 1.0f / 1000.0f;  // cs:76 MinTimeElapsedBeforeEvaluation

  // cs:24-25 — dt-gate: skip damping if the clock barely advanced. On the FIRST cook (dampInited=false) we do
  // NOT gate (cs's _lastEvalTime starts 0; the first frame with currentTime>=1/1000 passes; but a first frame
  // at currentTime 0 would gate in cs too — we mirror that by gating against the seeded 0). fork-dt-gate.
  if (st->dampInited && std::fabs(currentTime - st->lastEvalTime) < (double)kMinTimeElapsed) {
    // Gated frame: re-publish the persisted damped values WITHOUT damping (cs returns; Result already holds
    // the previous _dampedValues via the prior AddRange — here we re-emit them).
    *c.output = st->dampedValues;
    return;
  }

  // cs:28-29 — empty/absent input → early return WITHOUT touching output (fork-empty-leaves-output).
  if (!c.inputLists || c.inputLists->empty() || (*c.inputLists)[0].empty()) return;
  const std::vector<float>& list = (*c.inputLists)[0];
  const int n = (int)list.size();

  // cs:41-42 — MatchListLength: grow with 0 / shrink to the input count (preserves leading values on grow).
  if ((int)st->dampedValues.size() != n) st->dampedValues.resize(n, 0.0f);
  if ((int)st->velocities.size() != n) st->velocities.resize(n, 0.0f);

  st->lastEvalTime = currentTime;  // cs:44 — remember the evaluated time
  st->dampInited = true;

  const float damping = floatListParam(c.params, "Damping", 0.95f);  // .t3 default 0.95

  for (int i = 0; i < n; ++i) {
    const float value = list[i];
    // injectBug (golden teeth): DISABLE the cross-frame persistence — read the previous damped value as if it
    // were the current input (prev := value), so the damp collapses to the input and the output JUMPS instead
    // of ramping. With the bug output==input; without it output is the damped Lerp. Bites the REAL cook.
    const float prev = floatListInjectBug() ? value : st->dampedValues[i];
    // Method 0 (LinearInterpolation, the .t3 default): DampenFloat → Lerp(value, prev, damping). velocity is
    // not used by this method (stays 0). fork-method-1-deferred handles the unported spring variant.
    float dampedValue = lerpf(value, prev, damping);  // cs:52 (Method 0)
    if (std::isnan(dampedValue) || std::isinf(dampedValue)) dampedValue = 0.0f;  // cs:54 ApplyDefaultIfInvalid
    st->dampedValues[i] = dampedValue;  // cs:57 — PERSIST (the cross-frame memory)
    // velocity stays 0 for Method 0 (cs:58 _velocities[i]=velocity; velocity untouched → 0 / ApplyDefault 0).
  }

  *c.output = st->dampedValues;  // cs:60 — Result.Value.AddRange(_dampedValues)
}

}  // namespace

// Self-registration. stateful=true (the cook driver applies the cook-once advance guard). Ports: "out"
// (FloatList output); "Values" (FloatList input, .t3 default {5,17}); Damping/UseAppRunTime/Method params.
//   .t3 defaults baked into PortSpec.def: Damping 0.95, Method 0, UseAppRunTime 0 (false).
static const FloatListOp _reg_dampfloatlist{
    {"DampFloatList", "DampFloatList",
     {{"out", "out", "FloatList", false},
      {"Values", "Values", "FloatList", true},
      {"Damping", "Damping", "Float", true, 0.95f, 0.0f, 1.0f, Widget::Slider, {}, /*pinless=*/true},
      {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, /*pinless=*/true},
      {"UseAppRunTime", "UseAppRunTime", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       /*pinless=*/true}},
     /*evaluate=*/nullptr},
    cookDampFloatList, /*stateful=*/true};

}  // namespace sw
