// runtime/point_ops_setattributeswithpointfields — shared cook entry for the SetAttributesWithPointFields
// leaf + its golden. The cook fn + the test-only injectBug toggle live in the leaf .cpp; the _golden.cpp
// drives them directly (the inject leg) so both TUs see the SAME cook (no duplication, no ≤400 balloon).
//
// runtime leaf (ARCHITECTURE.md): no upward deps.
#pragma once

namespace sw {
struct PointCookCtx;

// The SetAttributesWithPointFields cook (defined in point_ops_setattributeswithpointfields.cpp). The
// golden's inject leg calls it with a hand-built ctx.
void cookSetAttributesWithPointFields(PointCookCtx& c);

// Test-only injection seam: when true, the cook severs the FieldPoints input (FieldCount=0) → no field
// accumulation → SourcePoint passes through. Set by the golden's RED case, cleared after.
bool& setAttrWithFieldsInjectBug();
}  // namespace sw
