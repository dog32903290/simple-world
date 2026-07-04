// StringToDateTime host-scalar op (host-scalar self-registration seam leaf — String input → host
// Float output, DateTime route B PRODUCER #2). TiXL authority:
// Operators/Lib/string/datetime/StringToDateTime.cs (verbatim below):
//
//   StringToDateTime.cs Update():
//     var dateString = DateString.GetValue(context);
//     if (DateTime.TryParse(dateString, out var dateTime)) {
//       Output.Value = dateTime;            // :20
//       _lastStatusError = null;
//     } else {
//       _lastStatusError = $"Failed to parse {dateString} into DateTime";   // :25 (IStatusProvider)
//     }
//   Ports: DateString = InputSlot<string>. Output = Slot<DateTime>.
//   StringToDateTime.t3: DateString DefaultValue "" (re-read & confirmed).
//
// EVAL-SIDE LAYOUT: CONSUMES a String, PRODUCES a single host Float (the route-B epoch) — the EXACT
// TryParse shape (host_scalar_ops_tryparse.cpp). Rides HostScalarCookCtx: inputStrings[0] = the
// resolved DateString (wire-OR-const), *output = the epoch (mirrored by the driver into
// Node::outCache so a downstream Float input — e.g. DateTimeToFloat.Value — reads it via the
// generalised evalFloat escape hatch).
//
// FLAT path: the generic cookFlatHostScalar branch gathers the String input + runs the cook below.
// RESIDENT (production) path: the generic cookHostScalarNodes loop SKIPS String-input host-scalar
// ops; StringToDateTime gets its dedicated branch in resident_host_scalar_cook.cpp (the TryParse
// pattern), calling the SHARED helper below so both rails parse byte-identically.
//
// FORKS (named):
//   - fork-datetime-epoch-as-float / fork-datetime-utc-not-local /
//     fork-datetime-tryparse-narrow-vocabulary: the family forks, all pinned in
//     runtime/datetime_host.h (the shared parse kernel this leaf calls).
//   - fork-stringtodatetime-parsefail-zero: TiXL on parse failure SKIPS the Output assignment (:23)
//     — the slot keeps its PREVIOUS value (stale-last-good) and the op reports a status warning
//     (IStatusProvider, :25). sw's host-scalar rail is stateless per cook (the driver hands a fresh
//     output slot every frame), so parse failure emits 0.0 (= epoch 1970-01-01, the route-B origin)
//     instead of a stale value. The status-warning channel (editor UI affordance) is dropped.
//   - fork-string-port-becomes-drivable: DateString is WIRE-OR-CONST (shared driver gather).
#include <string>

#include "runtime/datetime_host.h"            // tryParseDateTimeToEpoch (shared C# TryParse subset)
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug

namespace sw {

// The SHARED cook math — called by the flat cook below AND the resident dedicated branch
// (resident_host_scalar_cook.cpp), so both rails parse byte-identically (the tryParseFloat precedent).
// Parse-or-zero: fork-stringtodatetime-parsefail-zero (header).
float stringToDateTimeEpoch(const std::string& dateString) {
  double epoch = 0.0;
  if (!tryParseDateTimeToEpoch(dateString, epoch)) return 0.0f;
  return static_cast<float>(epoch);
}

namespace {

// StringToDateTime: Output = TryParse(DateString) ? epoch : 0. Mirrors StringToDateTime.cs Update().
void cookStringToDateTime(HostScalarCookCtx& c) {
  if (!c.output) return;
  const std::string in =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};
  *c.output = stringToDateTimeEpoch(in);
  // Test-only: corrupt the REAL output on the actual cook path (sentinel) so the golden's RED bites
  // here via downstream evalFloat, NOT by flipping the expected value. Off in production.
  if (hostScalarInjectBug()) *c.output = -999.0f;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (flat path); the resident
// dedicated branch is the one shared edit (the TryParse precedent).
//   Ports: "Output"     = the Float output (route-B epoch seconds; host scalar via outCache);
//          "DateString" = the String input (the text to parse, wire-OR-const; strDef "" per .t3).
// Output port FIRST (index 0) so outIdx 0 = Output (the host-scalar layout convention).
static const HostScalarOp _reg_stringtodatetime{
    {"StringToDateTime", "StringToDateTime",
     {{"Output", "Output", "Float", false},
      {"DateString", "DateString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""}},
     /*evaluate=*/nullptr,  // host scalar comes from the cook driver, not the value-eval evaluate fn
     "string.datetime"},
    cookStringToDateTime};

}  // namespace sw
