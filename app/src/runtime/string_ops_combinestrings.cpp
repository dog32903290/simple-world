// CombineStrings string op (string self-registration seam leaf — MultiInput<string> + Separator →
// string). TiXL authority: Operators/Lib/string/combine/CombineStrings.cs:20-31 (verbatim below):
//
//   CombineStrings.cs Update():
//     _stringBuilder.Clear();
//     var separator = Separator.GetValue(context).Replace("\\n", "\n");
//     var isFirst = true;
//     foreach (var input in Input.GetCollectedTypedInputs()) {
//       if (!isFirst && !string.IsNullOrEmpty(separator))
//         _stringBuilder.Append(separator);
//       var t = input.GetValue(context);
//       if (!string.IsNullOrEmpty(t))
//         _stringBuilder.Append(t);
//       isFirst = false;
//     }
//     Result.Value = _stringBuilder.ToString();
//
//   Ports: Input = MultiInputSlot<string> (the variable-length list of strings to join);
//          Separator = InputSlot<string> (placed between consecutive inputs). Output: Result (string).
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). The driver's gather hands inputStrings
// in spec port order with the MultiInput EXPANDED into wire-declaration order. We declare Input
// (MultiInput) BEFORE Separator (single), so the gathered layout is:
//     inputStrings = [ inputWire0, inputWire1, ..., inputWireN-1, Separator ]
// i.e. the LAST entry is always the single Separator (wired value or strDef const), and everything
// before it is the Input MultiInput wires (possibly zero). This is the load-bearing gather contract:
// gather-order = wire-declaration order (CombineStrings([a,b,c]) wired [a,b,c] → "a-b-c", NOT sorted,
// NOT first-wire-only). The driver guarantees Separator is last because it is the last String port.
//
// FORKS (named):
//   - fork-combinestrings-separator-last-in-gather: the Separator is recovered as inputStrings.back()
//     and the Input wires are inputStrings[0 .. size-2]. Relies on Separator being the LAST String
//     port + single-cardinality (always exactly 1 gathered entry). Declared so here.
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
//   - fork-no-dirtyflag-trigger: TiXL ends with Input.DirtyFlag.Clear() (Layer-A dirty bookkeeping);
//     the flat cook is stateless + cooks every frame, so there is no DirtyFlag to clear (no-op fork).
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// C# string.Replace("\\n", "\n"): every literal two-char backslash-n in the separator becomes a real
// newline. (The .cs source literal "\\n" is the two characters backslash + 'n'.)
std::string replaceLiteralNewline(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'n') {
      out.push_back('\n');
      ++i;  // consume the 'n'
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

// CombineStrings: join the Input MultiInput wires with Separator between consecutive entries.
void cookCombineStrings(StringCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // _stringBuilder.Clear()
  if (!c.inputStrings || c.inputStrings->empty()) return;  // no Separator gathered → nothing to join

  const std::vector<std::string>& gathered = *c.inputStrings;
  // The single Separator is the LAST gathered entry (see fork-combinestrings-separator-last-in-gather).
  std::string separator = replaceLiteralNewline(gathered.back());
  const size_t inputCount = gathered.size() - 1;  // everything before the Separator = Input wires

  bool isFirst = true;
  for (size_t i = 0; i < inputCount; ++i) {
    if (!isFirst && !separator.empty()) c.output->append(separator);  // CombineStrings.cs:25-26
    const std::string& t = gathered[i];
    if (!t.empty()) c.output->append(t);  // CombineStrings.cs:28-29 (empty inputs append nothing)
    isFirst = false;                      // flips regardless of empty t (faithful to the .cs)
  }

  // Test-only: corrupt the REAL output (drop the last char) so a golden's transport/gather-order RED
  // bites on the actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports (ORDER MATTERS for the gather): "Result" output, "Input" MultiInput String, then
//   "Separator" single String LAST (so the driver gathers it as inputStrings.back()).
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//   vecArity, multiInput, strDef}. Separator strDef "" = no separator by default (TiXL InputSlot<string>).
static const StringOp _reg_combinestrings{
    {"CombineStrings", "CombineStrings",
     {{"Result", "Result", "String", false},
      {"Input", "Input", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true, ""},
      {"Separator", "Separator", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookCombineStrings};

}  // namespace sw
