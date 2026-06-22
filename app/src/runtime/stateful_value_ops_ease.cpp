// runtime/stateful_value_ops_ease — Ease family (Ease / EaseVec2 / EaseVec3).
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
// The 30 easing curves live in the shared stateful_value_ops_easing.h util header.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn / getInC / lerpf / enumOf / clamp01
#include "runtime/stateful_value_ops_easing.h"    // applyEasing (+ all curve fns)

namespace sw {
namespace {

// --- Ease / EaseVec2 / EaseVec3 (TiXL float/process/Ease.cs, vec2/EaseVec2.cs, vec3/EaseVec3.cs) ---
// Time-based eased re-target: when the input changes by >0.001 the animation RESTARTS from the
// current output toward the new target over `Duration` seconds, shaped by Interpolation×Direction
// (EasingFunctions). Uses the wall `time` param as TiXL's currentTime; `dt` unused (absolute-time).
// State (per spec) packs startTime + initial/target/prevInput per component, PLUS one shared
// `prevEased` float at the END (see below):
//   Ease     s[0]=startTime, s[1]=initial, s[2]=target, s[3]=prevInput, s[4]=prevEased
//   EaseVec2 s[0]=startTime, s[1..2]=initial, s[3..4]=target, s[5..6]=prevInput, s[7]=prevEased
//   EaseVec3 s[0]=startTime, s[1..3]=initial, s[4..6]=target, s[7..9]=prevInput, s[10]=prevEased (≤12)
// Why prevEased: TiXL captures `_initialValue = Result.Value` on restart — i.e. LAST frame's output.
// frame_cook hands easeImpl a freshly-zeroed out[] each frame (it does NOT carry the prior Result),
// so we must reconstruct it. Between restarts initial/target are fixed, so last-frame's Result is
// exactly lerp(initial,target,prevEased) — and easing is scalar (one t for all components), so ONE
// stored float reconstructs the previous Result for any N. This is faithful to TiXL and fits s[12]
// (the spec's "out[] carries prior Result" assumption is broken by frame_cook's zeroing; this is the
// minimal correct substitute — flagged in the dossier).
// Fork (named) — same precedent as Damp/Spring (batch25):
//   • UseAppRunTime input DROPPED — frame_cook always cooks once per frame with wall time (TiXL's
//     non-default RunTimeInSecs branch has no analog here; we always use context-time = `time`).
//   • The 1ms MinTimeElapsedBeforeEvaluation early-return DROPPED — frame_cook cooks exactly once
//     per frame, so the sub-ms double-eval that guard prevents cannot occur.
//   • The __MotionBlurPass skip DROPPED — no motion-blur pass system in this runtime.
// Faithful: _previousInput field-inits to 0, so on frame 1 a nonzero input triggers an immediate
// restart capturing initialValue=prevResult=0 (matches TiXL's exact first-frame behavior).
void easeImpl(const std::map<std::string, float>& in, float time, StatefulValueState& st,
              float out[3], int N) {
  float duration = getIn(in, "Duration", 1.0f);
  if (duration == 0.0f) duration = 0.0001f;  // TiXL guard
  const int direction = enumOf(in, "Direction");
  const int easeMode = enumOf(in, "Interpolation");

  float input[3] = {0, 0, 0};
  for (int c = 0; c < N; ++c) input[c] = (N == 1) ? getIn(in, "Value", 0.0f) : getInC(in, "Value", c);

  float& startTime = st.s[0];
  float* initial = &st.s[1];           // s[1..N]
  float* target = &st.s[1 + N];        // s[1+N..1+2N-1]
  float* prevInput = &st.s[1 + 2 * N]; // s[1+2N..1+3N-1]
  float& prevEased = st.s[1 + 3 * N];  // shared scalar t of last frame's output

  // Restart trigger: scalar abs() / vec Distance() > 0.001 (TiXL Math.Abs / VectorN.Distance).
  float distSq = 0.0f;
  for (int c = 0; c < N; ++c) {
    const float d = input[c] - prevInput[c];
    distSq += d * d;
  }
  if (std::sqrt(distSq) > 0.001f) {
    startTime = time;
    for (int c = 0; c < N; ++c) {
      initial[c] = lerpf(initial[c], target[c], prevEased);  // TiXL _initialValue = Result.Value (last frame)
      target[c] = input[c];
    }
  }

  const float elapsed = time - startTime;
  const float progress = clamp01(elapsed / duration);
  const float eased = applyEasing(progress, direction, easeMode);
  for (int c = 0; c < N; ++c) {
    out[c] = lerpf(initial[c], target[c], eased);
    prevInput[c] = input[c];
  }
  prevEased = eased;
}
void stepEase(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { easeImpl(in, time, st, out, 1); }
void stepEaseVec2(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { easeImpl(in, time, st, out, 2); }
void stepEaseVec3(const std::map<std::string, float>& in, float, float time, StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) { easeImpl(in, time, st, out, 3); }

}  // namespace

static const StatefulOpReg _reg_Ease{"Ease", stepEase};
static const StatefulOpReg _reg_EaseVec2{"EaseVec2", stepEaseVec2};
static const StatefulOpReg _reg_EaseVec3{"EaseVec3", stepEaseVec3};

}  // namespace sw
