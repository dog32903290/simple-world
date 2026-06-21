// CombineColorLists colorlist op (MultiInput<List<Vector4>> -> one concatenated List<Vector4>).
// TiXL authority: external/tixl/Operators/Lib/numbers/color/CombineColorLists.cs (verbatim below).
//
//   CombineColorLists.cs Update():                                               // cs:14-33
//     Selected.Value ??= [];                                                     // cs:16
//     var list = Selected.Value;  list.Clear();                                  // cs:18-19
//     var connections = InputLists.GetCollectedTypedInputs();                    // cs:21
//     if (connections == null || connections.Count == 0) return;                 // cs:22-23
//     foreach (var i in connections) {                                           // cs:25
//         var inputList = i.GetValue(context);                                    // cs:27
//         if (inputList is { Count: > 0 }) list.AddRange(inputList);              // cs:28-29
//     }                                                                          // cs:30
//     InputLists.DirtyFlag.Clear();                                              // cs:32 (dirty bookkeeping, elided)
//
//   Ports: InputLists = MultiInputSlot<List<Vector4>>  (N wired ColorLists)       // cs:37-38
//          Selected   = Slot<List<Vector4>>            (the concatenation)        // cs:6-7
//
// Body: clear output, then for each gathered ColorList input (wire-declaration order) append all its
// elements. The `Count > 0` guard only SKIPS empty inputs — appending an empty range adds nothing, so
// the guard is a no-op for the concatenation RESULT (it exists in TiXL purely to avoid the AddRange call
// on an empty/absent list). Our cook appends every gathered list unconditionally; an empty input list
// contributes zero elements, so the result is byte-identical to the guarded foreach. The early-return on
// zero connections (cs:22-23) is also a no-op for the result: zero gathered lists -> nothing appended ->
// the already-cleared output stays empty.
//
// The currency's ColorList-input recursion branch (cookColorListNode / cookResidentColorList both gather
// "ColorList" input ports — MultiInput-expanded into wire-declaration order — into ColorListCookCtx::
// inputLists) hands us exactly `connections` in wire order. Pure HOST data: flat + R-2 resident, no GPU.
//
// NO FORK: a MultiInput of Vector4-LISTS maps 1:1 to ONE "ColorList" MultiInput port (multiInput=true).
// This is NOT the vec4-as-4-floats split (that fork is only for single Vector4 / Vector4 MultiInput) —
// each wire here already carries a whole List<Vector4> (the ColorList currency), so the MultiInput stays
// a single ColorList port whose N wires the driver expands into inputLists[0..N-1].
#include <simd/simd.h>

#include "runtime/colorlist_op_registry.h"  // ColorListOp / ColorListCookCtx / colorListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

int runCombineColorListsSelfTest(bool injectBug);

namespace {

// CombineColorLists: list.Clear(); foreach gathered ColorList input -> list.AddRange(inputList).
void cookCombineColorLists(ColorListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // cs:19 — Selected.Value.Clear()
  if (c.inputLists) {
    // cs:25-30 — append every gathered list's elements, in wire-declaration order (the driver expanded the
    // MultiInput's N wires into inputLists[0..N-1] in connection order). Empty inputs add nothing (== the
    // `Count > 0` guard's effect on the result).
    for (const std::vector<simd::float4>& in : *c.inputLists)
      c.output->insert(c.output->end(), in.begin(), in.end());
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop the last color) so the golden's RED
  // case bites here, NOT by flipping the expected value. Off in production.
  if (colorListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static ColorListOp — independent leaf .cpp (no shared edit point).
//   Ports: "InputLists" = the ColorList MultiInput (N wired ColorLists, concatenated in wire order);
//          "out"        = the ColorList output (the concatenation).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,labels,pinless,vecArity,multiInput.
static const ColorListOp _reg_combinecolorlists{
    {"CombineColorLists", "CombineColorLists",
     {{"InputLists", "InputLists", "ColorList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "ColorList", false}},
     /*evaluate=*/nullptr},  // ColorList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookCombineColorLists};

}  // namespace sw
