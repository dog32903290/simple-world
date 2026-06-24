// SetIntListValue floatlist op (floatlist self-registration seam leaf — List<int> + index/value/mode ->
// List<int>, dissolved onto the FloatList host-currency since sw has no Int rail). TiXL authority:
// external/tixl/Operators/Lib/numbers/ints/SetIntListValue.cs (verbatim below).
//
//   SetIntListValue.cs Update():  (structurally IDENTICAL to SetFloatListValue, with int element/value)
//     if (!TriggerSet) return;
//     var intList = IntList.GetValue(context);
//     if (intList == null || intList.Count == 0) return;
//     var value = Value (int); var index = Index;
//     if (index >= 0)      { index = index.Mod(Count); switch(Mode){Set/Add/Multiply on intList[index]} }
//     else if (index==-2)  { for i in 0..Count: switch(Mode){...on intList[i]} }
//     Result.Value = intList;
//
//   Ports: Mode = int enum {Set,Add,Multiply}; TriggerSet = bool; IntList = List<int>;
//          Index = int; Value = INT.
//   Output: Result = Slot<List<int>>.
//
// EVAL-SIDE LAYOUT: the IntList input dissolves onto the FloatList currency (gathered as inputLists[0]);
// Mode/TriggerSet/Index/Value are resolved Float params. Elements are kept integer-valued — the input is
// already integer-valued (dissolved ints), and after Add/Multiply by an integer Value the result is an
// integer; std::lround on Value + final element guards the int semantics. The Mod/-2/passthrough logic is
// byte-identical to SetFloatListValue (same .cs shape).
//
// FORKS (named):
//   - fork-int-dissolve: List<int> + int Value dissolve to the FloatList currency (Cut32 — no Int rail).
//     Value is rounded (std::lround) so Add/Multiply stay integer; elements are integer-valued throughout.
//     FREE fold (backward-trace: only Set/Add/Multiply by an int Value — NO bitwise/overflow concern at the
//     small list-index magnitudes these ops carry; LIST_SEAM_BLUEPRINT §1 IntList).
//   - fork-trigger-passthrough: same as SetFloatListValue — not-triggered / empty / out-of-range-negative
//     index → output = the input list UNCHANGED (the faithful stateless reading of TiXL's `return` paths).
#include <cmath>  // std::lround

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// T3 floor-Mod (MathUtils.cs:273-284): repeat==0 → 0; x = val % repeat; if (x<0) x += repeat.
int t3ModI(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// Apply Mode {0=Set,1=Add,2=Multiply} of int `value` onto e (in place; e stays integer-valued).
void applyModeInt(float& e, int mode, int value) {
  switch (mode) {
    case 0: e = (float)value; break;                          // Set
    case 1: e = (float)((long)std::lround(e) + value); break; // Add (integer arithmetic)
    case 2: e = (float)((long)std::lround(e) * value); break; // Multiply (integer arithmetic)
    default: break;                                           // unknown enum → no-op
  }
}

void cookSetIntListValue(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  if (c.inputLists && !c.inputLists->empty())
    *c.output = (*c.inputLists)[0];  // copy the dissolved List<int> (integer-valued floats)

  const bool trigger = floatListParam(c.params, "TriggerSet", 0.0f) != 0.0f;
  if (trigger && !c.output->empty()) {
    const int idx = (int)std::lround(floatListParam(c.params, "Index", 0.0f));
    const int mode = (int)std::lround(floatListParam(c.params, "Mode", 0.0f));
    const int value = (int)std::lround(floatListParam(c.params, "Value", 0.0f));  // int Value
    const int count = (int)c.output->size();
    if (idx >= 0) {
      const int wrapped = t3ModI(idx, count);                  // index.Mod(Count)
      applyModeInt((*c.output)[(size_t)wrapped], mode, value); // single element
    } else if (idx == -2) {
      for (float& e : *c.output) applyModeInt(e, mode, value); // ALL elements
    }
  }

  // Test-only: corrupt the REAL output (drop last) so the golden's RED bites on the actual cook path.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first; "IntList" input (dissolved onto FloatList currency); Mode (enum)/TriggerSet
//          (bool)/Index/Value (int) as pinless Float params (all dissolve to Float).
static const FloatListOp _reg_setintlistvalue{
    {"SetIntListValue", "SetIntListValue",
     {{"out", "out", "FloatList", false},
      {"IntList", "IntList", "FloatList", true},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Set", "Add", "Multiply"}, true},
      {"TriggerSet", "TriggerSet", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"Index", "Index", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"Value", "Value", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookSetIntListValue};

}  // namespace sw
