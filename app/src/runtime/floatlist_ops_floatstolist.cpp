// FloatsToList floatlist op (floatlist self-registration seam leaf — MultiInput<float> -> List<float>).
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/basic/FloatsToList.cs (verbatim below).
//
//   FloatsToList.cs Update():
//     Result.Value.Clear();
//     foreach (var input in Input.GetCollectedTypedInputs())
//         Result.Value.Add(input.GetValue(context));
//
//   Ports: Input = MultiInputSlot<float> (the ONE variable-length list of scalar floats).
//   Output: Result = Slot<List<float>> (the host list — the new FloatList channel's first producer).
//
// EVAL-SIDE LAYOUT: this is the FIRST FloatList producer and the cleanest one — it has NO FloatList
// INPUT to gather, only a scalar Float MultiInput. The cook driver (point_graph.cpp cookFloatListNode)
// gathers that ONE scalar Float MultiInput port's wired sources into a single host list (in WIRE-
// DECLARATION order — the load-bearing spot, mirroring the resident extraConns gather) and hands it as
// inputLists[0]. So this leaf's body is literally the .cs: clear the output list, then append each
// gathered scalar in order. The gather order discipline lives in the driver; here we copy verbatim.
//
// The output rides the new "FloatList" dataType port back to a downstream FloatList consumer
// (ValuesToTexture, Slice B). Pure HOST data — never touches the GPU 16B ctx.
//
// FORK (named):
//   - fork-floatstolist-empty-when-unwired: TiXL's GetCollectedTypedInputs() yields the connected
//     inputs only — an unwired MultiInput yields nothing, so Result is an empty list. The driver's
//     gather mirrors this: 0 wired sources → inputLists[0] is empty (or absent) → output stays empty.
//     (No "primary default" fallback like SumInts: FloatsToList does NOT read InputValues.GetValue on
//     empty — it just produces an empty list, faithful to the .cs which never touches an empty path.)
#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

int runFloatListSelfTest(bool injectBug);

namespace {

// FloatsToList: Result.Value.Clear(); foreach gathered scalar -> Result.Value.Add(scalar).
// The driver hands the gathered scalar Float MultiInput sources as inputLists[0] (one list, in
// wire-declaration order). Copy it verbatim into *output.
void cookFloatsToList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // FloatsToList.cs:16 — Result.Value.Clear()
  if (c.inputLists && !c.inputLists->empty()) {
    const std::vector<float>& gathered = (*c.inputLists)[0];
    for (float v : gathered)        // FloatsToList.cs:17-19 — Add each collected input, in order
      c.output->push_back(v);
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
// Feeds floatListSpecSink() + floatListCookFns() during pre-main dynamic init.
//   Ports: "Input" = scalar Float MultiInput (the driver expands all wires into one gathered list);
//          "out"   = the FloatList output (the new host-list channel's currency).
static const FloatListOp _reg_floatstolist{
    {"FloatsToList", "FloatsToList",
     {{"Input", "Input", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookFloatsToList};

}  // namespace sw
