// PickFloatFromList host-scalar op (host-scalar self-registration seam leaf — FloatList + Index →
// host Float output, the indexed-read consumer). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/logic/PickFloatFromList.cs:16-28 (verbatim):
//
//   PickFloatFromList.cs Update():
//     var list = Input.GetValue(context);
//     if (list == null || list.Count == 0) { Selected.Value = default; return; }   // default(float)=0
//     var index = Index.GetValue(context).Mod(list.Count);                          // T3 floor-Mod
//     Selected.Value = list[index];
//
//   Ports: Input  = InputSlot<List<float>> (default new List<float>(20) → non-null EMPTY → Count 0);
//          Index  = InputSlot<int>          (default 0).
//   Output: Selected = Slot<float>          (the picked element, 0 when the list is empty).
//
//   T3 .Mod (Core/Utils/MathUtils.cs:273-284): floor-mod, NOT C remainder —
//     if (repeat == 0) return 0;  x = val % repeat;  if (x < 0) x = repeat + x;  return x;
//   So Index 4 on a 3-list → 4 % 3 = 1 → list[1]; Index -1 → (-1)%3=-1 → +3 → 2 → list[2].
//
// EVAL-SIDE LAYOUT: same host-scalar contract as FloatListLength — cooked by the driver's host-scalar
// branch (gathers the FloatList input → inputLists[0], resolves the Index Float param), result stored
// in floatListBuf (transport) AND Node::outCache[0] (the BRIDGE evalFloat reads).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's Index is int; sw has no Int port → Index dissolves to
//     Float and is recovered with std::lround (Cut32 convention). list[index] is exact (host float).
//   - fork-pickfloat-empty-is-zero: list == null OR Count == 0 → 0 (default(float)). sw's gather: an
//     UNWIRED FloatList input → no entry → empty → 0; a wired empty list → Count 0 → 0. Both match.
#include <cmath>  // std::lround

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug / hostScalarParam

namespace sw {

namespace {

// T3 floor-Mod (MathUtils.cs:273-284): repeat==0 → 0; x = val % repeat; if (x<0) x += repeat.
int t3Mod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// PickFloatFromList: empty/absent list → 0; else list[Index.Mod(Count)].
void cookPickFloatFromList(HostScalarCookCtx& c) {
  if (!c.output) return;
  *c.output = 0.0f;  // PickFloatFromList.cs:21 — Selected.Value = default (float 0) on empty/null
  if (c.inputLists && !c.inputLists->empty()) {
    const std::vector<float>& list = (*c.inputLists)[0];
    if (!list.empty()) {  // PickFloatFromList.cs:19 — guard list.Count == 0
      const int idxRaw = (int)std::lround(hostScalarParam(c.params, "Index", 0.0f));  // int dissolve
      const int index = t3Mod(idxRaw, (int)list.size());  // PickFloatFromList.cs:25 — Index.Mod(Count)
      *c.output = list[(size_t)index];                    // PickFloatFromList.cs:27 — list[index]
    }
  }
  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites via downstream
  // evalFloat (NOT by flipping the expected value). Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
//   Ports: "Selected" = the Float output (the picked element; host scalar via outCache + floatListBuf);
//          "Input"    = the FloatList input (the list to pick from);
//          "Index"    = the Float index param (int dissolved to Float; resolved via the value spine).
// Output port FIRST (index 0) so outIdx 0 = Selected. Index default 0 (TiXL InputSlot<int>(0)); its
// range is wide+symmetric (negative indices are valid — T3 floor-Mod wraps them).
static const HostScalarOp _reg_pickfloatfromlist{
    {"PickFloatFromList", "PickFloatFromList",
     {{"Selected", "Selected", "Float", false},
      {"Input", "Input", "FloatList", true},
      {"Index", "Index", "Float", true, 0.0f, -100000.0f, 100000.0f}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookPickFloatFromList};

}  // namespace sw
