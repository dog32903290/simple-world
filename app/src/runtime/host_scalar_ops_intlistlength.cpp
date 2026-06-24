// IntListLength host-scalar op (host-scalar self-registration seam leaf — IntList input → host Float
// output, the int-fold twin of FloatListLength). TiXL authority:
// external/tixl/Operators/Lib/numbers/ints/IntListLength.cs:13-23 (verbatim):
//
//   IntListLength.cs Update():
//     var list = Input.GetValue(context);
//     if (list == null) { Length.Value = 0; return; }
//     Length.Value = list.Count;
//
//   Ports:  Input  = InputSlot<List<int>> (the ONE int list to measure).
//   Output: Length = Slot<int>            (the element count).
//
// BACKWARD-TRACE (blueprint §1 "每顆 backward-trace .cs 確認無 overflow/bitwise"): IntListLength reads ONLY
// list.Count — NO arithmetic on the int ELEMENTS, no overflow, no bitwise. So the int-list fold is FREE: an
// IntList dissolves to the FloatList currency (sw has no Int port type — Cut32 convention), and its Count is
// IDENTICAL whether the elements are stored as int or float. The op is byte-identical to FloatListLength
// except the input port's TiXL element type (which sw dissolves away). fork-intlist-dissolves-to-floatlist.
//
// EVAL-SIDE LAYOUT: single-output host-scalar contract (same as FloatListLength) — cooked by the driver's
// host-scalar branch (gathers the FloatList-currency input → inputLists[0]), result stored in floatListBuf
// (transport) AND Node::outCache[0] (the BRIDGE evalFloat reads).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's Length is Slot<int>; sw has no Int port → count dissolves
//     int→Float (Cut32 convention, same as FloatListLength.Length + StringLength).
//   - fork-intlist-dissolves-to-floatlist: TiXL's Input is List<int>; sw has no IntList currency — ints
//     ride the FloatList rail (Cut32). Count is type-agnostic so this is exact.
//   - fork-intlistlength-null-is-empty: TiXL guards list == null → 0; sw's gather yields no entry for an
//     unwired input → empty → 0 (identical to null→0 and the TiXL empty-list Count==0).
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

namespace {

// IntListLength: count = (wired list present) ? inputLists[0].size() : 0. Mirrors IntListLength.cs:
// list == null → 0 (here: no wired input → inputLists empty → 0); else list.Count.
void cookIntListLength(HostScalarCookCtx& c) {
  if (!c.output) return;
  size_t count = 0;
  if (c.inputLists && !c.inputLists->empty()) count = (*c.inputLists)[0].size();  // IntListLength.cs:22
  *c.output = (float)count;  // int→Float host scalar
  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites via downstream
  // evalFloat (NOT by flipping the expected value). Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
//   Ports: "Length" = the Float output (int count dissolved to Float; host scalar via outCache + floatListBuf);
//          "Input"  = the int list input, dissolved onto the FloatList currency rail.
// Output port FIRST (index 0) so outIdx 0 = Length, matching the host-scalar layout. The list input has no
// Float-rail def/widget fields (it is a FloatList-currency port).
static const HostScalarOp _reg_intlistlength{
    {"IntListLength", "IntListLength",
     {{"Length", "Length", "Float", false},
      {"Input", "Input", "FloatList", true}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookIntListLength};

}  // namespace sw
