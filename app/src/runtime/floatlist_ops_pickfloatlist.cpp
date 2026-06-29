// PickFloatList floatlist op (floatlist self-registration seam leaf — MultiInput<List<float>> ->
// List<float>, the index-selecting picker; the LIST twin of PickFloatFromList which picks one ELEMENT).
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/logic/PickFloatList.cs (verbatim below):
//
//   PickFloatList.cs Update():
//     var connections = Input.GetCollectedTypedInputs();
//     if (connections == null || connections.Count == 0) return;          // unwired → keep last (no clear)
//     var index = Index.GetValue(context).Mod(connections.Count);         // T3 floor-Mod
//     Selected.Value = connections[index].GetValue(context);
//
//   Ports: Input = MultiInputSlot<List<float>> (the variable-length set of FloatList sources).
//          Index = InputSlot<int>(0).
//   Output: Selected = Slot<List<float>> (the ONE picked source list).
//
// EVAL-SIDE LAYOUT: each wired FloatList source is gathered as a separate inputLists entry (wire-
// declaration order), exactly like CombineFloatLists. PickFloatList picks ONE of them by Index.Mod(count)
// and copies it to the output (the picker SELECTS, it does not concatenate). STATELESS — closed-form.
//
// FORKS (named):
//   - fork-pickfloatlist-t3-floor-mod: T3 .Mod (Core/Utils/MathUtils.cs) is a FLOOR-mod, NOT C remainder.
//     Index 4 on 3 sources → 4.Mod(3)=1; Index -1 → (-1).Mod(3)=2. SAME semantics as PickFloatFromList /
//     PickIntFromList (Index dissolves int→Float, lround back to int before the mod).
//   - fork-pickfloatlist-unwired-keeps-output: TiXL early-returns on 0 connections WITHOUT clearing
//     Selected (the previously-published list persists). sw's driver re-cooks the output fresh each frame
//     (output->clear() then nothing written → empty). On the FIRST cook both are empty, so this only
//     diverges if an upstream is later disconnected mid-run — a transient-disconnect edge case, named.
//   - fork-intlist-dissolves-to-floatlist: a MergeIntLists-style List<int> source rides this same
//     FloatList currency (sw has no Int rail) — PickFloatList picks it identically (it never reads element
//     VALUES, only selects a whole list).
#include <algorithm>  // std::lround (via cmath)
#include <cmath>      // std::lround, std::floor

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// T3 floor-Mod for the index (MathUtils.cs .Mod): result in [0, repeat) for repeat > 0.
int t3FloorMod(int val, int repeat) {
  if (repeat <= 0) return 0;
  int m = val % repeat;
  if (m < 0) m += repeat;
  return m;
}

// PickFloatList: pick inputLists[Index.Mod(count)] and copy it to *output. count==0 → empty output
// (the cook driver clears the output each frame; the .cs early-return-without-clear divergence is named).
void cookPickFloatList(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  if (c.inputLists && !c.inputLists->empty()) {
    const int count = (int)c.inputLists->size();
    const int idxRaw = (int)std::lround(floatListParam(c.params, "Index", 0.0f));  // int dissolve
    const int index = t3FloorMod(idxRaw, count);  // PickFloatList.cs — Index.Mod(connections.Count)
    const std::vector<float>& picked = (*c.inputLists)[(size_t)index];
    c.output->insert(c.output->end(), picked.begin(), picked.end());  // Selected = connections[index]
  }
  // Test-only: corrupt the REAL output on the actual cook path (drop last) so the golden's RED bites here,
  // NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "Input" = FloatList MultiInput (the candidate sources); "Index" = Float (int-dissolved
//          selector); "out" = the picked FloatList.
static const FloatListOp _reg_pickfloatlist{
    {"PickFloatList", "PickFloatList",
     {{"Input", "Input", "FloatList", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"Index", "Index", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPickFloatList};

}  // namespace sw
