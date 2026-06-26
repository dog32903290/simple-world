// FloatListToIntList floatlist op (floatlist self-registration seam leaf — List<float> -> List<int>,
// the per-element float→int truncation, dissolved onto the FloatList host-currency since sw has no Int
// rail). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/conversion/FloatListToIntList.cs (verbatim below).
//
//   FloatListToIntList.cs Update():
//     var floatValues = FloatList.GetValue(context);
//     if (floatValues == null) { Result.Value = new List<int>(); return; }     // null → empty
//     // "Casting a float to an int truncates the decimal part (e.g., 9.8f becomes 9)."
//     Result.Value = floatValues.Select(f => (int)f).ToList();                 // (int)f per element
//
//   Ports: FloatList = InputSlot<List<float>>.  Output: Result = Slot<List<int>>.
//
// EVAL-SIDE LAYOUT: single FloatList input gathered as inputLists[0]; output is the dissolved List<int>
// (integer-valued floats) on the FloatList currency. NO Float params. STATELESS — closed-form per element.
//
// FORKS (named):
//   - fork-int-dissolve-to-floatlist: TiXL Result is List<int>; sw has NO Int rail (Cut32 convention —
//     ints ride the FloatList currency). Each truncated int is stored as an integer-valued float. The
//     Count + every element value are byte-identical to a real List<int> at these magnitudes (list-index
//     scale — no overflow; LIST_SEAM_BLUEPRINT §1 IntList). This op does NO arithmetic on the ints (only
//     a per-element cast), so the fold is FREE.
//   - fork-csharp-int-cast-truncates-TOWARD-ZERO: C# `(int)f` truncates toward zero (9.8→9, -1.9→-1),
//     NOT floor (-1.9→-2). This is the LOAD-BEARING semantic — the golden asserts the NEGATIVE case to
//     pin trunc≠floor. Implemented via std::truncf (round toward zero), NOT std::floor / std::lround.
//   - fork-floatlist-null-is-empty: TiXL guards floatValues == null → empty; sw's gather yields no entry
//     for an unwired input → empty list → empty output (identical to null→empty).
#include <cmath>  // std::truncf

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// FloatListToIntList: Result = input.Select(f => (int)f). (int)f = truncate toward zero (C# semantics).
void cookFloatListToIntList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  // Empty/absent input → empty output (FloatListToIntList.cs null → new List<int>()).
  if (c.inputLists && !c.inputLists->empty()) {
    const std::vector<float>& in = (*c.inputLists)[0];
    c.output->reserve(in.size());
    for (float f : in)
      c.output->push_back(std::truncf(f));  // (int)f — truncate TOWARD ZERO (not floor)
  }

  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first (FloatList output = dissolved List<int>); "FloatList" input.
static const FloatListOp _reg_floatlisttointlist{
    {"FloatListToIntList", "FloatListToIntList",
     {{"out", "out", "FloatList", false},
      {"FloatList", "FloatList", "FloatList", true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookFloatListToIntList};

}  // namespace sw
