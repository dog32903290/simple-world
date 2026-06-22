// StringBuilder string op (string self-registration seam leaf — InitialString(String) + ClearTrigger →
// String producer). TiXL authority: Operators/TypeOperators/NET/StringBuilder.cs (verbatim below):
//
//   StringBuilder.cs Update():
//     var initialString = InitialString.GetValue(context);
//     var needsReset = ClearTrigger.GetValue(context);
//     if (!_initialized) { needsReset = true; _initialized = true; }
//     if (needsReset) { _builder.Clear(); _builder.Append(initialString); }
//     Builder.Value = _builder;
//
//   Ports: ClearTrigger = InputSlot<bool>; InitialString = InputSlot<string>.
//          Output: Builder = Slot<System.Text.StringBuilder> (the mutable accumulator).
//
// FORK: fork-stringbuilder-no-resident-state (CORE DESIGN DECISION — documented here and in golden):
//   TiXL's StringBuilder is STATEFUL: it holds a System.Text.StringBuilder across frames; subsequent
//   ops (AppendString etc.) mutate it in-place. sw has no cross-frame instance state (no StringBuilder
//   resident lifetime rail). In a sw graph the ONLY connection downstream is StringBuilderToString,
//   which converts to the String currency. Since sw is stateless-per-frame and no AppendString /
//   TrimEnd / other mutators are implemented, the observable output of this op equals InitialString
//   (the builder is cleared and set to InitialString every frame, just as the .cs _initialized path).
//   Therefore: cook = output InitialString (or "" if unwired). The ClearTrigger port is DECLARED (for
//   parity shape) but IGNORED in the cook — it never changes the output since the sw "builder" is
//   logically reset each frame anyway (fork-stringbuilder-no-resident-state).
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

// StringBuilder: output InitialString (inputStrings[0]) as the "builder state".
// ClearTrigger is structurally declared but ignored (fork-stringbuilder-no-resident-state).
void cookStringBuilder(StringCookCtx& c) {
  if (!c.output) return;

  // InitialString = inputStrings[0] (the one String input port).
  // Unwired → "" (same as TiXL Append("") after Clear — empty string).
  const bool hasInput = (c.inputStrings && !c.inputStrings->empty());
  *c.output = hasInput ? (*c.inputStrings)[0] : std::string{};

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the actual
  // cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER:
//     [0] "Builder"      = String output        (fork-stringbuilder-as-string: covers .NET StringBuilder)
//     [1] "InitialString"= String input          (wire-OR-const; default "" → empty builder)
//     [2] "ClearTrigger" = Float input (bool)    (fork-stringbuilder-no-resident-state: port declared,
//                                                  dissolved bool→Float per Cut32 convention; IGNORED)
//
//   ClearTrigger is dataType "Float" (bool dissolved to Float per the sw no-Bool-port convention).
//   The driver gathers String input ports → inputStrings; port [1] → inputStrings[0].
//   NOT multiInput on either input port.
static const StringOp _reg_stringbuilder{
    {"StringBuilder", "StringBuilder",
     {{"Builder",       "Builder",       "String", false},
      {"InitialString", "InitialString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, ""},
      {"ClearTrigger",  "ClearTrigger",  "Float",  true, 0.0f, 0.0f, 1.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookStringBuilder};

}  // namespace sw
