// ChangeCase string op (string self-registration seam leaf — InputText(String) + Mode(enum) →
// String). TiXL authority: Operators/Lib/string/transform/ChangeCase.cs (verbatim below):
//
//   ChangeCase.cs Update():
//     var str = InputText.GetValue(context);
//     var mode = Mode.GetEnumValue<Modes>(context);
//     switch (mode) {
//       case Modes.ToUpperCase:
//         Result.Value = str?.ToUpperInvariant();
//         break;
//       case Modes.ToLowerCase:
//         Result.Value = str?.ToLowerInvariant();
//         break;
//     }
//
//   enum Modes { ToUpperCase = 0, ToLowerCase = 1 };
//   Ports: InputText = InputSlot<string>; Mode = InputSlot<int> (MappedType=Modes). Output: Result (string).
//   ChangeCase.t3 default: Mode = 0 (ToUpperCase).
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). Its InputText is its ONE String input →
// inputStrings[0] (wired upstream string, or strDef const "" when unwired). Mode is read from the
// RESOLVED Float params (sc.params, the value spine — widget=Widget::Enum, stored as Float index):
//   mode = (int) stringFloatParam(c.params, "Mode", 0.0f) — same int-on-Float contract as every
//   enum-param op (CompareInt.Mode, etc.). Clamped to [0,1] (mirror of TiXL's .Clamp pattern).
//
// FORKS (named):
//   - fork-changecase-invariant-culture: TiXL calls str?.ToUpperInvariant() / str?.ToLowerInvariant()
//     (C# INVARIANT-culture case folding, NOT locale-aware ToUpper()/ToLower()). C++ std::toupper /
//     std::tolower with the "C" locale ('setlocale' neutral) maps ASCII identically to invariant. For
//     the printable ASCII range (0x20-0x7E) the mapping is byte-identical to C# InvariantCulture. For
//     non-ASCII (multi-byte UTF-8 sequences) the C runtime's "C" locale leaves bytes unchanged, whereas
//     C# InvariantCulture applies Unicode title mappings — this difference exists and is named here.
//     Real TiXL usage is overwhelmingly ASCII strings; the gap is known and explicitly accepted.
//   - fork-changecase-null-input: TiXL str?.ToUpperInvariant() is null-conditional: if the input is
//     null, Result stays null (unset). sw has no null strings — an unwired InputText yields its
//     strDef const "" (empty string). Applying ToUpper/ToLower to "" → "" = no-op; the observable
//     effect is identical to leaving the output unset for a downstream StringLength (both yield 0).
//   - fork-changecase-mode-clamp: out-of-range Mode is clamped to [0,1] (faithful to TiXL's integer
//     .Clamp idiom for MappedType enums). Default mode = 0 (ToUpperCase).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <cctype>
#include <locale>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Apply ASCII-safe invariant uppercase to every byte (fork-changecase-invariant-culture).
std::string toUpperInvariant(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    // std::toupper with unsigned char guard (UB if char is negative without cast).
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

// Apply ASCII-safe invariant lowercase to every byte (fork-changecase-invariant-culture).
std::string toLowerInvariant(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// ChangeCase: InputText (String) + Mode (enum: 0=ToUpperCase, 1=ToLowerCase) → host string.
void cookChangeCase(StringCookCtx& c) {
  if (!c.output) return;

  // InputText: the first (and only) String input — wired upstream string, or strDef "" when unwired.
  std::string input;
  if (c.inputStrings && !c.inputStrings->empty()) input = (*c.inputStrings)[0];

  // Mode: resolved Float param, truncated to int, clamped to [0,1] (fork-changecase-mode-clamp).
  const float modeF = stringFloatParam(c.params, "Mode", 0.0f);
  int mode = static_cast<int>(modeF);
  if (mode < 0) mode = 0;
  if (mode > 1) mode = 1;

  switch (mode) {
    case 0:  // ToUpperCase — str?.ToUpperInvariant()
      *c.output = toUpperInvariant(input);
      break;
    case 1:  // ToLowerCase — str?.ToLowerInvariant()
      *c.output = toLowerInvariant(input);
      break;
    default:
      *c.output = input;  // unreachable after clamp; defensive no-op
      break;
  }

  // Test-only: corrupt the REAL output (drop the last char) so golden RED case fires on the actual
  // cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports (ORDER MATTERS for the gather): "Result" output first, then "InputText" (single String
//   input, strDef=""), then "Mode" (Float, Widget::Enum, labels=[ToUpperCase,ToLowerCase]).
//   ChangeCase.t3 default: Mode = 0 (ToUpperCase).
static const StringOp _reg_changecase{
    {"ChangeCase", "ChangeCase",
     {{"Result", "Result", "String", false},
      {"InputText", "InputText", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, /*strDef=*/""},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"ToUpperCase", "ToLowerCase"}}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookChangeCase};

}  // namespace sw
