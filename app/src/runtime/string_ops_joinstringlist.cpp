// JoinStringList string op (string self-registration seam leaf — List<string> Input + Separator(String)
// → String). Sub-seam A: the StringList-currency proving op (consumes the NEW StringList channel via the
// StringCookCtx::inputStringLists field; emits a String). TiXL authority:
// Operators/Lib/string/list/JoinStringList.cs (verbatim below):
//
//   JoinStringList.cs Update():
//     var separatorValue = Separator.GetValue(context);
//     if (separatorValue == null) { Result.Value = string.Empty; return; }   // null sep → "" (warning)
//     var separator = separatorValue.Replace("\\n", "\n");
//     var input = Input.GetValue(context);
//     if (input == null || input.Count == 0) { Result.Value = string.Empty; return; }  // empty list → ""
//     Result.Value = string.Join(separator, input);                          // TRUE join (sep BETWEEN)
//
//   Ports: Input = InputSlot<List<string>>;  Separator = InputSlot<string>.
//   Output: Result = Slot<string>.  (Also IStatusProvider — UI-only warning text, not a data output.)
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode / cookResidentString). Input is a StringList
// input → gathered into StringCookCtx::inputStringLists[0] (the NEW StringList currency). Separator is the
// ONE String input → inputStrings[0] (wire-OR-const). No Float params.
//
// SEMANTICS (ported 1:1 from the .cs):
//   • Separator "\\n" (literal backslash-n) → real newline (same escape fork as CombineStrings).
//   • empty / unwired list → "" (the .cs Count==0 early-return; IStatusProvider warning is UI-only, not
//     ported — fork-joinstringlist-no-status).
//   • string.Join(separator, input): a TRUE join — the separator goes BETWEEN elements, NO trailing
//     separator (contrast FloatListToString's trailing-separator builder). N elements → N-1 separators.
//
// FORKS (named):
//   - fork-joinstringlist-empty-list: an empty (or unwired) StringList → "" (the .cs Count==0 branch).
//     sw has no null vs empty distinction for a host list, so unwired/empty collapse to the same "" —
//     faithful to BOTH .cs early-returns (null and Count==0 both yield "").
//   - fork-joinstringlist-no-status: TiXL's IStatusProvider warning ("Can't join empty string list.")
//     is UI-only; sw carries no per-op status channel, so it is not ported (the DATA output "" matches).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
//   - fork-join-is-between-not-trailing: separator placed BETWEEN elements (string.Join), the load-bearing
//     contrast vs FloatListToString — and the wire-ORDER proof (join honours list/wire order, not sorted).
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

void cookJoinStringList(StringCookCtx& c) {
  if (!c.output) return;

  // Input: the ONE StringList input → inputStringLists[0] (the NEW StringList currency). Unwired/empty
  // → empty list → "" (JoinStringList.cs:24-29).
  const std::vector<std::string> input =
      (c.inputStringLists && !c.inputStringLists->empty()) ? (*c.inputStringLists)[0]
                                                           : std::vector<std::string>{};

  // Separator = inputStrings[0] (wire-OR-const). "\\n" → newline (JoinStringList.cs:22).
  std::string sep = (c.inputStrings && c.inputStrings->size() > 0) ? (*c.inputStrings)[0] : std::string{};
  {
    std::string::size_type p = 0;
    while ((p = sep.find("\\n", p)) != std::string::npos) { sep.replace(p, 2, "\n"); p += 1; }
  }

  // string.Join(separator, input): separator BETWEEN elements, NO trailing (JoinStringList.cs:32).
  std::string result;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (i) result += sep;
    result += input[i];
  }
  *c.output = result;  // empty list → "" (loop skipped)

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the actual
  // cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order; StringList → inputStringLists, String → inputStrings):
//     [0] "Result"    = String output    (the host string currency — String PRODUCER)
//     [1] "Input"     = StringList input  (the NEW StringList currency → inputStringLists[0]; multiInput
//                                          like the .cs List input — a future fan of StringList wires)
//     [2] "Separator" = String input      (wire-OR-const; strDef "" → join with "")
//   The driver gathers StringList input ports into inputStringLists (port [1] → [0]) and String input
//   ports into inputStrings (port [2] → [0] == Separator).
static const StringOp _reg_joinstringlist{
    {"JoinStringList", "JoinStringList",
     {{"Result", "Result", "String", false},
      {"Input", "Input", "StringList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true, ""},
      {"Separator", "Separator", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookJoinStringList};

}  // namespace sw
