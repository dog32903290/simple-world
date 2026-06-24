// SumRange host-scalar op (host-scalar self-registration seam leaf — FloatList + LowerLimit + UpperLimit
// → host Float output, the clamped windowed-sum consumer). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/SumRange.cs:14-28 (verbatim):
//
//   SumRange.cs Update():
//     var list = Input.GetValue(context);
//     if (list == null || list.Count == 0) return;                       // Selected keeps prior (default 0)
//     var lowerLimit = Math.Max(0, LowerLimit.GetValue(context));        // clamp low ≥ 0
//     var upperLimit = Math.Min(list.Count, UpperLimit.GetValue(context)); // clamp high ≤ Count
//     var sum = 0f;
//     for (var index = lowerLimit; index < upperLimit; index++) sum += list[index];
//     Selected.Value = sum;
//
//   Ports: Input      = InputSlot<List<float>> (default new List<float>(20) → non-null EMPTY → Count 0);
//          LowerLimit = InputSlot<int>(0);
//          UpperLimit = InputSlot<int>(0).
//   Output: Selected = Slot<float> (the windowed sum; 0 when list empty/null — Selected never assigned).
//
// EVAL-SIDE LAYOUT: same single-output host-scalar contract as FloatListLength/PickFloatFromList — cooked
// by the driver's host-scalar branch (gathers the ONE FloatList input → inputLists[0], resolves the two
// int-dissolved Float params), result stored in floatListBuf (transport) AND Node::outCache[0] (the BRIDGE
// evalFloat reads).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's LowerLimit/UpperLimit are int; sw has no Int port → they
//     dissolve to Float and are recovered with std::lround (Cut32 convention). The sum is exact (host float).
//   - fork-sumrange-empty-is-zero: list == null OR Count == 0 → Selected unassigned → keeps its default 0.
//     sw's gather: an UNWIRED FloatList input → no entry → empty → 0; a wired empty list → Count 0 → 0.
//   - fork-sumrange-default-upper-zero: TiXL's UpperLimit default is 0 → Min(Count,0)=0 → loop body never
//     runs → sum 0. Faithful: with the default (unwired) UpperLimit the window is EMPTY (sum 0), matching
//     TiXL exactly — the user must raise UpperLimit to sum anything.
#include <algorithm>  // std::max, std::min
#include <cmath>      // std::lround

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug / hostScalarParam

namespace sw {

namespace {

// SumRange: empty/absent list → 0; else sum of list[lower .. upper-1] with lower=max(0,Lower),
// upper=min(Count,Upper). Mirrors SumRange.cs:17-27 verbatim (clamp-then-windowed-accumulate).
void cookSumRange(HostScalarCookCtx& c) {
  if (!c.output) return;
  *c.output = 0.0f;  // SumRange.cs:17-20 — empty/null list → Selected stays default (0)
  if (c.inputLists && !c.inputLists->empty()) {
    const std::vector<float>& list = (*c.inputLists)[0];
    if (!list.empty()) {  // SumRange.cs:17 — guard list.Count == 0
      const int rawLower = (int)std::lround(hostScalarParam(c.params, "LowerLimit", 0.0f));  // int dissolve
      const int rawUpper = (int)std::lround(hostScalarParam(c.params, "UpperLimit", 0.0f));  // int dissolve
      const int lower = std::max(0, rawLower);                       // SumRange.cs:21 — Math.Max(0, Lower)
      const int upper = std::min((int)list.size(), rawUpper);        // SumRange.cs:22 — Math.Min(Count, Upper)
      float sum = 0.0f;
      for (int index = lower; index < upper; ++index) sum += list[(size_t)index];  // SumRange.cs:24-26
      *c.output = sum;  // SumRange.cs:27 — Selected.Value = sum
    }
  }
  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites via downstream
  // evalFloat (NOT by flipping the expected value). Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
//   Ports: "Selected"   = the Float output (the windowed sum; host scalar via outCache + floatListBuf);
//          "Input"      = the FloatList input (the list to sum over);
//          "LowerLimit" = inclusive low index (int dissolved to Float; resolved via the value spine);
//          "UpperLimit" = exclusive high index (int dissolved to Float; resolved via the value spine).
// Output port FIRST (index 0) so outIdx 0 = Selected, matching the host-scalar layout. Both limits default
// 0 (TiXL InputSlot<int>(0)); their range is wide+symmetric (negative lower clamps to 0, large upper clamps
// to Count — the .cs clamps both).
static const HostScalarOp _reg_sumrange{
    {"SumRange", "SumRange",
     {{"Selected", "Selected", "Float", false},
      {"Input", "Input", "FloatList", true},
      {"LowerLimit", "LowerLimit", "Float", true, 0.0f, -100000.0f, 100000.0f},
      {"UpperLimit", "UpperLimit", "Float", true, 0.0f, -100000.0f, 100000.0f}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookSumRange};

}  // namespace sw
