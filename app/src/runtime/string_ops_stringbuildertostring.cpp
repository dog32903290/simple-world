// StringBuilderToString string op (string self-registration seam leaf — InputBuffer(String) → String).
// TiXL authority: Operators/Lib/string/buffers/convert/StringBuilderToString.cs (verbatim below):
//
//   StringBuilderToString.cs Update():
//     var stringBuilder = InputBuffer.GetValue(context);
//     if (stringBuilder == null)
//     {
//         String.Value = System.String.Empty;
//         return;
//     }
//     String.Value = stringBuilder.ToString();
//
//   Ports: InputBuffer = InputSlot<StringBuilder> (the mutable accumulator host type).
//          Output: String = Slot<string>.
//
// FORK: fork-stringbuilder-as-string (CORE DESIGN DECISION — documented here and in golden):
//   TiXL's StringBuilder is a .NET System.Text.StringBuilder, a mutable accumulator. sw has no
//   cross-frame instance state (no StringBuilder currency — stateful seam would require a whole new
//   resident lifetime rail). The ONLY observable product of a StringBuilder in TiXL's typical chain is
//   its final string — exactly the sw String currency. Therefore:
//     - StringBuilderToString is implemented as a String→String passthrough leaf.
//     - InputBuffer port is declared as dataType "String" (the sw String currency covers it).
//     - cook = unwired/empty input → "" (the .cs null guard); else output = inputStrings[0] (passthrough).
//   The canonical TiXL chain [StringBuilder] → [StringBuilderToString] → downstream is reproduced by
//   [sw_StringBuilder (InitialString passthrough)] → [StringBuilderToString (passthrough)] → downstream.
//   Zero ctx changes, zero driver changes, zero seam: rides the resident string-wire seam (commit
//   0bb25e2) automatically — both flat cook and resident extStrOut work.
//
// FORK: fork-stringbuilder-null-is-empty:
//   TiXL's null-StringBuilder → String.Empty (""). sw: unwired / empty inputStrings[0] → "".
//   Positive assertion in golden LEG A (fork-null-is-empty).
//
// FORK: fork-string-host-not-gpu:
//   String is host currency; no GPU EvaluationContext touched.
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// StringBuilderToString: InputBuffer(String) → String.
// fork-stringbuilder-as-string: String passthrough; unwired/empty → "" (the .cs null → String.Empty guard).
void cookStringBuilderToString(StringCookCtx& c) {
  if (!c.output) return;

  // InputBuffer = inputStrings[0] (the one String input port).
  // Unwired / empty → "" (StringBuilderToString.cs null guard; fork-stringbuilder-null-is-empty).
  const bool hasInput = (c.inputStrings && !c.inputStrings->empty());
  *c.output = hasInput ? (*c.inputStrings)[0] : std::string{};

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the actual
  // cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER:
//     [0] "String"      = String output   (the host string currency — String PRODUCER)
//     [1] "InputBuffer" = String input     (fork-stringbuilder-as-string: String covers the .NET type)
//
//   The driver gathers String input ports → inputStrings; port [1] → inputStrings[0].
//   NOT multiInput (the .cs InputSlot is single, not MultiInputSlot).
static const StringOp _reg_stringbuildertostring{
    {"StringBuilderToString", "StringBuilderToString",
     {{"String",      "String",      "String", false},
      {"InputBuffer", "InputBuffer", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookStringBuilderToString};

}  // namespace sw
