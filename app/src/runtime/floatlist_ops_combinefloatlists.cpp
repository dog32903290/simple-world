// CombineFloatLists floatlist op (floatlist self-registration seam leaf — MultiInput<List<float>> ->
// List<float>, the concatenating combiner). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/process/CombineFloatLists.cs (verbatim below).
//
//   CombineFloatLists.cs Update():
//     Selected.Value ??= [];
//     var list = Selected.Value;
//     list.Clear();
//     var connections = InputLists.GetCollectedTypedInputs();
//     if (connections == null || connections.Count == 0) return;
//     foreach (var i in connections) {
//         var inputList = i.GetValue(context);
//         if (inputList is { Count: > 0 })          // skip null/empty inputs
//             list.AddRange(inputList);
//     }
//
//   Ports: InputLists = MultiInputSlot<List<float>> (the ONE variable-length list of FloatList sources).
//   Output: Selected  = Slot<List<float>> (the concatenation of every wired list, in wire order).
//
// EVAL-SIDE LAYOUT: this is a FloatList→FloatList COMBINER — its one input port is a "FloatList"
// MultiInput, so the cook driver (point_graph_hostvalue_cook.cpp cookFlatFloatList) gathers EACH wired
// FloatList source as a SEPARATE entry of inputLists, in WIRE-DECLARATION order (the load-bearing spot
// — it mirrors the resident extraConns gather). So the leaf body is the .cs: clear the output, then
// concatenate each gathered list in order, skipping empties.
//
// FORK (named):
//   - fork-combinefloatlists-empty-when-unwired: TiXL's GetCollectedTypedInputs() yields the connected
//     inputs only — an unwired MultiInput yields nothing → Selected is empty. The driver's gather
//     mirrors this: 0 wired sources → inputLists empty → output stays empty (the .cs early-return path).
//     An individual EMPTY wired list contributes nothing (the `Count > 0` guard) — concatenation skips it.
#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// CombineFloatLists: list.Clear(); foreach gathered FloatList -> list.AddRange(it) (skip empties).
// The driver hands each wired FloatList source as a separate inputLists entry (wire-declaration order).
void cookCombineFloatLists(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // CombineFloatLists.cs — list.Clear()
  if (c.inputLists) {
    for (const std::vector<float>& src : *c.inputLists) {  // foreach connection, wire-declaration order
      if (!src.empty())                                    // CombineFloatLists.cs — `Count > 0` guard
        c.output->insert(c.output->end(), src.begin(), src.end());  // list.AddRange(inputList)
    }
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "InputLists" = FloatList MultiInput (the driver expands all wires into separate lists);
//          "out"        = the concatenated FloatList output.
static const FloatListOp _reg_combinefloatlists{
    {"CombineFloatLists", "CombineFloatLists",
     {{"InputLists", "InputLists", "FloatList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookCombineFloatLists};

}  // namespace sw
