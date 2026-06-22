// FloatListToString string op (string self-registration seam leaf — FloatList Value + Format(String) +
// Separator(String) → String). Sub-seam A: the FloatList-into-string BRIDGE proving op (consumes the
// EXISTING FloatList currency via the new StringCookCtx::inputFloatLists channel; emits a String).
// TiXL authority: Operators/Lib/string/combine/FloatListToString.cs (verbatim below):
//
//   FloatListToString.cs Update():
//     var values = Value.GetValue(context);
//     if (values == null || values.Count == 0) Output.Value = "";   // (note: falls through, see fork)
//     var format = Format.GetValue(context);
//     var sep = Separator.GetValue(context);
//     if (string.IsNullOrWhiteSpace(format)) format = "{0:0.00}";
//     if (sep == null) sep = ""; else sep = sep.Replace("\\n", "\n");
//     try {
//       _stringBuilder.Clear();
//       foreach (var v in values) { _stringBuilder.Append(v.ToString(format, InvariantCulture));
//                                   _stringBuilder.Append(sep); }       // TRAILING sep after EACH value
//       Output.Value = _stringBuilder.ToString();
//     } catch (FormatException) { Output.Value = "Invalid Format"; }
//
//   Ports: Value = InputSlot<List<float>>; Format = InputSlot<string>; Separator = InputSlot<string>.
//   Output: Output = Slot<string>.
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode / cookResidentString). Value is a FloatList
// input → gathered into StringCookCtx::inputFloatLists[0] (the EXISTING FloatList currency, wired into
// the string ctx — the bridge). Format + Separator are the TWO String inputs → inputStrings[0]/[1]
// (wire-OR-const). No Float params.
//
// SEMANTICS (ported from the .cs, with the divergences NAMED below):
//   • empty/null/whitespace Format → default "{0:0.00}" (see fork-floatlist-default-format).
//   • Separator: a const "" stays ""; "\\n" (literal backslash-n) → real newline (same escape fork as
//     CombineStrings / SplitString). The separator is appended AFTER EVERY value, so the result has a
//     TRAILING separator (NOT a true join — fork-floatlist-trailing-separator).
//   • each value v → v.ToString(format, InvariantCulture): a C# single-value custom/standard numeric
//     format (NOT string.Format composite). Supported vocabulary (parity): standard F/f, N/n, E/e (+ opt
//     precision) and the custom zero-pad form "[0].000…". Unsupported spec → "Invalid Format" (the .cs
//     FormatException branch), applied to the WHOLE output (TiXL catches around the whole loop).
//   • empty list → "" (the .cs early line — and the builder loop then appends nothing → still "").
//
// FORKS (named):
//   - fork-floatlist-default-format: TiXL's empty-Format default is the LITERAL "{0:0.00}", which it
//     then feeds to v.ToString("{0:0.00}") — a CUSTOM format whose '{',':','}' are literal chars, so C#
//     emits braces/colon around the number (a TiXL quirk). We do NOT reproduce the literal-brace quirk;
//     instead, when Format is empty/whitespace we render the INTENDED 2-fixed-digit form (v.ToString in
//     "0.00"), which is what the default visibly means. A golden that wants byte-parity drives an EXPLICIT
//     clean Format ("F1" / "0.00") — the unambiguous parity path. The empty-default fork is render-only.
//   - fork-int-float-dissolve: FloatList is host std::vector<float>; TiXL List<float> is the same — no
//     Int port here (Value is a float list), so no int/float dissolve on the list itself. The downstream
//     numeric format treats each float per the spec (F0 truncates the fractional part = the int view).
//   - fork-floatlist-trailing-separator: separator appended after EACH value (trailing), NOT joined
//     between — faithful to the .cs builder loop (contrast JoinStringList's true string.Join).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Fixed-point: n fractional digits (InvariantCulture '.'). Shared by "F<n>" and the custom "0.000…".
std::string fixedPoint(float v, int n) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%.*f", n, (double)v);
  return std::string(buf);
}

// C# "N<n>": fixed-point + ',' thousands grouping in the integer part (InvariantCulture).
std::string numberGrouped(float v, int n) {
  char raw[160];
  std::snprintf(raw, sizeof(raw), "%.*f", n, (double)v);
  std::string s(raw);
  bool neg = !s.empty() && s[0] == '-';
  if (neg) s.erase(s.begin());
  std::string::size_type dot = s.find('.');
  std::string intPart = (dot == std::string::npos) ? s : s.substr(0, dot);
  std::string frac = (dot == std::string::npos) ? std::string() : s.substr(dot);  // includes '.'
  std::string grouped;
  int cnt = 0;
  for (std::string::size_type i = intPart.size(); i-- > 0;) {
    grouped.push_back(intPart[i]);
    if (++cnt % 3 == 0 && i != 0) grouped.push_back(',');
  }
  std::reverse(grouped.begin(), grouped.end());
  return (neg ? "-" : "") + grouped + frac;
}

// C# "E<n>"/"e<n>": n fractional digits, exponent ALWAYS signed, padded to ≥3 digits. `upper` = 'E'/'e'.
std::string scientific(float v, int n, bool upper) {
  char raw[160];
  std::snprintf(raw, sizeof(raw), "%.*e", n, (double)v);
  std::string s(raw);
  std::string::size_type ep = s.find('e');
  if (ep == std::string::npos) return s;
  std::string mant = s.substr(0, ep);
  char sign = s[ep + 1];
  std::string digits = s.substr(ep + 2);
  while (digits.size() < 3) digits.insert(digits.begin(), '0');
  return mant + (upper ? "E" : "e") + sign + digits;
}

// v.ToString(spec, InvariantCulture) for the SINGLE-VALUE numeric format vocabulary (NOT composite —
// no "{0:…}" wrapping here; spec is the raw numeric format like "F1" / "0.00" / "N2"). Returns false
// on an unsupported spec (→ the caller's "Invalid Format", TiXL's FormatException). Mirrors the
// FloatToString TU's tryNumericSpec but standalone (the format engines are per-TU static).
bool toStringNumeric(const std::string& spec, float v, std::string& out) {
  if (spec.empty()) { out = fixedPoint(v, 2); return true; }  // empty spec ≈ default 2 fixed (fork)

  // Standard numeric format: one letter (F/N/E) + optional precision digits.
  {
    char c = spec[0];
    bool hasN = false;
    int n = 0;
    bool digitsOk = true;
    for (std::string::size_type i = 1; i < spec.size(); ++i) {
      char ch = spec[i];
      if (ch < '0' || ch > '9') { digitsOk = false; break; }
      hasN = true;
      n = n * 10 + (ch - '0');
    }
    if (digitsOk && spec.size() <= 3) {
      if (n > 99) return false;
      switch (c) {
        case 'F': case 'f': out = fixedPoint(v, hasN ? n : 2); return true;
        case 'N': case 'n':
          if (std::isnan(v) || std::isinf(v)) { out = std::isnan(v) ? "NaN" : (v < 0 ? "-Infinity" : "Infinity"); return true; }
          out = numberGrouped(v, hasN ? n : 2); return true;
        case 'E': case 'e':
          if (std::isnan(v) || std::isinf(v)) { out = std::isnan(v) ? "NaN" : (v < 0 ? "-Infinity" : "Infinity"); return true; }
          out = scientific(v, hasN ? n : 6, c == 'E'); return true;
        default: break;
      }
    }
  }

  // Custom zero-pad: "[0].000…" (optional leading '0' int placeholder, optional '.', run of '0' frac).
  {
    std::string::size_type i = 0;
    if (i < spec.size() && spec[i] == '0') ++i;
    int fracDigits = -1;
    if (i < spec.size() && spec[i] == '.') {
      ++i;
      fracDigits = 0;
      while (i < spec.size() && spec[i] == '0') { ++fracDigits; ++i; }
    }
    if (i == spec.size() && (fracDigits >= 0 || spec == "0")) {
      int n = (fracDigits >= 0) ? fracDigits : 0;
      if (n > 99) return false;
      if (std::isnan(v) || std::isinf(v)) { out = std::isnan(v) ? "NaN" : (v < 0 ? "-Infinity" : "Infinity"); return true; }
      out = fixedPoint(v, n);
      return true;
    }
  }
  return false;  // unsupported spec → "Invalid Format"
}

void cookFloatListToString(StringCookCtx& c) {
  if (!c.output) return;

  // Value: the ONE FloatList input → inputFloatLists[0] (the EXISTING FloatList currency, the bridge).
  // Unwired / empty → empty list → "" (FloatListToString.cs:19-20).
  const std::vector<float> values =
      (c.inputFloatLists && !c.inputFloatLists->empty()) ? (*c.inputFloatLists)[0] : std::vector<float>{};

  // Format = inputStrings[0]; Separator = inputStrings[1] (wire-OR-const).
  std::string format = (c.inputStrings && c.inputStrings->size() > 0) ? (*c.inputStrings)[0] : std::string{};
  std::string sep = (c.inputStrings && c.inputStrings->size() > 1) ? (*c.inputStrings)[1] : std::string{};

  // string.IsNullOrWhiteSpace(format) → default (FloatListToString.cs:24). fork-floatlist-default-format:
  // we use the clean "0.00" intent, not the literal-brace "{0:0.00}" quirk.
  bool ws = true;
  for (char ch : format) if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') { ws = false; break; }
  if (ws) format = "0.00";

  // Separator: "\\n" (literal backslash-n) → newline (FloatListToString.cs:35).
  {
    std::string::size_type p = 0;
    while ((p = sep.find("\\n", p)) != std::string::npos) { sep.replace(p, 2, "\n"); p += 1; }
  }

  // Build: append each value (formatted) then the separator (TRAILING — fork-floatlist-trailing-separator).
  // An unsupported format throws FormatException in C# → "Invalid Format" for the WHOLE output.
  std::string result;
  bool invalid = false;
  for (float v : values) {
    std::string piece;
    if (!toStringNumeric(format, v, piece)) { invalid = true; break; }
    result += piece;
    result += sep;
  }
  *c.output = invalid ? std::string("Invalid Format") : result;  // empty list → "" (loop body skipped)

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the actual
  // cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order; FloatList ports → inputFloatLists, String → inputStrings):
//     [0] "Output"    = String output (the host string currency — String PRODUCER)
//     [1] "Value"     = FloatList input  (the EXISTING FloatList currency — the bridge → inputFloatLists[0])
//     [2] "Format"    = String input     (wire-OR-const; strDef "" → default "0.00")
//     [3] "Separator" = String input     (wire-OR-const; strDef "" → no separator)
//   The driver gathers FloatList input ports into inputFloatLists (port [1] → [0]) and String input ports
//   into inputStrings in spec order (ports [2]/[3] → [0]/[1] == Format/Separator).
static const StringOp _reg_floatlisttostring{
    {"FloatListToString", "FloatListToString",
     {{"Output", "Output", "String", false},
      {"Value", "Value", "FloatList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"Format", "Format", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"Separator", "Separator", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookFloatListToString};

}  // namespace sw
