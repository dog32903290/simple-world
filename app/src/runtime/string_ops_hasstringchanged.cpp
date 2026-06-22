// HasStringChanged string op — the FIRST cross-frame STATE consumer on the STRING rail (the string twin
// of KeepColors on the colorlist rail). TiXL authority:
//   external/tixl/Operators/Lib/string/logic/HasStringChanged.cs  (verbatim Update() below, cited inline):
//
//     private string _lastString;                                   // PERSISTENT per-node field
//     private void Update(EvaluationContext context) {
//       var newString = Value.GetValue(context);                    // the String input
//       var hasChanged = newString != _lastString;                  // delta vs last frame
//       HasChanged.Value = hasChanged;                              // bool output
//       _lastString = newString;                                   // store for next frame
//       HasChanged.DirtyFlag.Trigger = hasChanged ? Animated : None; // editor dirty hint (sw: no-op)
//     }
//     Output: HasChanged = Slot<bool>;  Input: Value = InputSlot<string>.
//
// THE SEAM THIS LEAF PROVES: `_lastString` is a PER-NODE field that SURVIVES across frames. Every other
// String op is single-frame (a pure function of this frame's inputs). So HasStringChanged needed the new
// cross-frame STATE slot on the cook ctx: StringCookCtx::state (the string analog of ColorListCookCtx::
// state — KeepColors's `_list`). The driver owns + threads it — flat: Impl::stringState[flatKey(id)]
// (point_graph.cpp); resident (production): s_stringState[path] (cook_host_values.cpp, the mirror of
// s_colorListState). This leaf compares the current input against *state, emits the bool delta, then
// stores the current string back into *state.
//
// EVAL-SIDE LAYOUT: like StringLength, HasStringChanged CONSUMES a String but PRODUCES a host scalar
// (a bool, not a String) — it is NOT a String PRODUCER (no String output port). BUT unlike StringLength
// (stateless → cooked by the host-scalar pass), HasStringChanged is STATEFUL: the host-scalar pass has
// no cross-frame state store, so it must ride the STRING-producer cook pass (cookStringNode flat /
// cookStringNodes resident) — the ONLY pass that threads s_stringState. cookStringNodes special-cases a
// scalar-only StringOp that is NOT registered in the host-scalar set (= a stateful one) and cooks it here
// with its per-path state slot, fanning the bool→Float onto extOut[0] (resident_string_cook.cpp).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL's HasChanged is Slot<bool>; sw has no Bool port currency, so
//     the bool dissolves bool→Float (1.0/0.0 — Cut32 convention, same as FilePathParts.FileExists /
//     PickStringPart.TotalCount). Output rides scalarOutputs[0] → extOut[0] (resident) / outCache[0] (flat).
//   - fork-laststring-primed-flag: TiXL's `_lastString` field default is C# null; sw uses a `primed`
//     flag (StringState.primed, false by default) to reproduce null semantics: frame-0 is ALWAYS changed
//     regardless of input value, matching TiXL's "null != anything" behaviour (including ""→changed on
//     frame-0). After frame-0 the flag is set and comparisons use lastString normally. Old init "" was
//     wrong: TiXL frame-0 "" → changed (null!=""); old sw frame-0 "" → unchanged (""=="" = parity gap).
//   - fork-string-port-becomes-drivable: Value is WIRE-OR-CONST (wired → upstream cooked string; unwired →
//     strDef const). The shared driver gather owns this fork (inputStrings[0]).
#include <string>

#include "runtime/graph.h"                // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"   // StringOp / StringCookCtx / StringState / stringInjectBug

namespace sw {

int runHasStringChangedSelfTest(bool injectBug);  // hasstringchanged_golden.cpp (declared for the registrar)

namespace {

// HasStringChanged: compare this frame's String input against the PERSISTENT *state->lastString, emit the
// bool delta (dissolved to Float), then store the current string for next frame. Body is literally
// HasStringChanged.cs:Update() (cited file-top).
void cookHasStringChanged(StringCookCtx& c) {
  // cs: newString = Value.GetValue(context). The single String input (wire-OR-const) → inputStrings[0].
  const std::string current =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};

  // cs: hasChanged = newString != _lastString. TiXL's _lastString field default is C# null, so frame-0
  // ALWAYS reports changed regardless of input (null != anything). sw uses a `primed` flag to reproduce
  // this: !primed → first frame, no prior value → always changed (mirrors C# null != value).
  // With NO state slot (a hand-built ctx / single-frame caller), behave as unprimed every call.
  const bool changed = !c.state || !c.state->primed || (current != c.state->lastString);

  // cs: HasChanged.Value = hasChanged (bool dissolved → Float). Port 0 is the ONLY output → scalarOutputs[0]
  // → resident extOut[0] / flat outCache[0]. (No String output: *output stays untouched.)
  if (c.scalarOutputs) (*c.scalarOutputs)[0] = changed ? 1.0f : 0.0f;

  // cs: _lastString = newString. Store the current string AND mark as primed for the NEXT frame's
  // comparison. RED tooth (golden): stringInjectBug() SKIPS this store on the REAL accumulator → the next
  // frame compares against the STALE (un-updated) string, so an unchanged frame mis-reports "changed" →
  // the golden's frame-1 assertion bites on the actual cross-frame state path (NOT by flipping the
  // expected value). Also skips primed=true so a frame-0 "" leg stays FAILED under injectBug.
  if (c.state && !stringInjectBug()) {
    c.state->primed = true;
    c.state->lastString = current;
  }
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Output port [0] "HasChanged" = Float (bool dissolved → scalarOutputs[0] → extOut[0] / outCache[0]).
//   Input  port [1] "Value"      = String (wire-OR-const; strDef "" → empty → frame-0 baseline).
// PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity,
//                       multiInput, strDef}.
static const StringOp _reg_hasstringchanged{
    {"HasStringChanged", "HasStringChanged",
     {{"HasChanged", "HasChanged", "Float", false},
      {"Value", "Value", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     /*evaluate=*/nullptr},  // scalar comes from the cook driver (cross-frame state), not value-eval
    cookHasStringChanged};

}  // namespace sw
