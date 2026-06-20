// FloatToString string op (string self-registration seam leaf — Float + Format(string) → string).
// TiXL authority: Operators/Lib/string/convert/FloatToString.cs:22 (verbatim below):
//
//   FloatToString.cs Update():
//     var v = Value.GetValue(context);
//     var s = Format.GetValue(context);
//     try {
//       Output.Value = string.IsNullOrEmpty(s)
//           ? v.ToString(CultureInfo.InvariantCulture)
//           : string.Format(CultureInfo.InvariantCulture, s, v);
//     } catch (FormatException) { Output.Value = "Invalid Format"; }
//
//   Ports: Value = InputSlot<float>; Format = InputSlot<string>. Output: Slot<string>.
//   Format DEFAULT (FloatToString.t3:6): "{0:0.000}" — a C# CUSTOM numeric format (zero-padded to 3
//   fractional digits). So the DEFAULT cook is NOT empty → it goes through string.Format, not ToString:
//   v=0 → "0.000", v=3.14 → "3.140".
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). Its Value is read from the RESOLVED
// Float params (sc.params, the value spine — wire-driven or stored const); its Format is its ONE
// String input → inputStrings[0] (wired upstream string, or strDef const "{0:0.000}" when unwired).
//
// FORK (named):
//   - fork-floattostring-format-csharp-to-printf: TiXL uses C# composite format strings
//     (string.Format, e.g. "{0:0.000}"). C++ has no std::format on this toolchain's baseline + no C#
//     numeric format-spec engine. We implement the COMMON cases faithfully and fall back to
//     "Invalid Format" (TiXL's FormatException branch) for anything we don't recognise — so the
//     contract (valid→formatted, invalid→"Invalid Format") matches TiXL even where the exact format
//     vocabulary is narrower.
//   - fork-floattostring-format-narrow-vocabulary: the C# numeric-format vocabulary we SUPPORT vs.
//     deliberately leave as "Invalid Format". A composite format is one placeholder {0[:spec]} with
//     optional literal text before/after (string.Format keeps the literal text verbatim, substitutes
//     the placeholder).
//       SUPPORTED placeholder specs (InvariantCulture, '.' decimal, ',' group):
//         • {0}            → v.ToString(InvariantCulture)  (shortest round-trip; same as empty Format)
//         • {0:0.000…}     → C# CUSTOM numeric: N trailing '0' after '.' = N fixed fractional digits.
//                            {0:0} → 0 digits (integer). Only the simple zero-pad form (optional one
//                            '0' integer placeholder + optional '.' + run of '0') is recognised.
//         • {0:F<n>}/{0:f<n>} → C# standard fixed-point, n fractional digits (= %.<n>f).
//         • {0:N<n>}/{0:n<n>} → C# standard number, n fractional digits + ',' thousands grouping.
//         • {0:E<n>}/{0:e<n>} → C# standard scientific, n fractional digits, C#-style exponent
//                            (sign + ≥3 exponent digits, e.g. 1.0E+006); 'e' → lowercase 'e'.
//       NOT SUPPORTED (→ "Invalid Format", faithful to the catch; NOT silent passthrough):
//         • {0:C}/{0:P}/{0:X}/{0:G}/{0:R}/{0:D} and other standard specifiers (currency/percent/hex/
//           general/round-trip/decimal) — rarely used here, narrow vocabulary by design.
//         • Complex custom formats: '#' digit placeholders, ';' section separators, scaling/grouping
//           inside custom strings, multiple placeholders, escaped '{{'/'}}'.
//       empty Format → v.ToString(InvariantCulture): shortest round-trip decimal, in C#'s default "G"
//          layout (NaN→"NaN", +Inf→"Infinity", -Inf→"-Infinity"; -0f→"-0"). Decimal exponent E of the
//          leading digit ∈ [-4,6] → PLAIN decimal (1000000→"1000000", 0.0001→"0.0001"); otherwise
//          SCIENTIFIC: shortest mantissa + uppercase 'E' + sign + MIN-2-digit exponent (1e11→"1E+11",
//          1e-5→"1E-05"). This is float-shortest-sourced, NOT (double)v — so no binary noise leaks
//          into the default rendering. (The standard "E<n>" SPECIFIER differs: min-3-digit exponent.)
//   - fork-floattostring-net10-precision-noise: TiXL targets net10.0; since .NET Core 3.0 the
//     high-precision F/N/E specifiers EXPOSE the exact IEEE-754 value rather than zero-padding the
//     shortest decimal — "{0:F8}" 3.14 → "3.14000010" (not "3.14000000"), "{0:E10}" 0.1 →
//     "1.0000000149E-001" (not "…0000E-001"). We match net10.0 by feeding (double)v to %.*f/%.*e in
//     the SPECIFIER path (faithful), while the DEFAULT/general path stays float-shortest (above).
//   - fork-floattostring-stray-close-brace: a composite with an UNESCAPED '}' BEFORE the '{0…}'
//     placeholder (e.g. "}{0:F1}") is kept as literal-before-text → "}3.1", whereas C# string.Format
//     throws FormatException → "Invalid Format". We do NOT run a full brace-escape state machine
//     (the strDef default is "{0:0.000}" and real wirings don't emit lone '}' before the placeholder)
//     — narrower than C# on this extreme edge only; render of valid formats is unaffected. (A stray
//     '}' AFTER the placeholder, or '{{'/'}}' escapes, or >1 placeholder → "Invalid Format", as C#.)
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// C# `float.ToString(InvariantCulture)` = the SHORTEST decimal that round-trips back to the same
// float (the "R"/shortest-roundtrip rule; .NET Core 3.0+ / .NET 10 default for float — TiXL targets
// net10.0). NaN/±Infinity use C#'s literal spellings ("NaN"/"Infinity"/"-Infinity") — NOT printf's
// "nan"/"inf"/"-inf".
//
// This is the SOURCE engine: every default/general rendering (and the load-bearing scientific-form
// switch) hangs off the FLOAT shortest round-trip, NOT a (double)v reinterpretation. (double)v exposes
// the float→double binary-expansion noise — fine for the high-precision F/N/E specifiers, which C#
// DELIBERATELY exposes too [.NET Core 3.0+: (3.14f).ToString("F10") == "3.1400001049", verified vs
// learn.microsoft.com standard-numeric-format-strings], but WRONG for the default/general format,
// which must stay shortest.
//
// %g sweep gives us the shortest significant-digit run. C# default (= "G") format rule for the
// decimal exponent E of the shortest value (E = power of 10 of the leading digit):
//   • E in [-4, 6]  → PLAIN decimal     (1e6 → "1000000", 1e-4 → "0.0001"); fixed-point, no exponent.
//   • otherwise     → SCIENTIFIC: shortest mantissa + uppercase 'E' + sign + ≥2 exponent digits
//                                   (1e11 → "1E+11", 1e-5 → "1E-05", 1.234e-5 → "1.234E-05").
// (Switchover: large at exp≥7, small at exp<-4. Default G exponent is MIN 2 digits, uppercase E —
//  distinct from the standard "E<n>" specifier, which is min 3 digits: 0.1/E10 → "1.…E-001".)
std::string defaultToString(float v) {
  if (std::isnan(v)) return "NaN";                       // C# float.NaN.ToString() == "NaN"
  if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";  // C# ±Infinity literals
  if (v == 0.0f) return std::signbit(v) ? "-0" : "0";   // C# (-0f).ToString() == "-0"

  char buf[64];
  std::string shortest;
  for (int prec = 1; prec <= 9; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, (double)v);
    if (std::strtof(buf, nullptr) == v) { shortest = buf; break; }  // shortest that round-trips
  }
  if (shortest.empty()) {
    std::snprintf(buf, sizeof(buf), "%.9g", (double)v);  // fallback (always round-trips)
    shortest = buf;
  }

  // Recover the shortest mantissa DIGITS and the decimal exponent E from the %g rendering (whether %g
  // chose fixed or scientific form), so we can re-emit in C#'s G layout independent of %g's threshold.
  bool neg = !shortest.empty() && shortest[0] == '-';
  std::string body = neg ? shortest.substr(1) : shortest;
  std::string mantDigits;   // significant digits, no '.', no leading zeros (e.g. "1234")
  int exp = 0;              // decimal exponent of the LEADING digit (value = 0.d1d2… × 10^(exp+1))
  {
    std::string::size_type ep = body.find_first_of("eE");
    int sciExp = 0;
    std::string num = body;
    if (ep != std::string::npos) { sciExp = std::atoi(body.c_str() + ep + 1); num = body.substr(0, ep); }
    std::string::size_type dot = num.find('.');
    std::string ip = (dot == std::string::npos) ? num : num.substr(0, dot);
    std::string fp = (dot == std::string::npos) ? std::string() : num.substr(dot + 1);
    std::string allDigits = ip + fp;
    // exponent of the leading digit before trimming = (#int digits - 1) + sciExp.
    int lead = (int)ip.size() - 1 + sciExp;
    // strip leading zeros (track how many we dropped to keep `lead` pointing at the true leading digit)
    std::string::size_type firstNz = allDigits.find_first_not_of('0');
    if (firstNz == std::string::npos) { mantDigits = "0"; exp = 0; }
    else {
      lead -= (int)firstNz;
      allDigits = allDigits.substr(firstNz);
      // strip trailing zeros (shortest mantissa)
      std::string::size_type lastNz = allDigits.find_last_not_of('0');
      mantDigits = allDigits.substr(0, lastNz + 1);
      exp = lead;
    }
  }

  std::string sign = neg ? "-" : "";

  // PLAIN-decimal band: exp ∈ [-4, 6].
  if (exp >= -4 && exp <= 6) {
    std::string out;
    if (exp >= 0) {
      // integer part = first (exp+1) mantissa digits (pad with '0' if mantissa shorter).
      int intLen = exp + 1;
      if ((int)mantDigits.size() <= intLen) {
        out = mantDigits + std::string(intLen - mantDigits.size(), '0');
      } else {
        out = mantDigits.substr(0, intLen) + "." + mantDigits.substr(intLen);
      }
    } else {
      // 0.00…<mantissa> with (-exp-1) leading fractional zeros.
      out = "0." + std::string(-exp - 1, '0') + mantDigits;
    }
    return sign + out;
  }

  // SCIENTIFIC: <d>[.<rest>]E<sign><≥2-digit exp>. Uppercase E (C# default = "G" → uppercase).
  std::string mant = mantDigits.substr(0, 1);
  if (mantDigits.size() > 1) mant += "." + mantDigits.substr(1);
  char es = exp < 0 ? '-' : '+';
  int ae = exp < 0 ? -exp : exp;
  char ebuf[16];
  std::snprintf(ebuf, sizeof(ebuf), "%02d", ae);  // C# G exponent: minimum 2 digits, sign always.
  return sign + mant + "E" + es + ebuf;
}

// Render N fixed fractional digits (InvariantCulture '.'); shared by {0:0.000…} and {0:F<n>}.
std::string fixedPoint(float v, int n) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%.*f", n, (double)v);
  return std::string(buf);
}

// Render C# standard "N<n>" number format: fixed-point with n fractional digits + ',' thousands
// grouping in the integer part (InvariantCulture). C# rounds (away-from-zero/banker's both round
// 1234.5 → "1,234.50" identically at this precision; printf's round-half-to-even matches the common
// cases the parity goldens assert). e.g. 1234.5 / N2 → "1,234.50"; -1234.5 / N0 → "-1,234".
std::string numberGrouped(float v, int n) {
  char raw[160];
  std::snprintf(raw, sizeof(raw), "%.*f", n, (double)v);
  std::string s(raw);
  bool neg = !s.empty() && s[0] == '-';
  if (neg) s.erase(s.begin());
  std::string::size_type dot = s.find('.');
  std::string intPart = (dot == std::string::npos) ? s : s.substr(0, dot);
  std::string frac = (dot == std::string::npos) ? std::string() : s.substr(dot);  // includes '.'
  // Insert ',' every 3 digits from the right of intPart.
  std::string grouped;
  int cnt = 0;
  for (std::string::size_type i = intPart.size(); i-- > 0;) {
    grouped.push_back(intPart[i]);
    if (++cnt % 3 == 0 && i != 0) grouped.push_back(',');
  }
  std::reverse(grouped.begin(), grouped.end());
  return (neg ? "-" : "") + grouped + frac;
}

// Render C# standard "E<n>" / "e<n>" scientific format: n fractional digits, exponent ALWAYS signed
// with at least 3 digits (C# pads the exponent to 3, e.g. 1.000E+006). %e gives at least 2 exponent
// digits with a sign → we pad to 3. `upper` selects 'E' vs 'e'.
std::string scientific(float v, int n, bool upper) {
  char raw[160];
  std::snprintf(raw, sizeof(raw), "%.*e", n, (double)v);  // e.g. "1.000e+06"
  std::string s(raw);
  std::string::size_type ep = s.find('e');
  if (ep == std::string::npos) return s;  // non-finite (handled upstream, defensive)
  std::string mant = s.substr(0, ep);
  char sign = s[ep + 1];                              // '+' or '-'
  std::string digits = s.substr(ep + 2);             // exponent magnitude digits
  while (digits.size() < 3) digits.insert(digits.begin(), '0');  // C# pads exponent to ≥3 digits
  return mant + (upper ? "E" : "e") + sign + digits;
}

// Parse a C# numeric format SPEC (the text after "{0:" up to "}") and render `v`. Returns true +
// writes `out` on a SUPPORTED spec; false = unrecognised (caller emits "Invalid Format").
bool tryNumericSpec(const std::string& spec, float v, std::string& out) {
  if (spec.empty()) { out = defaultToString(v); return true; }  // "{0:}" ≈ "{0}" (general)

  // --- Standard numeric format: a single letter + optional precision digits (F/f/N/n/E/e). ---
  if (spec.size() >= 1) {
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
        case 'F': case 'f':                       // fixed-point
          out = fixedPoint(v, hasN ? n : 2);      // C# default fixed precision = 2
          return true;
        case 'N': case 'n':                       // number (grouped)
          if (std::isnan(v) || std::isinf(v)) { out = defaultToString(v); return true; }
          out = numberGrouped(v, hasN ? n : 2);
          return true;
        case 'E': case 'e':                       // scientific
          if (std::isnan(v) || std::isinf(v)) { out = defaultToString(v); return true; }
          out = scientific(v, hasN ? n : 6, c == 'E');  // C# default scientific precision = 6
          return true;
        default: break;                            // C/P/X/G/R/D… → fall through to custom / invalid
      }
    }
  }

  // --- Custom numeric format: only the simple zero-pad form {0:[0].000…} (optional one leading '0'
  // integer placeholder, optional '.', then a run of '0' = fixed fractional digits). '#', ';',
  // grouping, scaling etc. are out of vocabulary → "Invalid Format". ---
  {
    std::string::size_type i = 0;
    // optional single integer placeholder '0'
    if (i < spec.size() && spec[i] == '0') ++i;
    int fracDigits = -1;  // -1 = no '.' seen
    if (i < spec.size() && spec[i] == '.') {
      ++i;
      fracDigits = 0;
      while (i < spec.size() && spec[i] == '0') { ++fracDigits; ++i; }
    }
    if (i == spec.size() && (fracDigits >= 0 || spec == "0")) {
      // pure zero-pad custom format: "0" → 0 digits; "0.000" → 3 digits; ".00" → 2 digits.
      int n = (fracDigits >= 0) ? fracDigits : 0;
      if (n > 99) return false;
      if (std::isnan(v) || std::isinf(v)) { out = defaultToString(v); return true; }
      out = fixedPoint(v, n);
      return true;
    }
  }
  return false;  // unrecognised spec → "Invalid Format"
}

// Apply a C# composite format (one {0[:spec]} placeholder + literal text before/after). string.Format
// keeps the surrounding literal text verbatim and substitutes the placeholder. Returns true + writes
// `out` on success; false = unrecognised (caller emits "Invalid Format", TiXL's FormatException branch).
bool tryComposite(const std::string& fmt, float v, std::string& out) {
  // Find the SINGLE placeholder "{0" … "}". (Multiple placeholders / escaped braces → unsupported.)
  std::string::size_type open = fmt.find('{');
  if (open == std::string::npos) return false;          // no placeholder → FormatException (no arg used)
  if (fmt.find('{', open + 1) != std::string::npos) return false;  // >1 '{' → out of vocabulary
  // Placeholder must start with index 0: "{0" then either '}' or ":spec}".
  if (open + 1 >= fmt.size() || fmt[open + 1] != '0') return false;
  std::string::size_type close = fmt.find('}', open);
  if (close == std::string::npos) return false;         // unbalanced → FormatException
  if (fmt.find('}', close + 1) != std::string::npos) return false;  // stray '}' → out of vocabulary

  std::string inner = fmt.substr(open + 1, close - open - 1);  // "0" or "0:spec"
  std::string spec;
  if (inner == "0") {
    spec = std::string();  // {0} → default ToString
  } else if (inner.size() >= 2 && inner[1] == ':') {
    spec = inner.substr(2);  // "0:F1" → "F1"
  } else {
    return false;  // "{0X…}" malformed alignment/etc → out of vocabulary
  }

  std::string rendered;
  if (spec.empty()) {
    rendered = defaultToString(v);  // {0}
  } else if (!tryNumericSpec(spec, v, rendered)) {
    return false;  // unrecognised spec → "Invalid Format"
  }
  out = fmt.substr(0, open) + rendered + fmt.substr(close + 1);  // literal-before + value + literal-after
  return true;
}

// FloatToString: Value (resolved Float param) + Format (String input) → host string.
void cookFloatToString(StringCookCtx& c) {
  if (!c.output) return;
  const float v = stringFloatParam(c.params, "Value", 0.0f);  // FloatToString.cs:18 Value.GetValue
  std::string fmt;
  if (c.inputStrings && !c.inputStrings->empty()) fmt = (*c.inputStrings)[0];  // Format (inputStrings[0])

  std::string result;
  if (fmt.empty()) {
    result = defaultToString(v);  // string.IsNullOrEmpty(s) ? v.ToString(InvariantCulture)
  } else if (!tryComposite(fmt, v, result)) {
    result = "Invalid Format";  // FormatException branch (faithful to the catch)
  }
  *c.output = result;

  // Test-only: corrupt the REAL output (drop the last char) so a golden's transport RED bites on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports: "Value"  = scalar Float input (read via resolved params, the value spine);
//          "Format" = String input (wire-OR-const; strDef "{0:0.000}" = FloatToString.t3:6 default →
//                     3 fixed fractional digits, e.g. v=0 → "0.000");
//          "Output" = the String output (host string currency).
static const StringOp _reg_floattostring{
    {"FloatToString", "FloatToString",
     {{"Output", "Output", "String", false},
      {"Value", "Value", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider},
      {"Format", "Format", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "{0:0.000}"}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookFloatToString};

}  // namespace sw
