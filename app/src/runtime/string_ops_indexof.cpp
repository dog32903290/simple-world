// IndexOf string op (host-scalar leaf — String×String → Int output, int dissolved to Float).
// TiXL authority: external/tixl/Operators/Lib/string/search/IndexOf.cs
//
//   IndexOf.cs Update():
//     string searchPattern = SearchPattern.GetValue(context);
//     string originalString = OriginalString.GetValue(context);
//     if (string.IsNullOrEmpty(searchPattern) || string.IsNullOrEmpty(originalString))
//     {
//       Index.Value = -1;
//       return;
//     }
//     Index.Value = originalString.IndexOf(searchPattern);  // first occurrence, -1 if not found
//
//   Ports:
//     OriginalString  = InputSlot<string>  (the string to search IN)
//     SearchPattern   = InputSlot<string>  (the substring to search FOR)
//   Output: Index = Slot<int> (0-based index of first occurrence, or -1 if not found / either empty)
//
// EVAL-SIDE LAYOUT: IndexOf consumes TWO Strings and produces ONE host scalar (Int → Float via
// fork-int-bool-dissolve-to-float, Cut32 convention). It is NOT a String producer (no output String
// port), so it has NO StringCookFn. Its real flat cook runs via the generic cookHostScalar branch in
// point_graph.cpp — that branch already gathers String inputs via gatherStringInputs (wire-OR-const,
// spec port order) into HostScalarCookCtx::inputStrings BEFORE the guard that skips String-input ops
// was introduced; this op uses the FULL HostScalarOp path (unlike StringLength, which predates the
// HostScalarOp registry and therefore keeps a dedicated driver branch + a bridge-only registration).
// IndexOf registers a genuine HostScalarCookFn so the generic loop cooks it.
//
// RESIDENT PATH: resident_host_scalar_cook.cpp guards against String-input ops AFTER a dedicated
// StringLength branch. IndexOf gets its OWN dedicated branch (before the guard) that gathers its two
// String inputs via cookResidentString (the String-wire rail) — same pattern as StringLength but
// reading TWO String ports (OriginalString + SearchPattern), computing .find(), and writing the result
// (or -1) onto extOut[0]. The guard below the branch shields any future String-consumer that does NOT
// yet have a resident gather.
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's Index is Slot<int>; sw has no Int port type, so the
//     index dissolves int→Float (Cut32 convention, same as StringLength.Length and every Int-returning
//     op already ported). The golden asserts exact float equality (6.0f / -1.0f) since the int values
//     round-trip perfectly through float32.
//   - fork-indexof-host-scalar-via-outcache: same transport as StringLength (outCache[0] + floatListBuf
//     1-elem); no additional fork — the HostScalarOp registrar owns both channels.
//   - fork-string-port-becomes-drivable (inherited): both String inputs are WIRE-OR-CONST — wired reads
//     the upstream cooked string; unwired falls back to strDef const (""). Same fork as StringLength.
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // HostScalarOp / HostScalarCookCtx / hostScalarInjectBug
#include "runtime/string_op_registry.h"       // stringInjectBug (for LEG 25 golden teeth)

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for linkage; defined there)

namespace {

// IndexOf cook fn. HostScalarCookCtx::inputStrings contains the resolved String inputs in spec port
// order — port 1 = OriginalString, port 2 = SearchPattern → inputStrings[0] = original,
// inputStrings[1] = pattern. The gather fills BOTH entries because both String input ports are
// single-input (not MultiInput), and an unwired port contributes its strDef const (""), so
// inputStrings always has exactly 2 entries when the op is well-formed.
//
// TiXL semantics (IndexOf.cs):
//   if either is null-or-empty → Index = -1
//   else Index = originalString.IndexOf(searchPattern)  // C# string.IndexOf = first occurrence
//
// C++ std::string::find returns std::string::npos on miss; we map npos → -1 (faithful to TiXL).
void cookIndexOf(HostScalarCookCtx& c) {
  const auto& ins = *c.inputStrings;
  const std::string& original = (ins.size() > 0) ? ins[0] : std::string{};
  const std::string& pattern  = (ins.size() > 1) ? ins[1] : std::string{};

  float idx;
  if (original.empty() || pattern.empty()) {
    idx = -1.0f;  // TiXL: IsNullOrEmpty either → -1
  } else {
    auto pos = original.find(pattern);
    idx = (pos == std::string::npos) ? -1.0f : (float)(int)pos;
  }

  // Test-only injection seam: corrupt the REAL output so the golden's RED case fires on the actual
  // cook path (NOT by flipping the expected value). Off in production.
  if (hostScalarInjectBug()) idx = -999.0f;  // sentinel that diverges from any valid TiXL result

  *c.output = idx;
}

}  // namespace

// Self-registration. File-scope static HostScalarOp — independent leaf .cpp (no shared edit point).
// Feeds hostScalarSpecSink() (Add menu + findSpec), hostScalarCookFns() (cookIndexOf), and
// hostScalarTypes() (isHostScalarOp predicate for evalFloat / evalResidentFloat escape hatch).
//
// Port layout (spec port order, matching TiXL slot order for HostScalarCookCtx gather):
//   Port 0: "Index"          — Float output   (int dissolved to Float; rides outCache[0])
//   Port 1: "OriginalString" — String input    (the string to search IN; wire-OR-const, strDef "")
//   Port 2: "SearchPattern"  — String input    (the substring to search FOR; wire-OR-const, strDef "")
//
// PortSpec positional fields for a String input: {id, name, dataType, isInput, def, minV, maxV,
// widget, labels, pinless, vecArity, multiInput, strDef}. strDef "" = empty string default
// (TiXL InputSlot<string> default is empty → IsNullOrEmpty → Index -1).
static const HostScalarOp _reg_indexof{
    {"IndexOf", "IndexOf",
     {{"Index", "Index", "Float", false},
      {"OriginalString", "OriginalString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, ""},
      {"SearchPattern", "SearchPattern", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, ""}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver; no pure-float evaluate
    cookIndexOf};

}  // namespace sw
