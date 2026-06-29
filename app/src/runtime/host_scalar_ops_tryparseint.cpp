// TryParseInt host-scalar op (host-scalar self-registration seam leaf — String input → host Float
// output, the integer parse-or-default bridge). TiXL authority:
// external/tixl/Operators/Lib/numbers/int/process/TryParseInt.cs (verbatim below):
//
//   TryParseInt.cs Update():
//     if (int.TryParse(String.GetValue(context), result: out var result))
//         Result.Value = result;
//     else
//         Result.Value = Default.GetValue(context);
//
//   Ports: String  = InputSlot<string> (the text to parse).
//          Default = InputSlot<int>    (the fallback when parsing fails).
//   Output: Result = Slot<int>         (the parsed integer, or Default).
//
// EVAL-SIDE LAYOUT: the int twin of TryParse — CONSUMES a String, PRODUCES a single host scalar. Rides
// HostScalarCookCtx (inputStrings[0] + params["Default"] + *output). int dissolves to Float on sw's value
// rail (Cut32 convention — no Int port type), so Result is a "Float" output carrying an integral value.
//
// FLAT path: the generic cookFlatHostScalar branch gathers the String + runs cookTryParseInt (no driver
// edit). RESIDENT (production) path: its own dedicated branch in cookHostScalarNodes (the generic loop
// skips String-input host-scalar ops — see resident_host_scalar_cook.cpp, the IndexOf/TryParse pattern).
//
// FORKS (named):
//   - fork-tryparseint-csharp-integer-semantics: C# int.TryParse (NumberStyles.Integer = leading/trailing
//     whitespace + leading sign, NO decimal/exponent/thousands) parses ONLY a whole integer token. "1.5",
//     "3e2", "0x10", "12abc" all FAIL → Default. Range: a token outside int32 [-2147483648, 2147483647]
//     overflows → C# returns false → Default. We mirror with std::strtol + a full-token consume check +
//     the int32 range guard.
//   - fork-int-dissolve-to-float: TiXL's Result is Slot<int>; sw has no Int port — the integer dissolves
//     int→Float (host scalar carries the exact integral value, e.g. 42.0).
//   - fork-tryparseint-host-scalar-via-outcache / fork-string-port-becomes-drivable: same as TryParse.
#include <cerrno>   // errno
#include <climits>  // INT_MIN / INT_MAX
#include <cstdlib>  // std::strtol
#include <string>

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

// Parse `s` as an int32 exactly like C# int.TryParse (NumberStyles.Integer: whitespace + sign + digits;
// NO decimal/exponent/thousands). Returns true + writes `out` on success; false on failure. Shared by the
// flat cook here AND the resident branch so both rails parse byte-identically.
bool tryParseInt32(const std::string& s, int& out) {
  // Trim ASCII whitespace (C# TryParse allows leading/trailing whitespace).
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r' || s[b] == '\f' ||
                   s[b] == '\v'))
    ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r' ||
                   s[e - 1] == '\f' || s[e - 1] == '\v'))
    --e;
  if (b == e) return false;  // empty / all-whitespace → false → Default
  const std::string trimmed = s.substr(b, e - b);
  const char* cstr = trimmed.c_str();
  char* end = nullptr;
  errno = 0;
  const long v = std::strtol(cstr, &end, 10);  // base 10 only (no "0x"; C# Integer style rejects hex)
  if (end != cstr + trimmed.size()) return false;     // WHOLE token must parse ("1.5"/"12abc" fail)
  if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return false;  // out of int32 → C# false → Default
  out = (int)v;
  return true;
}

namespace {

// TryParseInt: result = int.TryParse(inputStrings[0]) ? parsed : Default. Mirrors TryParseInt.cs Update().
void cookTryParseInt(HostScalarCookCtx& c) {
  if (!c.output) return;
  const std::string in = (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};
  // Default is an int in TiXL; on the dissolved Float rail it rides as a Float param (lround to clamp it
  // back to an integral fallback — the param spine is Float, the value is conceptually an int).
  const float defF = hostScalarParam(c.params, "Default", 0.0f);
  int parsed = (int)(defF >= 0.0f ? (defF + 0.5f) : (defF - 0.5f));  // round-to-nearest int fallback
  tryParseInt32(in, parsed);  // leaves `parsed` == default on failure
  *c.output = (float)parsed;  // int→Float dissolve
  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites here. Off in prod.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp.
//   Ports: "Result" = Float output (parsed int dissolved to Float); "String" = the text input (wire-OR-
//          const); "Default" = the Float fallback (integral value on the dissolved rail).
static const HostScalarOp _reg_tryparseint{
    {"TryParseInt", "TryParseInt",
     {{"Result", "Result", "Float", false},
      {"String", "String", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"Default", "Default", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookTryParseInt};

}  // namespace sw
