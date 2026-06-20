// IntToString string op (string self-registration seam leaf — Int + Format(string) → string).
// TiXL authority: Operators/Lib/string/convert/IntToString.cs (verbatim below):
//
//   IntToString.cs Update():
//     var v = Value.GetValue(context);              // Value = InputSlot<int>
//     var s = Format.GetValue(context);             // Format = InputSlot<string>
//     try {
//       Output.Value = string.IsNullOrEmpty(s)
//           ? v.ToString(CultureInfo.InvariantCulture)
//           : string.Format(CultureInfo.InvariantCulture, s, v);
//     } catch (FormatException) { Output.Value = "Invalid Format"; }
//
//   IntToString.t3: Value DefaultValue 0, Format DefaultValue "{0:0}".  ("{0:0}" = integer custom
//   numeric format, 0 fractional digits → v=0→"0", v=42→"42", v=-7→"-7".)
//
// EVAL-SIDE LAYOUT (mirror of FloatToString — a String PRODUCER, rides cookStringNode): its Value is
// read from the RESOLVED Float params (sc.params, the value spine — wire-driven or stored const), then
// TRUNCATED to int (fork-inttostring-int-on-float-port); its Format is its ONE String input →
// inputStrings[0] (wired upstream string, or strDef const "{0:0}" when unwired). Writes *c.output.
//
// FORKS (named — IntToString is the INTEGER sibling of FloatToString; it shares the same C# composite-
// format contract but on a narrower, integer-oriented vocabulary):
//   - fork-inttostring-int-on-float-port: TiXL's Value is InputSlot<int>; sw has only Float ports, so the
//     resolved Float param is truncated to int ((int)v) before formatting — exactly as every Int-input op
//     on this runtime. Whole-number wirings (the only thing an int slider produces) are byte-identical.
//   - fork-inttostring-format-csharp-to-printf: like FloatToString, TiXL uses C# composite format strings
//     (string.Format). C++ has no C# format engine on this toolchain, so we implement the COMMON integer
//     cases faithfully and fall back to "Invalid Format" (TiXL's FormatException branch) for anything we
//     don't recognise — the contract (valid→formatted, invalid→"Invalid Format") matches TiXL.
//   - fork-inttostring-format-narrow-vocabulary: SUPPORTED placeholder specs (InvariantCulture, '.'
//     decimal, ',' group), one placeholder {0[:spec]} with optional literal text before/after:
//       • {0}            → v.ToString(InvariantCulture) (plain signed decimal, e.g. -7 → "-7").
//       • {0:0…}         → C# CUSTOM integer custom-numeric "0" form (one-or-more '0', NO '.'): the
//                          integer zero-padded to AT LEAST that many digits. "{0:0}" → "42"; "{0:000}"
//                          on 42 → "042"; on -42 → "-042" (sign then zero-padded magnitude).
//       • {0:D<n>}/{0:d<n>} → C# standard "D" decimal, min n digits, zero-padded ("{0:D4}" 42 → "0042";
//                          -42 → "-0042"). {0:D} (no n) = plain decimal.
//       • {0:N0}/{0:n0}  → C# standard number, 0 fractional digits + ',' thousands grouping
//                          ("{0:N0}" 1234567 → "1,234,567"). (Non-zero N<n> would add ".00…" — supported
//                          via the float path of the SAME engine in FloatToString; for an INT value
//                          "{0:N2}" → "1,234,567.00" is also produced faithfully.)
//       • {0:X<n>}/{0:x<n>} → C# standard hex, upper/lower, min n hex digits ("{0:X}" 255 → "FF";
//                          "{0:x4}" 255 → "00ff"). C# hex of a NEGATIVE int uses two's-complement 32-bit
//                          ("{0:X}" -1 → "FFFFFFFF") — ported verbatim.
//       NOT SUPPORTED (→ "Invalid Format", faithful to the catch): {0:C}/{0:P}/{0:E}/{0:G}/{0:R}, '#'
//          digit placeholders, ';' sections, multiple placeholders, escaped braces, alignment ({0,5}).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Plain signed decimal of an int (== C# int.ToString(InvariantCulture)).
std::string intToDecimal(int v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d", v);
  return std::string(buf);
}

// Zero-pad an int's decimal MAGNITUDE to >= minDigits, re-prepending the sign. Shared by {0:0…} and
// {0:D<n>}. minDigits counts the digit run only (the '-' is not a padded digit, matching C#).
std::string zeroPadInt(int v, int minDigits) {
  bool neg = v < 0;
  // magnitude as unsigned to avoid INT_MIN overflow on -v
  unsigned long mag = neg ? (unsigned long)(-(long)v) : (unsigned long)v;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lu", mag);
  std::string digits(buf);
  if ((int)digits.size() < minDigits) digits = std::string(minDigits - digits.size(), '0') + digits;
  return (neg ? "-" : "") + digits;
}

// C# "N<n>" for an int value: fixed n fractional digits + ',' thousands grouping. (n=0 → no '.'.)
std::string numberGroupedInt(int v, int n) {
  char raw[64];
  std::snprintf(raw, sizeof(raw), "%.*f", n, (double)v);  // int → exact double, then n frac digits
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

// C# "X<n>"/"x<n>" hex of an int: two's-complement 32-bit, min n hex digits, upper/lower case.
std::string hexInt(int v, int n, bool upper) {
  uint32_t u = (uint32_t)v;  // two's complement 32-bit (C# casts int→uint for the hex render)
  char buf[32];
  std::snprintf(buf, sizeof(buf), upper ? "%X" : "%x", u);
  std::string digits(buf);
  if ((int)digits.size() < n) digits = std::string(n - digits.size(), '0') + digits;
  return digits;
}

// Parse a C# numeric format SPEC for an INT value (the text after "{0:" up to "}") and render. Returns
// true + writes `out` on a SUPPORTED spec; false = unrecognised (caller emits "Invalid Format").
bool tryIntSpec(const std::string& spec, int v, std::string& out) {
  if (spec.empty()) { out = intToDecimal(v); return true; }  // "{0:}" ≈ "{0}"

  // --- Standard format: a single letter (D/d/N/n/X/x) + optional precision digits. ---
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
    if (digitsOk && spec.size() <= 3 /*one letter + ≤2 precision digits*/) {
      if (n > 99) return false;  // absurd precision → invalid (FormatException spirit)
      switch (c) {
        case 'D': case 'd':                       // decimal, min n digits (zero-padded)
          out = zeroPadInt(v, hasN ? n : 1);
          return true;
        case 'N': case 'n':                       // number (grouped), n fractional digits (default 2)
          out = numberGroupedInt(v, hasN ? n : 2);
          return true;
        case 'X': case 'x':                       // hex, min n digits
          out = hexInt(v, hasN ? n : 1, c == 'X');
          return true;
        default: break;                            // C/P/E/G/R… → fall through to custom / invalid
      }
    }
  }

  // --- Custom numeric format: the simple zero-pad form {0:0…} (one-or-more '0', NO '.'). '#', ';',
  // '.', grouping, scaling etc. are out of vocabulary for the INTEGER op → "Invalid Format". ---
  {
    bool allZeros = !spec.empty();
    for (char ch : spec) if (ch != '0') { allZeros = false; break; }
    if (allZeros) {
      out = zeroPadInt(v, (int)spec.size());  // "0" → min 1 digit; "000" → min 3 digits
      return true;
    }
  }
  return false;  // unrecognised spec → "Invalid Format"
}

// Apply a C# composite format (one {0[:spec]} placeholder + literal text before/after) for an int.
// Mirror of FloatToString's tryComposite, narrowed to the int spec engine. Returns true + writes `out`
// on success; false = unrecognised (caller emits "Invalid Format", TiXL's FormatException branch).
bool tryCompositeInt(const std::string& fmt, int v, std::string& out) {
  std::string::size_type open = fmt.find('{');
  if (open == std::string::npos) return false;                       // no placeholder → FormatException
  if (fmt.find('{', open + 1) != std::string::npos) return false;    // >1 '{' → out of vocabulary
  if (open + 1 >= fmt.size() || fmt[open + 1] != '0') return false;  // must start with index 0
  std::string::size_type close = fmt.find('}', open);
  if (close == std::string::npos) return false;                      // unbalanced → FormatException
  if (fmt.find('}', close + 1) != std::string::npos) return false;   // stray '}' → out of vocabulary

  std::string inner = fmt.substr(open + 1, close - open - 1);  // "0" or "0:spec"
  std::string spec;
  if (inner == "0") {
    spec = std::string();  // {0} → default ToString
  } else if (inner.size() >= 2 && inner[1] == ':') {
    spec = inner.substr(2);  // "0:D4" → "D4"
  } else {
    return false;  // "{0X…}" malformed alignment/etc → out of vocabulary
  }

  std::string rendered;
  if (spec.empty()) {
    rendered = intToDecimal(v);  // {0}
  } else if (!tryIntSpec(spec, v, rendered)) {
    return false;  // unrecognised spec → "Invalid Format"
  }
  out = fmt.substr(0, open) + rendered + fmt.substr(close + 1);  // literal-before + value + literal-after
  return true;
}

// IntToString: Value (resolved Float param, truncated to int) + Format (String input) → host string.
void cookIntToString(StringCookCtx& c) {
  if (!c.output) return;
  const int v = (int)stringFloatParam(c.params, "Value", 0.0f);  // IntToString.cs Value.GetValue, (int) cast
  std::string fmt;
  if (c.inputStrings && !c.inputStrings->empty()) fmt = (*c.inputStrings)[0];  // Format (inputStrings[0])

  std::string result;
  if (fmt.empty()) {
    result = intToDecimal(v);  // string.IsNullOrEmpty(s) ? v.ToString(InvariantCulture)
  } else if (!tryCompositeInt(fmt, v, result)) {
    result = "Invalid Format";  // FormatException branch (faithful to the catch)
  }
  *c.output = result;

  // Test-only: corrupt the REAL output (drop the last char) so a golden's transport RED bites on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — leaf .cpp (registered via the StringOp seam; its
// CMake entry must be added to app/CMakeLists.txt — the string_ops_*.cpp list is NOT globbed).
//   Ports: "Value"  = scalar Float input (read via resolved params, truncated to int);
//          "Format" = String input (wire-OR-const; strDef "{0:0}" = IntToString.t3 default → integer);
//          "Output" = the String output (host string currency).
static const StringOp _reg_inttostring{
    {"IntToString", "IntToString",
     {{"Output", "Output", "String", false},
      {"Value", "Value", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider},
      {"Format", "Format", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "{0:0}"}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookIntToString};

}  // namespace sw
