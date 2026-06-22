// PickString string op (string self-registration seam leaf — MultiInput<string> + Index(Int) →
// string). TiXL authority: Operators/Lib/string/logic/PickString.cs:16-39 (verbatim below):
//
//   PickString.cs Update():
//     var connections = Input.GetCollectedTypedInputs();
//     var index = Index.GetValue(context).Mod(connections.Count);
//     Input.DirtyFlag.Clear();
//     if (connections.Count == 0)
//         return;
//     Selected.Value = connections[index].GetValue(context);
//     if (_isFirstUpdate) { foreach (c in connections) c.GetValue(context); _isFirstUpdate = false; }
//     Input.DirtyFlag.Clear();
//
//   Ports: Input = MultiInputSlot<string> (the variable-length list of candidate strings);
//          Index = InputSlot<int> (which one to pick, modulo count). Output: Selected (string).
//
// This is the SAME MultiInput gather as CombineStrings (see string_ops_combinestrings.cpp), but
// instead of JOINING the wires it SELECTS exactly one by Index modulo the wire count.
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). The driver's gather hands inputStrings
// in spec port order with the MultiInput EXPANDED into wire-declaration order. We declare Input
// (MultiInput) FIRST, then Index is a Float param (Int dissolved to Float — the value spine). Index
// is NOT a String input port, so it does NOT land in inputStrings; it rides params. Therefore:
//     inputStrings = [ inputWire0, inputWire1, ..., inputWireN-1 ]   (exactly the Input wires)
// i.e. EVERY entry in inputStrings is an Input MultiInput wire (no trailing const like CombineStrings'
// Separator, because Index is a Float param, not a String port). This is the load-bearing gather
// contract: gather-order = wire-declaration order (the same proven contract as CombineStrings LEG 20).
//
// MODULO (PickString.cs:19) — index = Index.Mod(count), where Int.Mod is T3.Core.Utils.MathUtils.Mod:
//   • Mod(_, 0) → 0 (the "Prevent exception" branch; PickString then early-returns on count==0)
//   • Mod(v, n) = ((v % n) + n) % n  (euclidean: a NEGATIVE Index wraps to a non-negative slot)
// Ported 1:1 below. Note .Mod is evaluated BEFORE the count==0 guard in the .cs, but with count==0 it
// returns 0 and the very next line returns anyway — so empty-input → output left empty (unchanged).
//
// EMPTY-INPUT (PickString.cs:22-23): connections.Count == 0 → return WITHOUT setting Selected.Value.
// In our cook this means: leave *c.output empty (a fresh driver-owned string is already empty). Faithful.
//
// FORKS (named):
//   - fork-pickstring-no-trailing-const-in-gather: unlike CombineStrings (whose Separator is a String
//     port gathered LAST), PickString's Index is a FLOAT param (Int dissolved), so it rides params, NOT
//     inputStrings. Every inputStrings entry is therefore an Input MultiInput wire — pick index `i`
//     directly with no back()-trimming.
//   - fork-int-dissolve-to-float: TiXL Index is InputSlot<int>; sw has no Int port type, so it dissolves
//     int→Float (stored in params / resolved via the value spine), read back via (int)(float). Same
//     Cut32 convention as every Int-input op ported so far (SubString.Start/Length, etc.).
//   - fork-pickstring-euclidean-mod: Index.Mod uses euclidean wrap (negative → non-negative); ported
//     verbatim from MathUtils.Mod (NOT C++ %, which is truncating for negatives).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
//   - fork-no-dirtyflag-trigger: TiXL ends with Input.DirtyFlag.Clear() and a first-update warm-pull
//     (Layer-A dirty bookkeeping / value-priming, an OPTIMIZATION not cross-frame STATE). The flat cook
//     is stateless + cooks every frame, so there is no DirtyFlag to clear and nothing to warm — no-op
//     fork. PickString is confirmed STATELESS (the _isFirstUpdate bit only forces one extra GetValue on
//     the unselected wires to clear THEIR dirty flags; it does not change the SELECTED output).
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Euclidean modulo (T3.Core.Utils.MathUtils.Mod): Mod(_,0)=0; else ((v%n)+n)%n (negative→non-negative).
int euclideanMod(int val, int repeat) {
  if (repeat == 0) return 0;          // MathUtils.Mod:276-277 "Prevent exception"
  int x = val % repeat;               // MathUtils.Mod:279
  if (x < 0) x = repeat + x;          // MathUtils.Mod:280-281 (wrap negative into [0,repeat))
  return x;
}

// PickString: select inputStrings[Index.Mod(count)] from the Input MultiInput wires.
void cookPickString(StringCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // default (empty) — the faithful "Selected unset" state for count==0

  // Every inputStrings entry is an Input MultiInput wire (Index rides params, not inputStrings).
  const int count = (c.inputStrings ? (int)c.inputStrings->size() : 0);

  const int rawIndex = (int)stringFloatParam(c.params, "Index", 0.0f);
  const int index = euclideanMod(rawIndex, count);  // PickString.cs:19 (Index.Mod(connections.Count))

  // PickString.cs:22-23 — count==0 → return without setting the output (leave it empty).
  if (count == 0) {
    // inject bug even on empty path so the bug check bites the output write (mirrors SubString).
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  // PickString.cs:25 — Selected.Value = connections[index].GetValue(context). index ∈ [0,count) by Mod.
  *c.output = (*c.inputStrings)[static_cast<std::size_t>(index)];

  // Test-only: corrupt the REAL output (drop the last char) so a golden's selection/gather-order RED
  // bites on the actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports (ORDER MATTERS for the gather): "Selected" output, "Input" MultiInput String FIRST (so all
//   its wires fill inputStrings in wire-declaration order), then "Index" Float (Int dissolved — rides
//   params, NOT inputStrings).
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//   vecArity, multiInput, strDef}.
static const StringOp _reg_pickstring{
    {"PickString", "PickString",
     {{"Selected", "Selected", "String", false},
      {"Input", "Input", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true, ""},
      {"Index", "Index", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPickString};

}  // namespace sw
