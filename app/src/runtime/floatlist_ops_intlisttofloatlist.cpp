// IntListToFloatList floatlist op (floatlist self-registration seam leaf — List<int> -> List<float>, the
// exact int→float widening; the mirror of FloatListToIntList). Both rails dissolve onto the FloatList
// host-currency since sw has no Int rail. TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/conversion/IntListToFloatList.cs (verbatim below).
//
//   IntListToFloatList.cs Update():
//     var intValues = IntList.GetValue(context);
//     if (intValues == null) { Result.Value = new List<float>(); return; }     // null → empty
//     Result.Value = intValues.Select(i => (float)i).ToList();                 // (float)i per element
//
//   Ports: IntList = InputSlot<List<int>>.  Output: Result = Slot<List<float>>.
//
// EVAL-SIDE LAYOUT: single IntList input dissolves onto the FloatList currency (gathered as inputLists[0]
// — already integer-valued floats); output is a List<float> on the SAME currency. NO Float params.
// STATELESS — closed-form identity per element.
//
// FORKS (named):
//   - fork-intlist-dissolves-to-floatlist: TiXL's IntList is List<int>; sw has NO Int rail — ints ride
//     the FloatList currency as integer-valued floats (Cut32). The widening (float)i is therefore a pure
//     IDENTITY copy on the dissolved representation: the input floats ARE the int values, and (float)i of
//     an int that fits in a float is exact. NO arithmetic, NO overflow (list-index scale, LIST_SEAM_
//     BLUEPRINT §1) — a FREE fold, byte-identical to TiXL Select(i=>(float)i) at these magnitudes.
//   - fork-intlist-null-is-empty: TiXL guards intValues == null → empty; sw's gather yields no entry for
//     an unwired input → empty list → empty output (identical to null→empty).
#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// IntListToFloatList: Result = input.Select(i => (float)i). On the dissolved FloatList currency the input
// is already integer-valued floats, so the widening is an exact identity copy.
void cookIntListToFloatList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  // Empty/absent input → empty output (IntListToFloatList.cs null → new List<float>()).
  if (c.inputLists && !c.inputLists->empty())
    *c.output = (*c.inputLists)[0];  // (float)i per element = identity copy on dissolved ints

  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first (FloatList output = widened List<float>); "IntList" input (dissolved onto FloatList).
static const FloatListOp _reg_intlisttofloatlist{
    {"IntListToFloatList", "IntListToFloatList",
     {{"out", "out", "FloatList", false},
      {"IntList", "IntList", "FloatList", true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookIntListToFloatList};

}  // namespace sw
