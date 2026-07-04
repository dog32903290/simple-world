// ValueToRate host-scalar op (host-scalar self-registration seam leaf — String input → host Float
// output, the normalized-value→musical-rate quantizer). TiXL authority:
// external/tixl/Operators/Lib/numbers/float/logic/ValueToRate.cs (verbatim below):
//
//   ValueToRate.cs Update() (cs:17-33):
//     UpdateRatios(...)                                  // parse Rates: one float per '\n' line;
//                                                        //   unparseable lines are SKIPPED (cs:45-55,
//                                                        //   float.TryParse InvariantCulture else Log.Warning)
//     var stepCount = _ratios.Count;                     // cs:23
//     var f = Value.GetValue(context).Clamp(0, 0.99f);   // cs:24
//     var result = stepCount switch                      // cs:26-30
//                      { 0 => 1,
//                        _ => _ratios[(int)((stepCount - 1) * f + 0.5f)] };
//     Result.Value = result;                             // cs:32
//
//   Ports: Value = InputSlot<float> (cs:62-63), Rates = InputSlot<string> (cs:65-66).
//   Output: Result = Slot<float> (cs:9-10).
//   ValueToRate.t3 DefaultValues: Value = 0.5, Rates = "0\n0.0625\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32"
//   (11 lines, all parseable → default eval: f=0.5, idx=(int)(10*0.5+0.5)=5 → _ratios[5] = 1).
//
// EVAL-SIDE LAYOUT: ValueToRate CONSUMES a String and PRODUCES a single host Float — the EXACT shape of
// the host-scalar rail (TryParse precedent, host_scalar_ops_tryparse.cpp). NOT a String producer, NOT a
// pure value op (its Rates input is a host string evalFloat cannot see). It rides HostScalarCookCtx:
// inputStrings[0] = the resolved Rates string (wire-OR-const), params["Value"] = the resolved Float,
// *output = the picked rate (mirrored by the driver into Node::outCache / extOut[0] so a downstream
// Float input reads it via the generalised evalFloat escape hatch).
//
// FLAT path: the generic cookFlatHostScalar branch (point_graph_hostscalar_cook.cpp) gathers the String
// input via the shared gatherStringInputs + runs cookValueToRate below — no driver edit needed.
// RESIDENT (production) path: the generic cookHostScalarNodes loop SKIPS String-input host-scalar ops;
// ValueToRate gets its own dedicated branch there (resident_host_scalar_cook.cpp), the TryParse twin,
// calling the SHARED valueToRateResult below so both rails compute byte-identically.
//
// FORKS (named):
//   - fork-valuetorate-parse-per-cook: TiXL caches _ratios and re-parses only when Rates.IsDirty AND the
//     string changed (cs:37). We re-parse every cook — the parse is a pure function of the Rates string,
//     so the OUTPUT is byte-identical; only a cache is dropped (stateless > stateful for a pure fn).
//   - fork-valuetorate-csharp-parse-semantics: per-line float.TryParse mirrored with the SHARED
//     tryParseFloat (host_scalar_ops_tryparse.cpp — trims whitespace incl. '\r', full-token consume,
//     rejects NaN/Inf), so "0.5\r" (CRLF text) parses and "abc" is skipped exactly like C#.
//   - fork-valuetorate-host-scalar-via-outcache: the scalar rides Node::outCache (the bridge) in
//     PARALLEL with the legacy floatListBuf transport (TryParse precedent).
//   - fork-string-port-becomes-drivable: the Rates input is WIRE-OR-CONST (wired → upstream cooked
//     string; unwired → strDef const = the .t3 default table). The shared driver gather owns this fork.
#include <string>
#include <vector>

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

// Shared C#-parity float parse (host_scalar_ops_tryparse.cpp — the one parse truth point).
bool tryParseFloat(const std::string& s, float& out);

// ValueToRate.cs Update() as a pure function of (Rates string, Value). SHARED by the flat cook below
// AND the resident dedicated branch (resident_host_scalar_cook.cpp) so both rails compute identically.
//   • Split Rates on '\n' (cs:45); float.TryParse each line, skip unparseable (cs:47-54).
//   • f = Value.Clamp(0, 0.99f) (cs:24).
//   • stepCount==0 → 1 ; else _ratios[(int)((stepCount-1)*f + 0.5f)] (cs:26-30).
float valueToRateResult(const std::string& rates, float value) {
  std::vector<float> ratios;
  size_t start = 0;
  while (start <= rates.size()) {
    size_t nl = rates.find('\n', start);
    const std::string line =
        (nl == std::string::npos) ? rates.substr(start) : rates.substr(start, nl - start);
    float f;
    if (tryParseFloat(line, f)) ratios.push_back(f);  // unparseable line → skipped (cs:52-54)
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  const int stepCount = (int)ratios.size();
  if (stepCount == 0) return 1.0f;  // cs:28 `0 => 1`
  float f = value;                  // f = Value.Clamp(0, 0.99f) (cs:24)
  if (f < 0.0f) f = 0.0f;
  else if (f > 0.99f) f = 0.99f;
  const int idx = (int)((stepCount - 1) * f + 0.5f);  // cs:29 — C# (int) cast truncates toward zero
  return ratios[idx];
}

namespace {

// ValueToRate: result = pickRate(inputStrings[0], params["Value"]). Mirrors ValueToRate.cs Update().
void cookValueToRate(HostScalarCookCtx& c) {
  if (!c.output) return;
  const std::string rates =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};
  const float value = hostScalarParam(c.params, "Value", 0.5f);
  *c.output = valueToRateResult(rates, value);
  // Test-only: corrupt the REAL output on the actual cook path (sentinel) so the golden's RED bites
  // via downstream evalFloat, NOT by flipping the expected value. Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point on
// the flat path; the resident dedicated branch is the one shared edit, like TryParse).
//   Ports: "Result" = the Float output (the picked rate; host scalar via outCache + floatListBuf);
//          "Value"  = the Float input (.t3 default 0.5; clamped [0,0.99] in the cook);
//          "Rates"  = the String input (one float per line; .t3 default = the musical-rate table).
// Output port FIRST (index 0) so outIdx 0 = Result, matching the host-scalar layout.
static const HostScalarOp _reg_valuetorate{
    {"ValueToRate", "ValueToRate",
     {{"Result", "Result", "Float", false},
      {"Value", "Value", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Slider},
      {"Rates", "Rates", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "0\n0.0625\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32"}},
     /*evaluate=*/nullptr,  // host scalar comes from the cook driver, not the value-eval evaluate fn
     "numbers.float.logic"},
    cookValueToRate};

}  // namespace sw
