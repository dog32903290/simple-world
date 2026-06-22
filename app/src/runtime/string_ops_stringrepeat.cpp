// StringRepeat string op (string self-registration seam leaf — Fragment(String) + Count(Int) →
// String). TiXL authority: Operators/Lib/string/combine/StringRepeat.cs (verbatim below):
//
//   StringRepeat.cs Update():
//     var content = Fragment.GetValue(context);
//     var count =  Count.GetValue(context).Clamp(0,1000);
//     if (count == 0 || string.IsNullOrEmpty(content))
//     {
//         Result.Value = string.Empty;
//     }
//     else
//     {
//         Result.Value =  new StringBuilder().Insert(0, content, count).ToString();
//     }
//
//   Ports: Fragment = InputSlot<string>; Count = InputSlot<int>. Output: Result = Slot<string>.
//   StringBuilder().Insert(0, content, count) inserts `content` `count` times at position 0 — i.e.
//   `content` repeated `count` times concatenated. So StringRepeat("ab", 3) = "ababab".
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). Fragment is the ONE String input port →
// inputStrings[0] (wired upstream string, or strDef const "" when unwired). Count is an Int param
// dissolved to Float (the value spine, resolved via stringFloatParam / params) — same fork as every
// Int-input op already ported (fork-int-bool-dissolve-to-float, Cut32 convention).
//
// SEMANTICS (ported 1:1 from the .cs):
//   count = Clamp(rawCount, 0, 1000)          — the clamp IS assigned back in the .cs (var count = …)
//   if count == 0 OR content empty → ""        (StringRepeat.cs:20-23)
//   else → content repeated `count` times      (StringRepeat.cs:26)
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL Count is InputSlot<int>; sw has no Int port type, so it
//     dissolves int→Float (resolved via the value spine, read back via (int)(float)). Cut32 convention.
//   - fork-stringrepeat-count-clamp: Count clamped to [0,1000] (faithful to the .cs .Clamp(0,1000) —
//     and the clamp value IS used here, unlike StringInsert's discarded-clamp bug). Negative count → 0
//     → empty string; count > 1000 → 1000 repeats. The 1000 ceiling caps host memory (a genuine guard).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <algorithm>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// StringRepeat: Fragment (String input) + Count (Int param) → host string. Implements
// StringRepeat.cs Update() 1:1, Count dissolved to Float (fork named above).
void cookStringRepeat(StringCookCtx& c) {
  if (!c.output) return;

  // Fragment: first (and only) String input port — wired upstream string, or strDef const "" unwired.
  const std::string content =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};

  // Count: resolved Float param truncated to int, clamped to [0,1000] (fork-stringrepeat-count-clamp).
  // The .cs `var count = Count.GetValue(context).Clamp(0,1000)` USES the clamped value (assigned back).
  int count = (int)stringFloatParam(c.params, "Count", 0.0f);
  count = std::max(0, std::min(count, 1000));

  // StringRepeat.cs:20-27.
  if (count == 0 || content.empty()) {
    *c.output = std::string{};
  } else {
    // StringBuilder().Insert(0, content, count) = content concatenated `count` times.
    std::string out;
    out.reserve(content.size() * static_cast<std::string::size_type>(count));
    for (int i = 0; i < count; ++i) out += content;
    *c.output = out;
  }

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputStrings):
//     [0] "Result"   = String output (the host string currency — String PRODUCER)
//     [1] "Fragment" = String input  (wire-OR-const; strDef "" = empty, no text by default)
//     [2] "Count"    = Float/Int input (value spine; Int dissolved to Float; default 0)
//   The driver gathers String input ports into inputStrings in spec order: only port [1] is a String
//   input, so inputStrings[0] == Fragment (wired or strDef ""). Count rides params.
static const StringOp _reg_stringrepeat{
    {"StringRepeat", "StringRepeat",
     {{"Result",   "Result",   "String", false},
      {"Fragment", "Fragment", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"Count",    "Count",    "Float",  true, 0.0f, 0.0f, 1000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookStringRepeat};

}  // namespace sw
