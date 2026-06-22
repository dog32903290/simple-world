// PickStringFromList string op (string self-registration seam leaf — List<string> Input + Index(Int) →
// String). Sub-seam A: the StringList-currency CONSUMER twin of JoinStringList — it gathers the SAME
// StringList channel (StringCookCtx::inputStringLists), but instead of JOINING the list it SELECTS exactly
// one element by Index modulo count. TiXL authority: Operators/Lib/string/list/PickStringFromList.cs
// (verbatim below):
//
//   PickStringFromList.cs Update():
//     var list = Input.GetValue(context);
//     if (list == null || list.Count == 0) { Selected.Value = string.Empty; Count.Value = 0; return; }
//     Count.Value = list.Count;
//     var index = Index.GetValue(context).Mod(list.Count);
//     Selected.Value = list[index];
//
//   Ports: Input = InputSlot<List<string>>;  Index = InputSlot<int> (default 0).
//   Outputs: Selected = Slot<string>;  Count = Slot<int> (the list count).
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode / cookResidentString). Input is a StringList
// input → gathered into StringCookCtx::inputStringLists[0] (the StringList currency — EXACTLY the
// JoinStringList gather). Index is a Float param (Int dissolved → the value spine), read via
// stringFloatParam — it does NOT land in inputStrings (mirror of PickString.Index). No String inputs.
//
// SEMANTICS (ported 1:1 from the .cs):
//   • empty / unwired list → "" (the .cs list==null || Count==0 early-return).
//   • index = Index.Mod(list.Count), where Int.Mod is T3.Core.Utils.MathUtils.Mod — EUCLIDEAN wrap:
//       Mod(_, 0) → 0 (the "Prevent exception" branch; here unreachable because count==0 already
//       early-returned), else ((v % n) + n) % n (a NEGATIVE Index wraps to a non-negative slot).
//   • Selected = list[index], index ∈ [0, count) by Mod — list/wire ORDER honoured (not sorted).
//
// FORKS (named):
//   - fork-pickstringfromlist-empty-list: an empty (or unwired) StringList → "" (the .cs Count==0 branch).
//     sw has no null vs empty distinction for a host list, so unwired/empty collapse to the same "" —
//     faithful to BOTH .cs guards (null and Count==0 both yield "").
//   - fork-pickstringfromlist-count-deferred: TiXL's Count = list.Count (a Slot<int>). The String cook
//     flow's main output is the single Selected String; Count is a derived scalar (= list.size()) a
//     consumer can recompute. Transporting it would need a scalarOutputs entry (Sub-seam B multi-output);
//     not wired (mirror of fork-splitstring-count-deferred). Selected is the load-bearing output.
//   - fork-int-dissolve-to-float: TiXL Index is InputSlot<int>; sw has no Int port type, so it dissolves
//     int→Float (rides params, resolved via the value spine), read back via (int)(float). Same Cut32
//     convention as PickString.Index / SubString.Start.
//   - fork-pickstringfromlist-euclidean-mod: Index.Mod uses euclidean wrap (negative → non-negative);
//     ported verbatim from MathUtils.Mod (NOT C++ %, which is truncating for negatives).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Euclidean modulo (T3.Core.Utils.MathUtils.Mod): Mod(_,0)=0; else ((v%n)+n)%n (negative→non-negative).
int euclideanMod(int val, int repeat) {
  if (repeat == 0) return 0;          // MathUtils.Mod "Prevent exception"
  int x = val % repeat;
  if (x < 0) x = repeat + x;          // wrap negative into [0,repeat)
  return x;
}

// PickStringFromList: select inputStringLists[0][Index.Mod(count)]. Implements PickStringFromList.cs 1:1.
void cookPickStringFromList(StringCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // default (empty) — the faithful "Selected unset" state for the empty-list branch

  // Input: the ONE StringList input → inputStringLists[0] (the StringList currency). Unwired/empty
  // → empty list → "" (PickStringFromList.cs list==null || Count==0 early-return).
  const std::vector<std::string> list =
      (c.inputStringLists && !c.inputStringLists->empty()) ? (*c.inputStringLists)[0]
                                                           : std::vector<std::string>{};
  const int count = (int)list.size();

  // PickStringFromList.cs: empty list → Selected = "" (leave *output cleared). Inject bug even on the
  // empty path so the tooth bites the output write (mirror of PickString's empty-path inject).
  if (count == 0) {
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  const int rawIndex = (int)stringFloatParam(c.params, "Index", 0.0f);
  const int index = euclideanMod(rawIndex, count);  // Index.Mod(list.Count) — euclidean

  *c.output = list[static_cast<std::size_t>(index)];  // Selected = list[index], list/wire order

  // Test-only: corrupt the REAL output (drop the last char) so a golden's selection/wire-order RED bites
  // on the actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order; StringList → inputStringLists, Float → params):
//     [0] "Selected" = String output     (the host string currency — String PRODUCER)
//     [1] "Input"    = StringList input   (the StringList currency → inputStringLists[0]; multiInput like
//                                          JoinStringList.Input / the .cs List input — a fan of wires)
//     [2] "Index"    = Float input        (Int dissolved — rides params, NOT inputStrings)
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//   vecArity, multiInput, strDef}.
static const StringOp _reg_pickstringfromlist{
    {"PickStringFromList", "PickStringFromList",
     {{"Selected", "Selected", "String", false},
      {"Input", "Input", "StringList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true, ""},
      {"Index", "Index", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPickStringFromList};

}  // namespace sw
