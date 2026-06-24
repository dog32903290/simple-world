// SetFloatListValue floatlist op (floatlist self-registration seam leaf — List<float> + index/value/mode
// -> List<float>, the indexed mutate). TiXL authority:
// external/tixl/Operators/Lib/numbers/floats/basic/SetFloatListValue.cs (verbatim below).
//
//   SetFloatListValue.cs Update():
//     if (!TriggerSet) return;                                  // gate
//     var floatList = FloatList.GetValue(context);
//     if (floatList == null || floatList.Count == 0) return;    // empty/null guard
//     var value = Value; var index = Index;
//     if (index >= 0) {
//         index = index.Mod(floatList.Count);                   // T3 floor-Mod
//         switch (Mode) { Set: list[index]=value; Add: list[index]+=value; Multiply: list[index]*=value; }
//     } else if (index == -2) {
//         for (i in 0..Count) { same switch on list[i] }        // index -2 = apply to ALL elements
//     }
//     Result.Value = floatList;                                 // the mutated list
//
//   Ports: Mode = int enum {Set,Add,Multiply}; TriggerSet = bool; FloatList = List<float>;
//          Index = int (>=0 → single element Mod(Count); -2 → ALL; other negatives → no-op pass-through);
//          Value = float.
//   Output: Result = Slot<List<float>>.
//
// EVAL-SIDE LAYOUT: the FloatList input is gathered by the driver as inputLists[0]; Mode/TriggerSet/Index/
// Value are RESOLVED Float params (enum + bool dissolve to Float — Cut32; int Index dissolves to Float and
// is recovered with std::lround). The leaf COPIES inputLists[0] into *output, then mutates in place.
//
// FORKS (named):
//   - fork-trigger-passthrough: TiXL's `if (!TriggerSet) return;` leaves Result.Value at its PRIOR value
//     (a cross-frame field) — but this op holds NO other cross-frame state, and a not-triggered frame is
//     semantically "don't change the list". The faithful STATELESS reading: not-triggered → output = the
//     input list UNCHANGED (pass-through). Same for the empty/null guard (empty in → empty out) and for an
//     out-of-range negative index that is neither >=0 nor -2 (TiXL skips the mutate, Result = the list as-is
//     → pass-through). Every guard path = "the list flows through untouched", which the copy already gives.
//   - fork-int-bool-enum-dissolve: Mode/TriggerSet/Index dissolve to Float params (no Int/Bool rail).
//     Index via std::lround; TriggerSet via != 0; Mode via lround → switch. list[index] / value exact (host float).
#include <cmath>  // std::lround

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug / floatListParam
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// T3 floor-Mod (MathUtils.cs:273-284): repeat==0 → 0; x = val % repeat; if (x<0) x += repeat.
int t3Mod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// Apply Mode {0=Set,1=Add,2=Multiply} of `value` onto e (in place).
void applyMode(float& e, int mode, float value) {
  switch (mode) {
    case 0: e = value; break;       // Set
    case 1: e += value; break;      // Add
    case 2: e *= value; break;      // Multiply
    default: break;                 // unknown enum → no-op (faithful: TiXL switch has no default)
  }
}

void cookSetFloatListValue(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  // Copy the input list (the list TiXL mutates in place + returns). Empty/absent input → empty output.
  if (c.inputLists && !c.inputLists->empty())
    *c.output = (*c.inputLists)[0];

  const bool trigger = floatListParam(c.params, "TriggerSet", 0.0f) != 0.0f;  // TriggerSet bool
  // Gate + empty guard: not triggered OR empty list → pass the (copied) list through unchanged.
  if (trigger && !c.output->empty()) {
    const int idx = (int)std::lround(floatListParam(c.params, "Index", 0.0f));    // int dissolve
    const int mode = (int)std::lround(floatListParam(c.params, "Mode", 0.0f));    // enum dissolve
    const float value = floatListParam(c.params, "Value", 0.0f);
    const int count = (int)c.output->size();
    if (idx >= 0) {
      const int wrapped = t3Mod(idx, count);                 // index.Mod(Count)
      applyMode((*c.output)[(size_t)wrapped], mode, value);  // single element
    } else if (idx == -2) {
      for (float& e : *c.output) applyMode(e, mode, value);  // index -2 → ALL elements
    }
    // (other negative indices: TiXL skips → output stays the unchanged copy = pass-through)
  }

  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "out" first; "FloatList" input (the list to mutate); Mode (enum)/TriggerSet (bool)/Index/Value
//          as pinless Float params (enum/bool/int dissolve to Float — resolved via the value spine).
// Index range is wide+symmetric (negative valid: -2 = all; T3 floor-Mod wraps non-negative).
static const FloatListOp _reg_setfloatlistvalue{
    {"SetFloatListValue", "SetFloatListValue",
     {{"out", "out", "FloatList", false},
      {"FloatList", "FloatList", "FloatList", true},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Set", "Add", "Multiply"}, true},
      {"TriggerSet", "TriggerSet", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"Index", "Index", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true},
      {"Value", "Value", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookSetFloatListValue};

}  // namespace sw
