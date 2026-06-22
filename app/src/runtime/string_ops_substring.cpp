// SubString string op (string self-registration seam leaf — InputText(String) + Start(Int) +
// Length(Int) → String). TiXL authority: Operators/Lib/string/search/SubString.cs (verbatim below):
//
//   SubString.cs Update():
//     var str = InputText.GetValue(context);
//     var start = Start.GetValue(context);
//     var length = Length.GetValue(context);
//
//     var clampStart = start.Clamp(0, str.Length);
//     var clampedLength = length.Clamp(0, str.Length - clampStart);
//
//     if (string.IsNullOrEmpty(str) || clampedLength == 0 || clampStart >= str.Length)
//     {
//         Result.Value = string.Empty;
//         return;
//     }
//
//     // Return full string
//     if(start == 0 &&  length >= str.Length)
//     {
//         Result.Value = str;
//     }
//     else
//     {
//         try
//         {
//             Result.Value = str.Substring(clampStart, clampedLength);
//         }
//         catch (Exception e)
//         {
//             Log.Warning("Failed to get substring: " + e.Message, this);
//         }
//     }
//
//   Ports: InputText = InputSlot<string>;  Start = InputSlot<int>;  Length = InputSlot<int>.
//   Output: Result = Slot<string>.
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). InputText is the ONE String input
// port → inputStrings[0] (wired upstream string, or strDef const "" when unwired). Start and Length
// are Int params dissolved to Float (the value spine, resolved via stringFloatParam / params) — the
// same fork as every Int-input op already ported (fork-int-bool-dissolve-to-float, Cut32 convention).
//
// CLAMPING SEMANTICS (ported 1:1 from the .cs):
//   clampStart  = Clamp(start,  0, str.length())     — negative start → 0; past-end → str.length()
//   clampedLen  = Clamp(length, 0, str.length() - clampStart)  — negative len → 0; overrun → rest
//   Early-exit:  empty str, clampedLen == 0, or clampStart >= str.length() → ""
//   Fast path:   start==0 && length >= (int)str.length() → full str (faithful to .cs; unclamped check)
//   Else:        str.substr(clampStart, clampedLen)
// The try/catch in the .cs is a defensive net around std::string::substr; after correct clamping it is
// unreachable — we do not port it (the clamped indices are provably safe).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL Start/Length are InputSlot<int>; sw has no Int port type,
//     so both dissolve int→Float (stored in params / resolved via the value spine). Values are read
//     back via (int)(float) — same as every Int-input op ported so far (Cut32 convention).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
//   - fork-no-dirtyflag-trigger: TiXL has no visible DirtyFlag clear for SubString; the flat cook is
//     stateless, no-op fork (same as CombineStrings).
#include <algorithm>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// SubString: InputText (String input) + Start (Int param) + Length (Int param) → host string.
// Implements TiXL SubString.cs Update() 1:1, with Int params dissolved to Float (fork named above).
void cookSubString(StringCookCtx& c) {
  if (!c.output) return;

  // InputText: first (and only) String input port — wired upstream string, or strDef const "" when
  // unwired. inputStrings[0] is InputText; Start and Length are Float params (Int dissolved to Float).
  const std::string str =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};

  const int start  = (int)stringFloatParam(c.params, "Start",  0.0f);
  const int length = (int)stringFloatParam(c.params, "Length", 0.0f);

  // Clamp logic from SubString.cs:22-23 (verbatim):
  //   clampStart  = start.Clamp(0, str.Length)
  //   clampedLength = length.Clamp(0, str.Length - clampStart)
  const int strLen      = (int)str.size();
  const int clampStart  = std::max(0, std::min(start,  strLen));
  const int clampedLen  = std::max(0, std::min(length, strLen - clampStart));

  // Early-exit: SubString.cs:25-29
  if (str.empty() || clampedLen == 0 || clampStart >= strLen) {
    *c.output = std::string{};
    // inject bug even on empty path so the bug check bites the output write
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  // Fast path: SubString.cs:32-34 (unclamped start/length, faithful to .cs)
  if (start == 0 && length >= strLen) {
    *c.output = str;
  } else {
    // Normal path: SubString.cs:38-40 (try block — after correct clamping always safe)
    *c.output = str.substr(static_cast<std::string::size_type>(clampStart),
                           static_cast<std::string::size_type>(clampedLen));
  }

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputStrings):
//     [0] "Result"    = String output (the host string currency — String PRODUCER)
//     [1] "InputText" = String input  (wire-OR-const; strDef "" = empty, no text by default)
//     [2] "Start"     = Float/Int input (value spine; Int dissolved to Float; default 0)
//     [3] "Length"    = Float/Int input (value spine; Int dissolved to Float; default 0)
//   The driver gathers String input ports into inputStrings in spec order: only port [1] is a String
//   input, so inputStrings[0] == InputText (wired or strDef ""). Start and Length ride params.
static const StringOp _reg_substring{
    {"SubString", "SubString",
     {{"Result",    "Result",    "String", false},
      {"InputText", "InputText", "String", true,  0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"Start",     "Start",     "Float",  true,  0.0f, -1000000.0f, 1000000.0f, Widget::Slider},
      {"Length",    "Length",    "Float",  true,  0.0f, -1000000.0f, 1000000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookSubString};

}  // namespace sw
