// ui/timeline_selftest_b9 — 批次9 fixer-E2 legs of --selftest-timeline, split mechanically out of
// timeline_selftest.cpp along the batch seam (ARCHITECTURE rule 4: one file one responsibility,
// <400). Called by runTimelineSelfTest; returns its failure count so the parent aggregates one
// verdict (= the runAnimGuiS6Legs precedent). Legs bite the EXPORTED gesture-core seams
// (timeline_internal.h) — the same code the views/executor run, not a re-enactment:
//   ⑪ per-view snap polarity      (修2: dope default-ON/Shift-off vs curve default-OFF/Shift-on)
//   ⑫ snap-indicator honesty      (修3: clamp-eaten snapped dt must NOT stamp / clean snap must)
//   ⑬ sibling-channel selection   (修4: dope click selects the whole channel group at one time;
//                                  exclusion then drops the .y sibling from the snap anchors)
//   ⑭ damp-drag drift             (修5: absolute inverse-transform mapping — key time invariant
//                                  across two damped frames with the mouse still)
//   ⑮ snap-anchor composition     (補牙: raster ticks + playhead + key times; excludeSelected)
// injectBug re-enacts each bug's data shape -> the same CHKs must FAIL (teeth proof).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/frame_cook.h"  // transportPosition (the playhead anchor expectation in ⑮)
#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "ui/timeline_internal.h"

namespace sw::ui::tl {
namespace {

bool near9(double a, double b) { return std::fabs(a - b) < 1e-9; }

bool contains(const std::vector<double>& v, double x) {
  for (double a : v)
    if (std::fabs(a - x) < 1e-9) return true;
  return false;
}

// Seed a bare Symbol with one animated input of `channels` curves, every channel carrying one key
// at each time in `times` (values = the time, Linear law shape). Test-local seam: the gesture core
// only reads sym.animator, so no library/commands are needed here.
void seedAnimator(Symbol& sym, int childId, const std::string& inputId, int channels,
                  const std::vector<double>& times) {
  std::vector<float> v(channels, (float)times[0]);
  sym.animator.animateFloatVector(childId, inputId, times[0], v.data(), channels);
  Animator::CurveArray* arr = sym.animator.curvesFor(childId, inputId);
  for (size_t i = 1; i < times.size(); ++i) {
    for (Curve& c : *arr) {
      VDefinition k;
      k.value = times[i];
      k.u = Curve::roundTime(times[i]);
      k.inInterpolation = KeyInterpolation::Linear;
      k.outInterpolation = KeyInterpolation::Linear;
      k.brokenTangents = true;
      c.addOrUpdate(times[i], k);
    }
  }
}

#define CHK(cond, msg)                                                       \
  do {                                                                       \
    if (!(cond)) { printf("  [timeline] FAIL %s\n", msg); ++failures; }      \
    else printf("  [timeline] ok   %s\n", msg);                             \
  } while (0)

}  // namespace

int runTimelineSelfTestB9Legs(bool injectBug) {
  int failures = 0;

  // ⑪ Per-view snap polarity (修2): dope = TiXL DopeSheetArea.cs:927 `if (!KeyShift)` (default ON,
  //    Shift disables); curve editor = TimelineCurveEditor.cs:461 `enableSnapping = KeyShift`
  //    (default OFF, Shift enables). injectBug = the old shared-dope-polarity shape.
  {
    auto enabled = [&](bool curveMode, bool shift) {
      return injectBug ? !shift : snapEnabledForView(curveMode, shift);
    };
    CHK(enabled(false, false), "⑪ dope view: snap ON by default");
    CHK(!enabled(false, true), "⑪ dope view: Shift disables snap");
    CHK(!enabled(true, false), "⑪ curve view: snap OFF by default (TimelineCurveEditor.cs:461)");
    CHK(enabled(true, true), "⑪ curve view: Shift ENABLES snap");
  }

  // ⑫ Snap-indicator honesty (修3): group contains key@0; a snapped dt<0 gets eaten by the rigid
  //    clamp -> the indicator must NOT stamp (the orange line / eye tl_snap would be lying). A
  //    clean snap (applied == snapped dt) must stamp. Bites applyDragOffset's applied-dt return +
  //    snapIndicatorShouldStamp — the exact pair applyDragLive uses.
  //    injectBug = the old shape: stamp whenever the snap handler reported a hit.
  {
    Symbol sym;
    seedAnimator(sym, 1, "a", 1, {0.0, 2.0});
    State st;
    st.selection = {SelKey{1, "a", 0, 0.0}, SelKey{1, "a", 0, 2.0}};
    stageDrag(st, sym, Geom{}, ImVec2(0, 0));
    const double dtSnapped = -0.1;  // snap handler pulled the grabbed key toward an anchor...
    const double applied = applyDragOffset(st, sym, dtSnapped, 0.0);  // ...but key@0 clamps to 0
    const bool stamped = injectBug ? true : snapIndicatorShouldStamp(true, dtSnapped, applied);
    CHK(near9(applied, 0.0), "⑫ rigid clamp eats the snapped dt (group contains key@0)");
    CHK(!stamped, "⑫ clamp-eaten snap does NOT stamp the indicator (no tl_snap lie)");
    const double applied2 = applyDragOffset(st, sym, 0.5, 0.0);
    CHK(snapIndicatorShouldStamp(true, 0.5, applied2),
        "⑫ clean snap (applied == snapped dt) stamps");
    CHK(!snapIndicatorShouldStamp(false, 0.5, 0.5), "⑫ no snap hit -> never stamps");
  }

  // ⑬ Sibling-channel selection (修4): dope click on .x selects the .y key at the same time
  //    (= TiXL FindParameterKeysAtPosition); the exclusion then drops BOTH from the snap anchors,
  //    so dragging .x is no longer sucked back to its own original time by the .y sibling.
  //    injectBug = the old per-key selection shape (.y stays an anchor -> snap-back).
  {
    Symbol sym;
    seedAnimator(sym, 1, "vec", 2, {1.3});  // .x and .y both keyed @1.3 (off the integer raster)
    State st;
    const SelKey click{1, "vec", 0, 1.3};
    if (injectBug) {
      st.selection = {click};  // re-enact per-key selection: the .y sibling left out
    } else {
      selectParamKeysOnClickOrDrag(st, sym, click, /*alreadySelected=*/false, false, false);
    }
    bool hasY = isSelected(st, 1, "vec", 1, 1.3);
    CHK(st.selection.size() == 2 && isSelected(st, 1, "vec", 0, 1.3) && hasY,
        "⑬ clicking .x selects the .y key at the same time (whole channel group)");
    Geom g;
    g.x1 = 400.0f;  // raster width for collectSnapAnchors (ticks land on integers @40px/bar)
    std::vector<double> anchors;
    collectSnapAnchors(sym, st, g, /*excludeSelected=*/true, anchors);
    CHK(!contains(anchors, 1.3), "⑬ exclusion drops the WHOLE selected group from the anchors");
    bool did = false;
    const double t = snapDragTime(1.32, 40.0, anchors, /*snappingDisabled=*/false, &did);
    CHK(!did && near9(t, 1.32), "⑬ dragging .x is not sucked back to 1.3 by the .y sibling");
    // cmd-deselect drops the whole group too (TiXL's FindParameterKeysAtPosition loop).
    State st2;
    selectParamKeysOnClickOrDrag(st2, sym, click, false, false, false);
    selectParamKeysOnClickOrDrag(st2, sym, click, /*alreadySelected=*/true, false, /*cmd=*/true);
    CHK(st2.selection.empty(), "⑬ cmd-click deselects the whole channel group");
  }

  // ⑭ Damp-drag drift (修5): two frames of a settling zoom (scale 40 -> 60, cursor-anchored so
  //    the time under the STATIONARY mouse is invariant — what dampView produces for a wheel
  //    zoom). Absolute mapping keeps dt frame-invariant -> the key does not move.
  //    injectBug = the old relative dxPx/drawn-scale mapping -> the same pixels re-price to a
  //    different dt and the key drifts.
  {
    Symbol sym;
    seedAnimator(sym, 1, "a", 1, {1.0, 3.0, 5.0});
    State st;
    st.selection = {SelKey{1, "a", 0, 3.0}};
    Geom g1;  // frame 1: pxPerBar=40, scroll=0, x0=0 -> key@3 sits at x=120
    stageDrag(st, sym, g1, ImVec2(120, 0), &st.selection[0]);
    const ImVec2 mouse(160, 0);  // moved 40px right, then HELD STILL
    double dt = 0.0, dv = 0.0;
    dragDeltaFromMouse(st, g1, mouse, /*allowH=*/true, /*allowV=*/false, &dt, &dv);
    applyDragOffset(st, sym, dt, dv);
    CHK(near9(dt, 1.0) && (*sym.animator.curvesFor(1, "a"))[0].hasKeyAt(4.0),
        "⑭ frame 1: 40px @40px/bar -> dt=1, key lands @4");
    Geom g2;  // frame 2: damping animates the scale; cursor-anchored -> xToTime(160) stays 4.0
    g2.pxPerBar = 60.0;
    g2.scrollBars = 4.0 - 160.0 / 60.0;
    double dt2 = 0.0;
    if (injectBug) {
      dt2 = (double)(mouse.x - st.drag.mouseStart.x) / g2.pxPerBar;  // the old relative form
    } else {
      dragDeltaFromMouse(st, g2, mouse, true, false, &dt2, &dv);
    }
    applyDragOffset(st, sym, dt2, 0.0);
    CHK(near9(dt2, dt), "⑭ frame 2 (mouse still, scale damping): dt is frame-invariant");
    CHK((*sym.animator.curvesFor(1, "a"))[0].hasKeyAt(4.0),
        "⑭ key does not drift while the canvas settles");
    // Curve-view value axis uses the same absolute form (drift-free for dv too).
    st.view.curveMode = true;
    Geom g3;
    g3.pxPerUnit = 40.0;  // y1=0, valueBottom=0 -> yToValue(-40) = 1.0
    dragDeltaFromMouse(st, g3, ImVec2(120, -40), /*allowH=*/false, /*allowV=*/true, &dt, &dv);
    CHK(near9(dv, g3.yToValue(-40) - st.drag.mouseStartValue),
        "⑭ curve-view dv = absolute value-axis mapping (yToValue(mouse) - grab value)");
  }

  // ⑮ Snap-anchor composition (補牙: the refuter named collectSnapAnchors as untested): the set
  //    = visible raster ticks + playhead + every keyframe time; excludeSelected drops exactly the
  //    selected keys (= TiXL SnapHandlerForU attractors + SelectionDragSnapExclusions).
  //    injectBug = the "exclusion ignored" shape (anchors collected with excludeSelected=false).
  {
    Symbol sym;
    seedAnimator(sym, 1, "a", 1, {1.3, 2.7});
    State st;
    Geom g;
    g.x1 = 400.0f;
    std::vector<double> anchors;
    collectSnapAnchors(sym, st, g, /*excludeSelected=*/false, anchors);
    CHK(contains(anchors, 1.3) && contains(anchors, 2.7),
        "⑮ anchors contain every keyframe time (nothing selected)");
    CHK(contains(anchors, 1.0), "⑮ anchors contain the visible raster ticks");
    CHK(contains(anchors, sw::framecook::transportPosition()),
        "⑮ anchors contain the playhead (_currentTimeMarker attractor)");
    st.selection = {SelKey{1, "a", 0, 1.3}};
    collectSnapAnchors(sym, st, g, /*excludeSelected=*/!injectBug, anchors);
    CHK(!contains(anchors, 1.3), "⑮ excludeSelected drops the selected key from the anchors");
    CHK(contains(anchors, 2.7), "⑮ ...and keeps the unselected key");
  }

  return failures;
}

}  // namespace sw::ui::tl
