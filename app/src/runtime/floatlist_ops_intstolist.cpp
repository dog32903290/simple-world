// IntsToList floatlist op (floatlist self-registration seam leaf — MultiInput<int> -> List<int>,
// dissolved onto the FloatList host-currency since sw has no Int port). TiXL authority:
// external/tixl/Operators/Lib/numbers/ints/IntsToList.cs (verbatim below).
//
//   IntsToList.cs Update():
//     Result.Value.Clear();
//     foreach (var input in Input.GetCollectedTypedInputs())
//         Result.Value.Add(input.GetValue(context));
//     Input.DirtyFlag.Clear();
//
//   Ports: Input = MultiInputSlot<int> (the ONE variable-length list of scalar ints).
//   Output: Result = Slot<List<int>>.
//
// This is the INT twin of FloatsToList — structurally identical (clear, append each gathered scalar in
// wire-declaration order). The cook driver's scalar-MultiInput gather aggregates all wired Float sources
// into ONE list (inputLists[0]); the leaf body copies it verbatim.
//
// FORK (named):
//   - fork-int-dissolve-to-float: sw has NO Int port (Cut32 convention — ints dissolve to Float, no Int
//     rail). So Input is a scalar "Float" MultiInput, and each gathered value is ROUNDED to the nearest
//     integer (std::lround) before storage, faithful to TiXL's List<int> element semantics (the source
//     scalars are integer-valued anyway; the round is belt-and-suspenders for non-integer wired inputs).
//     This is a FREE fold (backward-trace: IntsToList does NO arithmetic — only collect+add — so there
//     is NO overflow / bitwise concern; the values pass straight through. LIST_SEAM_BLUEPRINT §1 IntList).
//   - fork-intstolist-empty-when-unwired: GetCollectedTypedInputs() yields connected inputs only — an
//     unwired MultiInput → empty list (driver: 0 wired → inputLists[0] empty → output empty).
#include <cmath>  // std::lround

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// IntsToList: Result.Clear(); foreach gathered scalar int -> Result.Add(round(scalar)).
void cookIntsToList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // IntsToList.cs — Result.Value.Clear()
  if (c.inputLists && !c.inputLists->empty()) {
    const std::vector<float>& gathered = (*c.inputLists)[0];
    for (float v : gathered)                                   // IntsToList.cs — Add each collected int
      c.output->push_back((float)std::lround(v));              // int dissolve: store as integer-valued float
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "Input" = scalar Float MultiInput (int dissolved; driver expands all wires into one list);
//          "out"   = the FloatList output (the dissolved List<int>).
static const FloatListOp _reg_intstolist{
    {"IntsToList", "IntsToList",
     {{"Input", "Input", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookIntsToList};

}  // namespace sw
