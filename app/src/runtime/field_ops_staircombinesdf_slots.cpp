// StairCombineSDF slot-id registration (PF-0c Option B, slot-id==port-id guard).
//
// WHY A SEPARATE TU: field_ops_staircombinesdf.cpp owns StairCombineSDF's NodeSpec, factory, configurer,
// and its FieldOp registrar — but it sits at the line-count cap (the ratchet, NO grandfather bump), so its
// registrar line cannot grow to the 4-arg FieldOp overload that pushes slot ids into fieldSlotSpecs().
// Rather than peel the frozen leaf this wave, StairCombineSDF's REAL apply-table slot ids are registered
// here via the standalone FieldSlotIds registrar. These ids are the SAME ones
// configureStairCombineSdfFromParams applies (field_ops_staircombinesdf.cpp: applyFloatSlot "K" +
// applyFloatSlot "Steps" + applyIntSelSlot "CombineMethod") — single source of truth, the guard reads them
// and cannot drift from a hand-copied list. All three are real PortSpec.ids in stairCombineSdfSpec()
// (K, Steps, CombineMethod), so the guard passes. (When StairCombineSDF is later peeled, this row folds
// back into its 4-arg FieldOp registrar — exactly like field_ops_combinesdf_slots.cpp.)
#include "runtime/field_node_registry.h"  // FieldSlotIds

namespace sw {
namespace {
const FieldSlotIds g_stairCombineSdfSlots("StairCombineSDF", {"K", "Steps", "CombineMethod"});
}  // namespace
}  // namespace sw
