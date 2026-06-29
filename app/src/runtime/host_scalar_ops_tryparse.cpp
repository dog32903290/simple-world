// TryParse host-scalar op (host-scalar self-registration seam leaf — String input → host Float output,
// the parse-or-default bridge). TiXL authority:
// external/tixl/Operators/Lib/numbers/float/logic/TryParse.cs (verbatim below):
//
//   TryParse.cs Update():
//     if (float.TryParse(String.GetValue(context), result: out var result))
//         Result.Value = result;
//     else
//         Result.Value = Default.GetValue(context);
//
//   Ports: String  = InputSlot<string> (the text to parse).
//          Default = InputSlot<float>  (the fallback when parsing fails).
//   Output: Result = Slot<float>       (the parsed value, or Default).
//
// EVAL-SIDE LAYOUT: TryParse CONSUMES a String and PRODUCES a single host Float — the EXACT shape of the
// host-scalar rail (StringLength / IndexOf cross-rail consumers). So it is NOT a String PRODUCER (no
// port-0 String, no String wire follows it) and NOT a pure value op (its INPUT is a host string that
// evalFloat — a pure float recursion — cannot see). It rides HostScalarCookCtx: inputStrings[0] = the
// resolved upstream String (wire-OR-const), params["Default"] = the resolved Float fallback, *output =
// the scalar (mirrored by the driver into Node::outCache so a downstream Float input reads it via the
// generalised evalFloat escape hatch).
//
// FLAT path: the generic cookFlatHostScalar branch (point_graph_hostscalar_cook.cpp) gathers the String
// input via the shared gatherStringInputs + runs cookTryParse below — no driver edit needed.
// RESIDENT (production) path: the generic cookHostScalarNodes loop SKIPS String-input host-scalar ops
// (the resident String-wire gather lives in a dedicated branch, like IndexOf). TryParse gets its own
// dedicated branch there (resident_host_scalar_cook.cpp) — the resident twin of this cook.
//
// FORKS (named):
//   - fork-tryparse-csharp-parse-semantics: C# float.TryParse with the invariant/current culture accepts
//     a leading/trailing-whitespace-free decimal ("3.14", "-2", "1e3"); we mirror with std::strtof + a
//     full-token consume check (the WHOLE trimmed string must parse, else fall to Default — "12abc" fails
//     in C# too). Hex / thousands separators are NOT accepted (C# NumberStyles.Float default excludes them).
//   - fork-tryparse-host-scalar-via-outcache: the scalar rides Node::outCache (the bridge) in PARALLEL
//     with the legacy floatListBuf transport — evalFloat reaches outCache, not floatListBuf.
//   - fork-string-port-becomes-drivable: the String input is WIRE-OR-CONST (wired → upstream cooked
//     string; unwired → strDef const ""). The shared driver gather owns this fork.
#include <cerrno>   // errno
#include <cmath>    // std::isfinite
#include <cstdlib>  // std::strtof
#include <string>

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

// Parse `s` as a float exactly like C# float.TryParse (NumberStyles.Float | AllowThousands? NO — default
// is Float, which is leading/trailing whitespace + sign + decimal + exponent, NO thousands). Returns true
// + writes `out` on success; false (leaves `out` untouched) on failure. Shared by the flat cook here AND
// the resident branch (resident_host_scalar_cook.cpp) so both rails parse byte-identically.
bool tryParseFloat(const std::string& s, float& out) {
  // Trim ASCII whitespace (C# TryParse allows leading/trailing whitespace).
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r' || s[b] == '\f' ||
                   s[b] == '\v'))
    ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r' ||
                   s[e - 1] == '\f' || s[e - 1] == '\v'))
    --e;
  if (b == e) return false;  // empty / all-whitespace → C# returns false → Default
  const std::string trimmed = s.substr(b, e - b);
  const char* cstr = trimmed.c_str();
  char* end = nullptr;
  errno = 0;
  const float v = std::strtof(cstr, &end);
  // The WHOLE trimmed token must be consumed (C# rejects "12abc"); reject NaN/Inf (C# TryParse default
  // NumberStyles.Float does NOT accept "NaN"/"Infinity" without AllowSpecialValues).
  if (end != cstr + trimmed.size()) return false;
  if (!std::isfinite(v)) return false;
  out = v;
  return true;
}

namespace {

// TryParse: result = float.TryParse(inputStrings[0]) ? parsed : Default. Mirrors TryParse.cs Update().
void cookTryParse(HostScalarCookCtx& c) {
  if (!c.output) return;
  const std::string in = (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};
  const float def = hostScalarParam(c.params, "Default", 0.0f);
  float parsed = def;
  tryParseFloat(in, parsed);  // leaves `parsed` == def on failure
  *c.output = parsed;
  // Test-only: corrupt the REAL output on the actual cook path (sentinel) so the golden's RED bites here
  // via downstream evalFloat, NOT by flipping the expected value. Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point on the
// flat path; the resident dedicated branch is the one shared edit, like IndexOf).
//   Ports: "Result" = the Float output (parsed-or-default; host scalar via outCache + floatListBuf);
//          "String" = the String input (the text to parse, wire-OR-const);
//          "Default" = the Float fallback param.
// Output port FIRST (index 0) so outIdx 0 = Result, matching the host-scalar layout (evalFloat's outIdx
// = output port index). The String input carries the {pinless, vecArity, multiInput, strDef} tail.
static const HostScalarOp _reg_tryparse{
    {"TryParse", "TryParse",
     {{"Result", "Result", "Float", false},
      {"String", "String", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"Default", "Default", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider, {}, true}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookTryParse};

}  // namespace sw
