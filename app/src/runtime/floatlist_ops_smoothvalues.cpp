// SmoothValues floatlist op (floatlist self-registration seam leaf — List<float> -> List<float>, a
// forward-window box average). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/SmoothValues.cs (verbatim below).
//
//   SmoothValues.cs Update():
//     var windowSize = WindowSize.GetValue(context).Clamp(1, 10);            // int, clamp to [1,10]
//     var list = Input.GetValue(context);
//     if (list == null || list.Count == 0) return;                          // empty/absent → leave output
//     if (Result.Value == null || Result.Value.Count != list.Count)
//         Result.Value = new List<float>();
//     Result.Value.Clear();
//     for (index = 0; index < list.Count; index++):
//         float sum=0, count=0;
//         SampleAtIndex(index, ref sum, ref count);                         // sample index ONCE (extra)
//         for (windowIndex = 0; windowIndex < windowSize; windowIndex++):
//             SampleAtIndex(index + windowIndex, ref sum, ref count);       // then index..index+window-1
//         Result.Value.Add(count == 0 ? float.NaN : sum/count);
//     // local SampleAtIndex(i): if (i<0 || i>=list.Count) return; sum += list[i]; count++;
//
// THE OP IS STATELESS — despite living in process/ alongside the cross-frame KeepFloatValues/AmplifyValues,
// SmoothValues holds NO persistent field. Result.Value is rebuilt every cook purely from the CURRENT Input
// (the `Result.Value.Count != list.Count` guard only re-allocs the output container — it carries no value
// memory; Clear() then wipes it before the fill). So it needs NO state slot and is idempotent under the
// FloatList resident pull-driven re-cook — correct on flat AND production resident with zero seam work,
// exactly the clean wave-2 leaf shape (clone of RemapFloatList: FloatList in → FloatList out, closed-form).
//
// FORK (named, load-bearing): fork-double-count-index. SmoothValues.cs samples `index` ONCE up front
//   (the pre-loop SampleAtIndex(index)) and then AGAIN as windowIndex==0 inside the loop. So the element
//   AT `index` is weighted twice and the window spans [index, index+windowSize-1] forward. The mean is
//   sum / count where count = 1 (pre-loop) + (#in-bounds samples in [index, index+windowSize-1]). This is
//   a VERBATIM transcription of the .cs (NOT a symmetric/centered window) — preserving the double weight
//   and the forward-only window is the parity contract.
//
// FORK (named): fork-empty-leaves-output. .cs `return`s on an empty/absent input WITHOUT touching
//   Result.Value — so the previously-cooked output persists. On this host rail the driver hands a fresh
//   *output each cook (floatListBuf scratch, cleared by the driver's owner each frame); the faithful
//   empty-branch behaviour here is "leave *output untouched" (the op writes nothing) → an empty input
//   yields whatever the driver pre-seeded (empty by construction). The golden drives a non-empty input.
//
//   .cs DEFAULTS: WindowSize = new() → int default 0 → Clamp(1,10) ⇒ 1 (a single forward sample, but the
//   pre-loop SampleAtIndex still double-counts `index` ⇒ window {index, index} ⇒ mean == list[index]).
#include <cmath>  // (no NaN literal needed: count is always >= 1 for an in-bounds index, see below)

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// SmoothValues: forward-window box average of the input list, per SmoothValues.cs Update() (verbatim).
void cookSmoothValues(FloatListCookCtx& c) {
  if (!c.output) return;
  // .cs: empty/absent input → early return WITHOUT touching the output (leaves prior frame). Here the
  // driver-owned *output is fresh scratch; faithful "leave untouched" = write nothing → it stays empty.
  if (!c.inputLists || c.inputLists->empty() || (*c.inputLists)[0].empty()) return;
  const std::vector<float>& in = (*c.inputLists)[0];

  // WindowSize.Clamp(1, 10) — default 0 clamps up to 1 (int param; dissolve via lround-free cast is exact
  // for the small integer range, but cast-from-float of an integer-valued param is exact regardless).
  int windowSize = (int)floatListParam(c.params, "WindowSize", 0.0f);
  if (windowSize < 1) windowSize = 1;
  else if (windowSize > 10) windowSize = 10;

  c.output->clear();
  c.output->reserve(in.size());
  const int n = (int)in.size();
  for (int index = 0; index < n; ++index) {
    float sum = 0.0f;
    int count = 0;
    // Pre-loop SampleAtIndex(index) — index is in-bounds (0..n-1) so it ALWAYS contributes (the .cs
    // local guard i<0||i>=Count never fires here). This is the double-count of `index` (fork above).
    sum += in[index];
    ++count;
    // Forward window SampleAtIndex(index + windowIndex) for windowIndex in [0, windowSize-1].
    for (int windowIndex = 0; windowIndex < windowSize; ++windowIndex) {
      const int i = index + windowIndex;
      if (i < 0 || i >= n) continue;  // SampleAtIndex out-of-bounds guard (.cs local)
      sum += in[i];
      ++count;
    }
    // count is >= 1 always (the pre-loop sample), so the `count==0 → NaN` .cs branch is unreachable here;
    // transcribed for fidelity: mean = sum / count.
    c.output->push_back(count == 0 ? std::nanf("") : sum / (float)count);
  }

  // Test-only: corrupt the REAL output (drop last) so the golden's RED bites on the actual cook path.
  if (floatListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first (FloatList output); "Input" (FloatList input); "WindowSize" (int enum-free Slider,
//          pinless param, default 0 → clamps to 1). int rides the Float value rail (Cut32 IntList fold:
//          WindowSize carries no overflow/bitwise — a small [1,10] clamp — so the float dissolve is exact).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const FloatListOp _reg_smoothvalues{
    {"SmoothValues", "SmoothValues",
     {{"out", "out", "FloatList", false},
      {"Input", "Input", "FloatList", true},
      {"WindowSize", "WindowSize", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Slider, {}, /*pinless=*/true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookSmoothValues};

}  // namespace sw
