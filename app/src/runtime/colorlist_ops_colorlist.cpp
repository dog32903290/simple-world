// ColorList colorlist op (TypeOperator passthrough — colorlist in -> same colorlist out, a fresh copy).
// TiXL authority: external/tixl/Operators/TypeOperators/Collections/ColorList.cs (verbatim below).
//
//   ColorList.cs Update():                                                       // cs:16-20
//     var list = List.GetValue(context);                                         // cs:18
//     Result.Value = [..list ?? []];                                            // cs:19 — copy (or empty)
//
//   Ports: List   = InputSlot<List<Vector4>>  (one ColorList input)              // cs:22-23
//          Result = Slot<List<Vector4>>        (the copied color list)           // cs:8-9
//
// This is the cleanest ColorList CONSUMER: it reads ONE upstream ColorList and re-emits a fresh copy.
// `[..list ?? []]` is C# spread-into-new-collection — it produces a NEW List<Vector4> with the SAME
// elements (a copy, not aliasing the input). When `list` is null (unwired) it yields the empty list.
// Our cook mirrors that exactly: clear the driver-owned output, then append every element of the first
// wired ColorList input. Unwired → inputLists empty → output stays empty (== `?? []`).
//
// This op exercises the ColorList-INPUT recursion branch the currency already ships (cookColorListNode /
// cookResidentColorList both gather "ColorList" input ports into ColorListCookCtx::inputLists) — it is
// the first consumer of that branch. Pure HOST data: flat + R-2 resident, no GPU.
//
// NO FORK: a single Vector4-LIST input (not a single color) maps 1:1 to one "ColorList" dataType input
// port. No vec4-as-4-floats split (that fork is only for single Vector4 / Vector4 MultiInput, e.g.
// ColorsToList) — a List<Vector4> is already the ColorList currency.
#include <simd/simd.h>

#include "runtime/colorlist_op_registry.h"  // ColorListOp / ColorListCookCtx / colorListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

int runColorListSelfTest(bool injectBug);

namespace {

// ColorList: Result.Value = [..list ?? []]. Clear output, copy the FIRST wired ColorList input (if any).
void cookColorList(ColorListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // start fresh — the spread builds a NEW list each Update
  if (c.inputLists && !c.inputLists->empty()) {
    // cs:18-19 — `list` is the single wired input; copy its elements into the fresh output list. A single
    // (non-MultiInput) ColorList port gathers exactly one source into inputLists[0] (the driver pushes the
    // FIRST wire only for a single input). Unwired → inputLists empty → output stays [] (the `?? []` leg).
    const std::vector<simd::float4>& in = (*c.inputLists)[0];
    c.output->assign(in.begin(), in.end());
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop the last color) so the golden's RED
  // case bites here, NOT by flipping the expected value. Off in production.
  if (colorListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static ColorListOp — independent leaf .cpp (no shared edit point).
//   Ports: "List" = the ColorList input (TypeOperator's single Vector4-list input);
//          "out"  = the ColorList output (the copied host color list).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const ColorListOp _reg_colorlist{
    {"ColorList", "ColorList",
     {{"List", "List", "ColorList", true},
      {"out", "out", "ColorList", false}},
     /*evaluate=*/nullptr},  // ColorList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookColorList};

}  // namespace sw
