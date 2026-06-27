// AmplifyValues floatlist op (floatlist self-registration seam leaf — the FIRST cross-frame state consumer
// on the FLOATLIST rail). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/AmplifyValues.cs (verbatim Update() below; cited inline).
//
//   AmplifyValues.cs Update():
//     var list = Input.GetValue(context);
//     if (list == null || list.Count == 0) return;                          // empty/absent → leave output
//     if (_averagedValues.Length != list.Count) {                           // (re)size on count change
//         _averagedValues = new float[list.Count];                          //   → all 0
//         _lastValues     = new float[list.Count];                          //   → all 0
//         _output         = new List<float>(list.Count); _output.AddRange(Repeat(0f, list.Count));
//     }
//     // CHANGE DETECTION: scan list vs _lastValues; hasChanged = any element differs.
//     for (index2 = 0; index2 < list.Count; index2++)
//         if (list[index2] != _lastValues[index2]) { hasChanged = true; break; }
//     if (hasChanged) {
//         float smoothing       = Smoothing.GetValue(context);
//         var   mixAverage      = MixAverage.GetValue(context);
//         var   mixCurrent      = MixCurrent.GetValue(context);
//         var   mixAboveAverage = MixAboveAverage.GetValue(context);
//         for (index = 0; index < list.Count; index++) {
//             var v = list[index];
//             _lastValues[index] = v;                                       // remember THIS frame's input
//             if (double.IsNaN(v)) v = 0;
//             var smoothed = MathUtils.Lerp(v, _averagedValues[index], smoothing);  // a + (b-a)*t
//             if (float.IsNaN(smoothed) || float.IsInfinity(smoothed)) smoothed = 0;
//             _averagedValues[index] = smoothed;                            // PERSIST the running average
//             _output[index] = (v - smoothed).Clamp(0, 1000) * mixAboveAverage
//                              + v * mixCurrent
//                              + smoothed * mixAverage;
//         }
//     }
//     Output.Value = _output;                                              // publish (even if unchanged)
//   Fields: _averagedValues / _lastValues / _output — PERSISTENT per node (cs:79-81).
//
// THE SEAM THIS LEAF PROVES: _averagedValues / _lastValues / _output are PER-NODE fields that SURVIVE
// across frames. Lerp(v, _averagedValues, smoothing) damps the running average TOWARD the input each frame
// (with smoothing=0.95, the average chases the input slowly: 5% of the gap closes per frame). So feeding a
// step input does NOT make the output jump — it RAMPS toward the new value over many frames. That cross-
// frame ramp is the whole point; it needs the new FloatListCookCtx::state slot (FloatListState). The driver
// owns + threads it — flat: Impl::floatListState[flatKey(id)] (point_graph_hostvalue_cook.cpp); resident
// (production): the residentFloatListState() process static keyed by path (resident_host_scalar_cook.cpp).
// This leaf reads + mutates *state, then COPIES it to *output (output = the per-frame readback channel).
//
// ★.t3 DEFAULTS (AmplifyValues.t3 — these OVERRIDE the C# InputSlot `new(0)` constructor defaults!):
//   Smoothing = 0.95 ; MixAverage = 1.0 ; MixCurrent = 0.0 ; MixAboveAverage = 0.0 ; Input = {5.0, 17.0}.
//   At these defaults the output formula collapses to:
//       output[i] = (v-smoothed).Clamp*0 + v*0 + smoothed*1.0 = smoothed = Lerp(v, _averaged[i], 0.95).
//   So with all-default mixes the op is a PURE damped-average follower (the common "amplify peaks above the
//   running average" preset is reached by raising MixAboveAverage). The golden hand-computes BOTH a default-
//   mix trajectory AND a peak-amplify (MixAboveAverage>0) trajectory.
//
// FORK (named, load-bearing): fork-amplify-change-detect-freeze. AmplifyValues.cs ONLY updates _averaged/
//   _output when `hasChanged` (the input differs from _lastValues). If the input is IDENTICAL frame→frame,
//   the op SKIPS the loop and re-publishes the SAME _output (the average does NOT continue to drift toward a
//   steady input). This is a VERBATIM transcription — preserving the freeze-on-unchanged-input is the parity
//   contract (a naive "always damp" version would keep creeping while the input holds; .cs does not).
//
// FORK (named): fork-empty-leaves-output. .cs `return`s on an empty/absent input WITHOUT touching _output.
//   On this host rail the driver hands a fresh *output each cook (floatListBuf scratch, cleared by the owner
//   each frame); the faithful empty-branch here writes nothing → an empty input yields an empty *output. The
//   golden drives a non-empty input.
#include <cmath>  // std::isnan / std::isinf (the NaN/Inf guards on v and smoothed)

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / FloatListState / injectBug / param
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// MathUtils.Lerp(a, b, t) = a + (b - a) * t  (external/tixl/Core/Utils/MathUtils.cs:305-308).
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Clamp(0, 1000) — the (v - smoothed) above-average term clamp (T3.Core.Utils float Clamp).
inline float clamp01000(float x) { return x < 0.0f ? 0.0f : (x > 1000.0f ? 1000.0f : x); }

// AmplifyValues: damped-average follower with cross-frame state, per AmplifyValues.cs Update() (verbatim).
void cookAmplifyValues(FloatListCookCtx& c) {
  if (!c.output) return;
  // cs: empty/absent input → early return WITHOUT touching output (fork-empty-leaves-output). The driver-
  // owned *output is fresh scratch; faithful "leave untouched" = write nothing → it stays empty.
  if (!c.inputLists || c.inputLists->empty() || (*c.inputLists)[0].empty()) return;
  const std::vector<float>& list = (*c.inputLists)[0];
  const int n = (int)list.size();

  // No state slot (a hand-built ctx with no driver-owned state) → behave as a single fresh frame: size the
  // arrays to 0 (a one-shot), produce the unchanged-from-zero output. Faithful to frame 0 with no persistence.
  FloatListState local;                       // scratch when *state is absent (single-frame fallback)
  FloatListState* st = c.state ? c.state : &local;

  // cs:28-34 — (re)size on first cook / a list-count change: _averaged/_last → all 0, _output → all 0.
  if (!st->inited || (int)st->averagedValues.size() != n) {
    st->averagedValues.assign(n, 0.0f);
    st->lastValues.assign(n, 0.0f);
    st->output.assign(n, 0.0f);
    st->inited = true;
  }

  // cs:36-45 — change detection: hasChanged if ANY element differs from last frame's input.
  bool hasChanged = false;
  for (int i = 0; i < n; ++i) {
    if (list[i] != st->lastValues[i]) { hasChanged = true; break; }  // exact !=, mirror of the .cs (no eps)
  }

  if (hasChanged) {  // cs:47 — freeze the average + output when the input is identical (fork-change-detect-freeze)
    const float smoothing = floatListParam(c.params, "Smoothing", 0.95f);        // .t3 default 0.95
    const float mixAverage = floatListParam(c.params, "MixAverage", 1.0f);       // .t3 default 1.0
    const float mixCurrent = floatListParam(c.params, "MixCurrent", 0.0f);       // .t3 default 0.0
    const float mixAboveAverage = floatListParam(c.params, "MixAboveAverage", 0.0f);  // .t3 default 0.0
    for (int i = 0; i < n; ++i) {
      float v = list[i];
      st->lastValues[i] = v;                  // cs:54 — remember THIS frame's input (set BEFORE NaN clamp)
      if (std::isnan(v)) v = 0.0f;            // cs:55-56
      // injectBug (golden teeth): DISABLE the cross-frame persistence — read the running average as if it
      // were freshly the current input (avg := v), so smoothed == v and the output JUMPS instantly to the
      // input instead of ramping. This is the state-slot's load-bearing proof: with the bug the damp is
      // gone (output == X), without it the output is the damped Lerp (output != X). Bites the REAL cook.
      const float avg = floatListInjectBug() ? v : st->averagedValues[i];
      float smoothed = lerpf(v, avg, smoothing);  // cs:59 — damp toward the running avg
      if (std::isnan(smoothed) || std::isinf(smoothed)) smoothed = 0.0f;  // cs:60-63
      st->averagedValues[i] = smoothed;       // cs:65 — PERSIST the running average (the cross-frame memory)
      st->output[i] = clamp01000(v - smoothed) * mixAboveAverage  // cs:67-69
                      + v * mixCurrent
                      + smoothed * mixAverage;
    }
  }

  *c.output = st->output;  // cs:73 — Output.Value = _output (publish, even on an unchanged frame)
  // (injectBug teeth are applied INSIDE the damp loop above — they kill the cross-frame persistence so the
  // output jumps instantly to the input, proving the state slot is load-bearing. No output-truncation here.)
}

}  // namespace

// Self-registration. File-scope static FloatListOp with stateful=true (the cook driver applies the cook-once
// advance guard). Ports: "out" (FloatList output); "Input" (FloatList input, .t3 default {5,17} — a node-to-
// node wire feeds it here, the const default rides params unused); the 4 mix/smooth Float params.
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
//   .t3 defaults baked into PortSpec.def: Smoothing 0.95, MixAverage 1.0, MixCurrent 0.0, MixAboveAverage 0.0.
static const FloatListOp _reg_amplifyvalues{
    {"AmplifyValues", "AmplifyValues",
     {{"out", "out", "FloatList", false},
      {"Input", "Input", "FloatList", true},
      {"Smoothing", "Smoothing", "Float", true, 0.95f, 0.0f, 1.0f, Widget::Slider, {}, /*pinless=*/true},
      {"MixAverage", "MixAverage", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider, {}, /*pinless=*/true},
      {"MixCurrent", "MixCurrent", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, /*pinless=*/true},
      {"MixAboveAverage", "MixAboveAverage", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Slider, {},
       /*pinless=*/true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookAmplifyValues, /*stateful=*/true};

}  // namespace sw
