// runtime/point_ops_visiblegizmos — VisibleGizmos command op (camera-B lane).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/VisibleGizmos.cs (GUID d61d7192) +
// VisibleGizmos.t3 + Core/Operator/EvaluationContext.cs:16-22 (the GizmoVisibility enum).
//
// VisibleGizmos.cs:16-63 Update: a VISIBILITY GATE over a MultiInput Commands subtree —
//   visibility = (Visibility == Inherit) ? context.ShowGizmos : Visibility;
//   if (visibility != On && !showIfSelected) return;      // hidden → commands NOT evaluated
//   foreach (t in commands) t.GetValue(context);          // visible → evaluate all collected wires
// GizmoVisibility: Inherit=-1, Off=0, On=1, IfSelected=2 (EvaluationContext.cs:16-22). The stored
// param value IS the TiXL enum int (the .t3 default "Inherit" = -1).
//
// FORKS (named):
//   - fork-visiblegizmos-inherit-off: Inherit resolves to context.ShowGizmos in TiXL; a FRESH TiXL
//     EvaluationContext leaves ShowGizmos at default(GizmoVisibility) = Off (Reset() never sets it —
//     the EDITOR's output-window gizmo toggle is what flips it, and sw has no such ambient toggle yet).
//     So Inherit → Off here. When an output-window gizmo toggle lands, THIS is the seam to thread it.
//   - fork-visiblegizmos-ifselected-never: IfSelected walks MouseInput.SelectedChildId up the instance
//     parents (VisibleGizmos.cs:35-48) — editor selection does not reach the cook rail; never selected
//     → IfSelected behaves as hidden.
//   - fork-visiblegizmos-gather-side-effects: TiXL skips EVALUATING the subtree when hidden; the sw
//     driver gathers (cooks) the Commands wires before the op runs, so a hidden gate DROPS the items
//     instead of never cooking them (same retained-gather posture as ReuseCamera's missing-ref path).
//   - the dead prewarm block VisibleGizmos.cs:23-31 never runs (_updatedOnce is initialized true,
//     :65) → not ported.
#pragma once

namespace sw {

void registerVisibleGizmosOp();

// --selftest-visiblegizmos golden: two Layer2d quads (disjoint NDC regions) wired into the MultiInput
// Commands port, cooked through the PRODUCTION resident terminal at Visibility = On / Off / Inherit.
//   On (1)      → BOTH probes quad-colored (the MultiInput gather passes every wire through the gate).
//   Off (0)     → both probes background (the gate drops the whole chain).
//   Inherit(-1) → both probes background (fork-visiblegizmos-inherit-off pinned: TiXL fresh-context
//                 ShowGizmos default = Off).
// injectBug: DROP THE GATE in the real cook (wrapper forces pass-through regardless of Visibility) →
// the Off leg shows the quads → RED. did-not-trip → return 0 (NO-BITE latch).
int runVisibleGizmosSelfTest(bool injectBug);

}  // namespace sw
