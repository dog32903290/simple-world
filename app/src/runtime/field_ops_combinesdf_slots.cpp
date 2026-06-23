// CombineSDF slot-id registration (PF-0c Option B, slot-id==port-id guard).
//
// WHY A SEPARATE TU: field_ops_combinesdf.cpp owns CombineSDF's NodeSpec, factory, configurer, and its
// FieldOp registrar — but it sits at the line-count cap (the ratchet, no grandfather bump), so its
// registrar line cannot grow to the 4-arg FieldOp overload that pushes slot ids into fieldSlotSpecs().
// Rather than peel the frozen leaf this wave, CombineSDF's REAL apply-table slot ids are registered here
// via the standalone FieldSlotIds registrar. These ids are the SAME ones configureCombineSdfFromParams
// applies (field_ops_combinesdf.cpp configureCombineSdfFromParams: applyFloatSlot "K" +
// applyIntSelSlot "CombineMethod") — single source of truth, the guard reads them and cannot drift from
// a hand-copied list. Both are real PortSpec.ids in combineSdfSpec() (K, CombineMethod), so the guard
// passes. (When CombineSDF is later peeled, this row folds back into its 4-arg FieldOp registrar.)
#include "runtime/field_node_registry.h"  // FieldSlotIds

namespace sw {
namespace {
const FieldSlotIds g_combineSdfSlots("CombineSDF", {"K", "CombineMethod"});
}  // namespace
}  // namespace sw
