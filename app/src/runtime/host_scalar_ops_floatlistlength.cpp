// FloatListLength host-scalar op (host-scalar self-registration seam leaf — the BRIDGE's minimal proof:
// FloatList input → host Float output, flowing DOWNSTREAM through evalFloat via Node::outCache).
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/basic/FloatListLength.cs:14-24 (verbatim):
//
//   FloatListLength.cs Update():
//     var list = Input.GetValue(context);
//     if (list == null) { Length.Value = 0; return; }
//     Length.Value = list.Count;
//
//   Ports: Input  = InputSlot<List<float>>  (the ONE list to measure).
//   Output: Length = Slot<int>              (the element count).
//
// EVAL-SIDE LAYOUT: FloatListLength CONSUMES a FloatList and PRODUCES a host scalar (a number), not a
// FloatList — so it is NOT a FloatList PRODUCER (no FloatListCookFn), and NOT a pure value op (its
// INPUT is a host list that evalFloat cannot see). It is cooked by the driver's HOST-SCALAR branch
// (point_graph.cpp), which gathers its one FloatList input via cookFloatListNode → inputLists[0],
// runs this leaf to compute the count, and stores the result BOTH as a 1-element host FloatList
// (Impl::floatListBuf — transport, readback via debugCookedFloatList) AND in Node::outCache[0] (the
// BRIDGE — evalFloat reads it via the generalised stateful escape hatch). So a downstream Float INPUT
// port wired to FloatListLength.Length reads the count (3.0 for a 3-element list) — the seam's headline.
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's Length is Slot<int>; sw has no Int port type, so the
//     count dissolves int→Float (Cut32 convention, same as StringLength + every Int-returning op).
//   - fork-floatlist-scalar-via-outcache: the host scalar rides Node::outCache (the bridge) in
//     PARALLEL with the legacy floatListBuf transport — evalFloat reaches outCache, not floatListBuf.
//   - fork-floatlistlength-null-is-empty: TiXL guards list == null → 0. sw's gather always yields a
//     (possibly empty) list per wired source — an UNWIRED FloatList input contributes NO entry, so
//     inputLists is empty → count 0, which is identical to TiXL's null→0 and empty-list→0 (Count==0).
#include "runtime/graph.h"                      // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"    // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

namespace {

// FloatListLength: count = (wired list present) ? inputLists[0].size() : 0. Mirrors FloatListLength.cs:
// list == null → 0 (here: no wired FloatList source → inputLists empty → 0); else list.Count.
void cookFloatListLength(HostScalarCookCtx& c) {
  if (!c.output) return;
  size_t count = 0;
  if (c.inputLists && !c.inputLists->empty()) count = (*c.inputLists)[0].size();  // FloatListLength.cs:23
  *c.output = (float)count;  // int→Float host scalar
  // Test-only: corrupt the REAL output on the actual cook path (sentinel) so the golden's RED bites
  // here via downstream evalFloat, NOT by flipping the expected value. Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
// Feeds hostScalarSpecSink() + hostScalarCookFns() + hostScalarTypes() during pre-main dynamic init.
//   Ports: "Length" = the Float output (int dissolved to Float; host scalar via outCache + floatListBuf);
//          "Input"  = the FloatList input (the list to measure).
// Output port FIRST (index 0) so outIdx 0 = Length, matching the StringLength/AudioReaction layout
// (evalFloat's outIdx = output port index). The FloatList input has no Float-rail def/widget fields.
static const HostScalarOp _reg_floatlistlength{
    {"FloatListLength", "FloatListLength",
     {{"Length", "Length", "Float", false},
      {"Input", "Input", "FloatList", true}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookFloatListLength};

}  // namespace sw
