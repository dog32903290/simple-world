// StringLength string op (string self-registration seam leaf — the FIRST cross-rail consumer:
// String input → host scalar output). TiXL authority: Operators/Lib/string/list/StringLength.cs:16:
//
//   StringLength.cs Update():
//     Length.Value = InputString.GetValue(context).Length;
//
//   Ports: InputString = InputSlot<string> (the ONE string to measure).
//   Output: Length = Slot<int> (the character count).
//
// EVAL-SIDE LAYOUT: StringLength is the ONLY one of this batch's three leaves that crosses rails —
// it CONSUMES a String but PRODUCES a host scalar (a number), not a String. So it is NOT a String
// PRODUCER: its StringCookFn is a never-invoked STUB (it never appears as an upstream String source).
// Its REAL cook is the FLAT DRIVER's dedicated cookStringLength branch (point_graph.cpp:757) — NOT
// the value-eval path and NOT the generic cookStringNode producer path. That branch resolves its
// String input via the shared gather, takes .size(), and stores the result as a 1-element host
// FloatList (Impl::floatListBuf — the only existing host-scalar channel; readback via
// debugCookedFloatList). This leaf therefore carries ONLY the NodeSpec (the port shape + Add-menu
// entry) plus a stub cook fn, registered through the StringOp sink. The driver never dispatches the
// stub for StringLength (the terminal dispatch checks "StringLength" FIRST → cookStringLength, and
// StringLength is never wired as a String input source).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's Length is Slot<int>; sw has no Int port type, so the
//     count dissolves int→Float (Cut32 convention, same as every Int-returning op already ported).
//   - fork-stringlength-host-scalar-via-floatlist: the host scalar rides Impl::floatListBuf as a
//     1-element list (the only host-scalar transport). Feeding it into a downstream Float INPUT port
//     is the separate list-routing seam (FloatList→Float bridge, SEAM_COMPLETION_PLAN §2 stage 1),
//     deferred — so StringLength's value is transported + readback-provable now, not yet rail-bridged.
//   - fork-string-port-becomes-drivable: StringLength's String input is WIRE-OR-CONST (wired →
//     upstream cooked string; unwired → strDef const). The shared driver gather owns this fork.
#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/host_scalar_op_registry.h"  // registerHostScalarType — StringLength.Length now rides outCache (the bridge)
#include "runtime/string_op_registry.h"       // StringOp / StringCookCtx

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Stub cook fn: StringLength is NOT a String producer (its real cook is the driver's cookStringLength
// branch, which writes a host scalar to floatListBuf — StringCookCtx has no scalar channel). The
// driver never dispatches a String-producer cook for "StringLength" (terminal dispatch checks it
// first; it is never an upstream String source), so this stub is unreachable in practice. Kept as a
// defined fn only to satisfy the StringOp registrar (NodeSpec carrier). Writes empty (harmless).
void cookStringLengthStub(StringCookCtx& c) {
  if (c.output) c.output->clear();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
// Feeds stringSpecSink() (Add menu + findSpec) + stringCookFns() (the stub) during pre-main init.
//   Ports: "InputString" = the String input (the new wire-OR-const String port);
//          "Length"       = the Float output (int dissolved to Float; host scalar via floatListBuf).
// PortSpec positional fields for a String input: {id, name, dataType, isInput, def, minV, maxV,
// widget, labels, pinless, vecArity, multiInput, strDef}. strDef "" = empty string default
// (TiXL InputSlot<string> default is empty → Length 0).
static const StringOp _reg_stringlength{
    {"StringLength", "StringLength",
     {{"Length", "Length", "Float", false},
      {"InputString", "InputString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""}},
     /*evaluate=*/nullptr},  // host scalar comes from the cook driver, not the value-eval evaluate fn
    cookStringLengthStub};

// BRIDGE registration (list-routing seam): StringLength's NodeSpec + cook stay on the String rail
// (its String-input gather predates the host-scalar registry), but its Float "Length" output now
// rides Node::outCache (cookStringLength writes it) so a downstream Float INPUT port can read it via
// evalFloat's generalised stateful escape hatch. Register the TYPE NAME ONLY into the host-scalar set
// so isHostScalarOp("StringLength") is true — WITHOUT moving its spec/cook (no node_registry churn).
// fork-evalfloat-stateful-generalized (the eval-side half) + fork-floatlist-scalar-via-outcache.
struct StringLengthBridgeReg {
  StringLengthBridgeReg() { registerHostScalarType("StringLength"); }
};
static const StringLengthBridgeReg _reg_stringlength_bridge;

}  // namespace sw
