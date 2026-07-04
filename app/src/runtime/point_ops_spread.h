// runtime/point_ops_spread — the SPREAD family (render/transform): SpreadIntoGrid + SpreadLayout.
//
// Both are TiXL MultiInput-Command layout ops: they iterate their child Command chains and push a
// PER-CHILD ObjectToWorld (SpreadIntoGrid.cs:33-66 / SpreadLayout.cs:55-93 for-loop over
// Commands.CollectedInputs) — unlike Group, whose single SRT wraps ALL children identically. sw's
// retained-mode analog: the per-wire item boundaries the cook driver records into
// CmdCookCtx::inputCmdWireItemCounts let the op stamp a DIFFERENT group-SRT per child chain
// (the same per-item hasGroup/groupObjectToWorld stamp + executor multiply Group rides).
//
// Tiny own-header (NOT the at-cap point_ops.h — the Group/field_camera precedent) so the registrar
// and the goldens can reach the ops without the god-header. runtime leaf: no upward deps.
#pragma once

namespace sw {

// Register both spread cmd ops ("SpreadIntoGrid" + "SpreadLayout"); called by registerDrawPointOps.
void registerSpreadOps();

// Test-only op flag (the per-wire seam tooth): when true, BOTH spread cooks run their REAL math but
// with every child's spreadIndex forced to 0 — the per-wire boundary mechanism is corrupted (all
// children collapse onto child 0's transform) while the cook path itself still runs. The goldens'
// -bug leg sets it; the per-index expected translations then diverge → RED. OFF in production (zero
// behavior change). CPU op flag, NOT a shader branch (constitution rule); parallel to
// executeCollectFirstOnlyForTest / g_groupDropPush.
bool& spreadCollapseIndexForTest();

// --selftest entries (point_ops_spreadintogrid_golden.cpp / point_ops_spreadlayout_golden.cpp).
int runSpreadIntoGridSelfTest(bool injectBug);
int runSpreadLayoutSelfTest(bool injectBug);

}  // namespace sw
