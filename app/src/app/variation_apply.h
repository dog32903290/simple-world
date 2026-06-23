// app/variation_apply — Lane L1 (Variation / Snapshot): the DOCUMENT-OVERRIDE bridge. This is the
// step that makes the snapshot pool / crossfader actually DRIVE the live project: it turns a blend
// toward a target snapshot into UNDO-ABLE input overrides on the running graph (SymbolLibrary), exactly
// the way TiXL routes a blend through a MacroCommand of ChangeInputValueCommands.
//
// ZONE: app (product behavior — edits g_lib's editing model through the command stack). app -> runtime
// (reads compound_graph effectiveInput / SlotDef.def, writes via SetOverrideCommand). No UI, no
// platform, no cook-core. One responsibility: build the "blend towards snapshot" MacroCommand.
//
// ── What this CLOSES (fork-crossfader-direct-apply) ─────────────────────────────────────────────────
// The runtime crossfader (variation_crossfader.h) blends into a plain LiveParams SIDE MAP — the right
// numbers, but they never reach the graph and are not undo-able (the named fork). THIS layer is the
// graph-facing half: it computes the SAME Lerp(current, target, weight) per input and writes each as a
// SymbolChild::overrides[slotId] = float through a MacroCommand of SetOverrideCommands pushed on
// g_commands. So the blend becomes a single undo-able document edit that the resident projection
// rebuilds from — the fork is closed.
//
// ── TiXL ground-truth (SymbolVariationPool.cs:603-631 TryCreateBlendTowardsVariationCommand) ─────────
//   foreach child in composition.Children:
//     if variation has no ParameterSet for this child -> skip the child
//     foreach inputSlot in child.Inputs:
//       if no BlendMethod for the type -> skip
//       if the variation TRACKS this input (parameter present):
//          mixed = Lerp(input.Value, parameter, blend)            // current -> stored snapshot value
//       else if (!input.IsDefault):                               // NIT 1 — untracked but overridden
//          mixed = Lerp(input.Value, input.DefaultValue, blend)   // current -> the input's DefaultValue
//       // else: untracked AND already at default -> no command (skip)
//       commands.Add(ChangeInputValueCommand(symbol, childId, input, mixed))
//   MacroCommand("Blend towards snapshot", commands)
//
// sw vocabulary mapping (faithful, named where it forks):
//   child.SymbolChildId        -> int childId in the composition Symbol
//   inputSlot.Id               -> std::string slotId
//   inputSlot.Input.Value      -> effectiveInput(lib, child, slotId)        (override else def)
//   inputSlot.Input.DefaultValue-> SlotDef.def on the referenced Symbol's inputDefs
//   inputSlot.Input.IsDefault  -> the child has NO override for slotId (sparse-override model)
//   ChangeInputValueCommand    -> SetOverrideCommand (already the sw input-override command)
//   ValueUtils.BlendMethods    -> only Float slots blend (sw value rail is float-per-slot; String/non-
//                                 float slots have no BlendMethod, so they are skipped — same as TiXL
//                                 skipping a type with no BlendMethod). Vec inputs are already split into
//                                 per-component Float slots in sw, so each component blends as its own
//                                 Float slot — no separate vec BlendMethod is needed (FORK-NAME:
//                                 fork-vec-as-float-slots, faithful to sw's float-per-slot value model).
#pragma once
#include <map>
#include <memory>
#include <string>

#include "app/command.h"
#include "runtime/compound_graph.h"

namespace sw {

// A target snapshot expressed in the DOCUMENT'S OWN vocabulary: per (childId, slotId) the stored
// float value (= TiXL Variation.ParameterSetsForChildIds[childId][inputId], where every sw input value
// is a float slot). This is what a snapshot of sw's running graph captures. The crossfader's tagged
// VariationValue (runtime side-map type) flattens INTO this for the document path: a vec3 snapshot
// becomes three (childId, "slot.x/.y/.z") float entries — one per Float slot the document actually has.
struct DocVariation {
  // parameterSets[childId][slotId] = stored snapshot value for that input slot.
  std::map<int, std::map<std::string, float>> parameterSets;

  // Does this snapshot track (childId, slotId)?  (mirror of TiXL parametersForInputs.TryGetValue)
  const float* find(int childId, const std::string& slotId) const {
    auto cit = parameterSets.find(childId);
    if (cit == parameterSets.end()) return nullptr;
    auto iit = cit->second.find(slotId);
    if (iit == cit->second.end()) return nullptr;
    return &iit->second;
  }
};

// Build the undo-able "Blend towards snapshot" MacroCommand (TiXL TryCreateBlendTowardsVariationCommand).
// For every Float input slot of every child of `compositionSymbolId`, compute the blended value and add
// a SetOverrideCommand:
//   - TRACKED input:                 Lerp(effectiveInput, snapshotValue, weight)
//   - UNTRACKED but NON-DEFAULT:     Lerp(effectiveInput, SlotDef.def, weight)   (NIT 1)
//   - UNTRACKED and at default:      no command (skipped)
// `weight` in [0,1] is the damped blend weight (VariationCrossfader::dampedWeight()).
// Returns an empty MacroCommand (size()==0) if the composition symbol is missing or nothing blends —
// the caller checks empty() and skips the push (no dead undo entry, 照 rename/環檢 前例).
std::unique_ptr<MacroCommand> buildBlendTowardsVariationCommand(SymbolLibrary& lib,
                                                                const std::string& compositionSymbolId,
                                                                const DocVariation& target, float weight);

// Headless RED->GREEN proof of the document-override bridge (--selftest-variation-apply):
//   (1) a crossfade at weight=0.5 lands the correct Lerp midpoint AS A DOCUMENT OVERRIDE readable back
//       through effectiveInput (not a side map), and the MacroCommand undo restores the prior overrides;
//   (2) NIT 1 — an untracked-but-non-default input blends toward its SlotDef.def per TiXL;
//   (3) NIT 2 — the crossfader does NOT zero dampingVelocity on a target switch (velocity carries).
// injectBug breaks one承重 leg -> FAIL (teeth). Returns 0 = PASS, nonzero = FAIL.
int runVariationApplySelfTest(bool injectBug);

}  // namespace sw
