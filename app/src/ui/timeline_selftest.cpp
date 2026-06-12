// ui/timeline_selftest — headless RED->GREEN teeth for the S6 timeline gesture core
// (--selftest-timeline). Bites the EXPORTED seams the views/executor call (timeline_internal.h:
// applyDragOffset / dedupeSelection / runDeleteSelected / applyTangentDrag / zoomDeltaFromWheel)
// so the legs exercise the REAL mutation code, not a re-enactment. No imgui context needed: the
// gesture core is pure of imgui input state (views feed it dt/dv/angle).
// Legs = refuter 批次8 S6 五條 BROKEN 轉正式牙:
//   ① rigid group drag clamp (修1①: no merge / spacing kept / undo-redo clean)
//   ② ghost-selection dedupe (修1②/修5)
//   ③ delete misroute guard (修1③: duplicates/ghosts must NOT trip RemoveAnimation)
//   ④ boundary tangent roundtrip (修2: no updateTangents mid-drag)
//   ⑤ Linear->Tangent promotion on broken drag (修3)
//   ⑥ wheel-zoom integer-step pins (修4)
// 批次9 S6 四殘項 legs:
//   ⑦ time snap math (snapDragTime: in-radius snaps / out stays / Shift disables / nearest wins)
//   ⑧ beat raster ladder (computeRaster: gear change across zoom + fade + label format)
//   ⑨ curve-view insert keys (runInsertKeys: sampled value + one-macro undo roundtrip)
//   ⑩ canvas damping (dampView: damped intermediate frame + N-frame convergence + NaN guard)
// injectBug re-introduces each bug's data shape -> the same CHKs must FAIL (teeth proof).
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/animation_commands.h"
#include "app/command.h"
#include "runtime/compound_save.h"  // libToJsonV2 (byte-faithful undo 比對)
#include "runtime/graph.h"          // defaultParticleGraph (selftest seed)
#include "runtime/graph_bridge.h"   // libFromGraph (selftest seed)
#include "ui/timeline_internal.h"
#include "ui/timeline_window.h"

namespace sw::ui {
namespace {

using tl::SelKey;
using tl::State;

// Same shape as animation_commands_selftest*.cpp's finder (test-local seam, duplicated on
// purpose: the selftest TUs stay independently compilable).
struct AnimTarget { int childId = 0; std::string slotId; bool ok = false; };
AnimTarget findFloatTarget(SymbolLibrary& lib) {
  AnimTarget t;
  Symbol* root = lib.find(lib.rootId);
  if (!root) return t;
  for (const SymbolChild& c : root->children) {
    const Symbol* d = lib.find(c.symbolId);
    if (!d || !d->atomic) continue;
    for (const SlotDef& s : d->inputDefs)
      if (s.dataType == "Float") { t.childId = c.id; t.slotId = s.id; t.ok = true; return t; }
  }
  return t;
}

// Seed: one animated Float input with keys @1/@3/@5 (values 1/3/1). Returns the curve array ptr.
Animator::CurveArray* seedThreeKeys(SymbolLibrary& lib, CommandStack& stack, const AnimTarget& t) {
  const std::string r = lib.rootId;
  stack.push(std::make_unique<AddAnimationCommand>(lib, r, t.childId, t.slotId, 1.0, 1.0f));
  stack.push(std::make_unique<AddKeyframeCommand>(lib, r, t.childId, t.slotId, 0, 3.0));
  stack.push(std::make_unique<WriteKeyAtPlayheadCommand>(lib, r, t.childId, t.slotId, 0, 3.0, 3.0f));
  stack.push(std::make_unique<AddKeyframeCommand>(lib, r, t.childId, t.slotId, 0, 5.0));
  stack.push(std::make_unique<WriteKeyAtPlayheadCommand>(lib, r, t.childId, t.slotId, 0, 5.0, 1.0f));
  return lib.find(r)->animator.curvesFor(t.childId, t.slotId);
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

#define CHK(cond, msg)                                                        \
  do {                                                                        \
    if (!(cond)) { printf("  [timeline] FAIL %s\n", msg); ++failures; }       \
    else printf("  [timeline] ok   %s\n", msg);                              \
  } while (0)

}  // namespace

int runTimelineSelfTest(bool injectBug) {
  int failures = 0;

  // ① Rigid group drag clamp: 3 keys @1/@3/@5, select @3+@5, drag dt=-8 (way past 0).
  //    Fixed: ONE clamp -> dt=-3, keys land @0/@2 (spacing kept), key@1 untouched, no merge.
  //    injectBug = the OLD per-key max(0,·) clamp -> both keys pile onto t=0 and merge.
  {
    SymbolLibrary lib = libFromGraph(defaultParticleGraph());
    AnimTarget t = findFloatTarget(lib);
    CommandStack stack;
    Animator::CurveArray* live = seedThreeKeys(lib, stack, t);
    Symbol* sym = lib.find(lib.rootId);
    const std::string preDrag = libToJsonV2(lib);
    State st;
    st.selection = {SelKey{t.childId, t.slotId, 0, 3.0}, SelKey{t.childId, t.slotId, 0, 5.0}};
    tl::stageDrag(st, *sym, ImVec2(0, 0));
    double applied = tl::applyDragOffset(st, *sym, -8.0, 0.0);
    if (injectBug) {  // re-enact the per-key clamp (the 修1 bug shape)
      for (const tl::GroupSnap& gs : st.drag.before)
        sym->animator.setCurves(gs.childId, gs.inputId, gs.before);
      live = sym->animator.curvesFor(t.childId, t.slotId);
      for (const tl::DragKey& dk : st.drag.keys) (*live)[0].removeAt(dk.startTime);
      st.selection.clear();
      for (const tl::DragKey& dk : st.drag.keys) {
        const double newT = std::max(0.0, Curve::roundTime(dk.startTime - 8.0));
        VDefinition v = dk.def;
        v.u = newT;
        (*live)[0].addOrUpdate(newT, v);
        st.selection.push_back(SelKey{dk.key.childId, dk.key.inputId, 0, newT});
      }
      applied = -8.0;
    }
    live = sym->animator.curvesFor(t.childId, t.slotId);
    const Curve& c = (*live)[0];
    CHK(near(applied, -3.0), "① group clamp: dt=-8 clamped ONCE to -3 (left key stops at 0)");
    CHK(c.count() == 3, "① no merge: all 3 keys survive the over-drag");
    CHK(c.hasKeyAt(0.0) && c.hasKeyAt(2.0), "① rigid spacing kept: selected keys land @0 and @2");
    CHK(c.hasKeyAt(1.0), "① unselected key@1 untouched");
    std::vector<SelKey> dedup = st.selection;
    tl::dedupeSelection(dedup);
    CHK(st.selection.size() == 2 && dedup.size() == st.selection.size(),
        "① selection stays one-entry-per-key (no ghosts)");
    tl::finishDrag(st, lib, stack, lib.rootId, *sym);
    stack.undo();
    CHK(libToJsonV2(lib) == preDrag, "① undo restores the pre-drag state byte-faithful");
    stack.redo();
    const Curve& c2 = (*sym->animator.curvesFor(t.childId, t.slotId))[0];
    CHK(c2.count() == 3 && c2.hasKeyAt(0.0) && c2.hasKeyAt(2.0), "① redo re-applies the rigid move");
  }

  // ② dedupeSelection collapses entries naming the same (lane, rounded time).
  {
    std::vector<SelKey> sel = {SelKey{1, "a", 0, 2.0}, SelKey{1, "a", 0, 2.00001},  // rounds to 2.0
                               SelKey{1, "a", 0, 3.0}};
    tl::dedupeSelection(sel);
    if (injectBug) sel.push_back(sel[0]);  // a ghost the prune law must never let survive
    CHK(sel.size() == 2 && near(Curve::roundTime(sel[0].time), 2.0) && near(sel[1].time, 3.0),
        "② dedupe: same-(lane,roundTime) entries collapse, order kept");
  }

  // ③ Delete misroute guard: selection carries a duplicate + a ghost (4 entries vs 3 real keys).
  //    Fixed: unique-existing count (2) < total (3) -> per-key delete; animation survives.
  //    injectBug pushes the OLD misroute (RemoveAnimation) -> the survival CHKs must FAIL.
  {
    SymbolLibrary lib = libFromGraph(defaultParticleGraph());
    AnimTarget t = findFloatTarget(lib);
    CommandStack stack;
    seedThreeKeys(lib, stack, t);
    Symbol* sym = lib.find(lib.rootId);
    State st;
    st.selection = {SelKey{t.childId, t.slotId, 0, 3.0}, SelKey{t.childId, t.slotId, 0, 5.0},
                    SelKey{t.childId, t.slotId, 0, 3.0},   // duplicate (merge ghost)
                    SelKey{t.childId, t.slotId, 0, 9.0}};  // ghost: no key @9
    if (injectBug) {
      auto rm = std::make_unique<RemoveAnimationCommand>(lib, lib.rootId, t.childId, t.slotId);
      stack.push(std::move(rm));  // the old "keys.size() >= total" misroute, re-enacted
      st.selection.clear();
    } else {
      tl::runDeleteSelected(st, lib, stack, lib.rootId, *sym);
    }
    const Animator::CurveArray* arr = sym->animator.curvesFor(t.childId, t.slotId);
    CHK(sym->animator.isAnimated(t.childId, t.slotId),
        "③ partial delete keeps the animation (driver NOT flipped to Constant)");
    CHK(arr && (*arr)[0].count() == 1 && (*arr)[0].hasKeyAt(1.0),
        "③ only selected keys deleted; unselected key@1 alive");
    CHK(st.selection.empty(), "③ selection cleared after delete");
    stack.undo();
    const Animator::CurveArray* back = sym->animator.curvesFor(t.childId, t.slotId);
    CHK(back && (*back)[0].count() == 3, "③ undo restores the deleted keys");
    // All-selected branch still collapses to RemoveAnimation (= TiXL RemoveAnimationsCommand).
    st.selection = {SelKey{t.childId, t.slotId, 0, 1.0}, SelKey{t.childId, t.slotId, 0, 3.0},
                    SelKey{t.childId, t.slotId, 0, 5.0}};
    tl::runDeleteSelected(st, lib, stack, lib.rootId, *sym);
    CHK(!sym->animator.isAnimated(t.childId, t.slotId),
        "③ ALL keys selected -> RemoveAnimation branch (driver back to Constant)");
  }

  // ④ Boundary tangent roundtrip: authored broken angles on the FIRST key's IN and the LAST
  //    key's OUT must survive (修2: no updateTangents mid-drag re-mirroring them away).
  //    injectBug re-adds the per-frame updateTangents -> both angles get punched back.
  {
    SymbolLibrary lib = libFromGraph(defaultParticleGraph());
    AnimTarget t = findFloatTarget(lib);
    CommandStack stack;
    const std::string r = lib.rootId;
    stack.push(std::make_unique<AddAnimationCommand>(lib, r, t.childId, t.slotId, 1.0, 1.0f));
    stack.push(std::make_unique<AddKeyframeCommand>(lib, r, t.childId, t.slotId, 0, 2.0));
    stack.push(std::make_unique<WriteKeyAtPlayheadCommand>(lib, r, t.childId, t.slotId, 0, 2.0, 5.0f));
    Symbol* sym = lib.find(r);
    Animator::CurveArray* live = sym->animator.curvesFor(t.childId, t.slotId);
    const std::string preTan = libToJsonV2(lib);
    Animator::CurveArray before = *live;
    tl::applyTangentDrag((*live)[0], 1.0, /*inSide=*/true, -1.0, /*breakTangents=*/true);
    tl::applyTangentDrag((*live)[0], 2.0, /*inSide=*/false, 2.5, /*breakTangents=*/true);
    if (injectBug) (*live)[0].updateTangents();  // the 修2 bug shape: per-frame re-mirror
    {
      const VDefinition& k1 = (*live)[0].table().at(Curve::roundTime(1.0));
      const VDefinition& k2 = (*live)[0].table().at(Curve::roundTime(2.0));
      CHK(k1.inInterpolation == KeyInterpolation::Tangent && near(k1.inTangentAngle, -1.0),
          "④ FIRST key IN: authored broken angle survives the drag");
      CHK(k2.outInterpolation == KeyInterpolation::Tangent && near(k2.outTangentAngle, 2.5),
          "④ LAST key OUT: authored broken angle survives the drag");
    }
    Animator::CurveArray after = *live;
    stack.push(std::make_unique<SetCurveGroupSnapshotCommand>(
        lib, r,
        std::vector<CurveGroupEdit>{CurveGroupEdit{t.childId, t.slotId, std::move(before),
                                                   std::move(after)}},
        "Edit Tangents"));
    stack.undo();
    CHK(libToJsonV2(lib) == preTan, "④ undo byte-faithful");
    stack.redo();
    const Curve& cb = (*sym->animator.curvesFor(t.childId, t.slotId))[0];
    CHK(near(cb.table().at(Curve::roundTime(1.0)).inTangentAngle, -1.0) &&
            near(cb.table().at(Curve::roundTime(2.0)).outTangentAngle, 2.5),
        "④ redo: authored angles roundtrip");
  }

  // ⑤ Linear->Tangent promotion (修3, CurvePoint.cs:289-298): broken drag on one side promotes
  //    the opposite Linear side; the segment leaves the linear fast path; the promoted side now
  //    survives structural updateTangents (Tangent mode = authored).
  {
    SymbolLibrary lib = libFromGraph(defaultParticleGraph());
    AnimTarget t = findFloatTarget(lib);
    CommandStack stack;
    Animator::CurveArray* live = seedThreeKeys(lib, stack, t);
    Curve& c = (*live)[0];
    tl::applyTangentDrag(c, 3.0, /*inSide=*/true, -1.2, /*breakTangents=*/true);  // middle key
    VDefinition& km = c.table().at(Curve::roundTime(3.0));
    if (injectBug) km.outInterpolation = KeyInterpolation::Linear;  // strip the promotion
    CHK(km.outInterpolation == KeyInterpolation::Tangent,
        "⑤ broken IN drag promotes the opposite Linear side to Tangent");
    CHK(km.inInterpolation == KeyInterpolation::Tangent && near(km.inTangentAngle, -1.2),
        "⑤ dragged IN side carries the authored angle");
    // Segment [1,3] now has a Tangent endpoint -> sampler takes the spline path, departing the
    // linear midpoint (keys @1=1, @3=3 -> linear sample(2.0)=2.0).
    CHK(std::fabs(c.sample(2.0) - 2.0) > 0.05, "⑤ sampling goes spline, not linear");
    const double outBefore = km.outTangentAngle;
    c.updateTangents();  // a STRUCTURAL pass (add/remove/move would trigger this)
    CHK(near(km.outTangentAngle, outBefore),
        "⑤ promoted side's angle survives structural updateTangents (authored now)");
  }

  // ⑥ Wheel-zoom pins (修4, ScalableCanvas.cs:453-477): integer 1.2 steps — a fractional notch
  //    counts as ONE full step — clamped [0.02,100]. injectBug = the old pow(1.2, wheel).
  {
    auto zoom = [&](float w) {
      return injectBug ? std::pow(1.2, (double)w) : tl::zoomDeltaFromWheel(w);
    };
    CHK(near(zoom(0.5f), 1.2), "⑥ wheel +0.5 -> one full 1.2 step (Mac touchpad fractional)");
    CHK(near(zoom(2.5f), 1.2 * 1.2 * 1.2), "⑥ wheel +2.5 -> three steps (1.728)");
    CHK(near(zoom(-0.5f), 1.0 / 1.2), "⑥ wheel -0.5 -> one step down");
    CHK(near(zoom(-2.5f), 1.0 / (1.2 * 1.2 * 1.2)), "⑥ wheel -2.5 -> three steps down");
    CHK(near(zoom(0.0f), 1.0), "⑥ wheel 0 -> identity");
    CHK(near(zoom(40.0f), 100.0) && near(zoom(-40.0f), 0.02), "⑥ factor clamped to [0.02,100]");
  }

  // ⑦ Time snap math (= ValueSnapHandler/SnapResult + DopeSheetArea.cs:927 Shift gate):
  //    threshold = 5px / pxPerBar; in-radius snaps, outside stays, Shift disables, nearest wins.
  //    injectBug = ignore the Shift gate + double the radius (the "snap always grabs" shape).
  {
    auto snap = [&](double target, double pxPerBar, const std::vector<double>& anchors,
                    bool shiftHeld, bool* did) {
      return tl::snapDragTime(target, injectBug ? pxPerBar / 2.0 : pxPerBar, anchors,
                              injectBug ? false : shiftHeld, did);
    };
    const std::vector<double> anchors = {1.0, 2.0};
    bool did = false;
    CHK(near(snap(1.05, 40.0, anchors, false, &did), 1.0) && did,
        "⑦ in radius (5px/scale): 1.05 @40px/bar snaps to beat 1.0");
    CHK(near(snap(1.5, 40.0, anchors, false, &did), 1.5) && !did,
        "⑦ outside radius: 1.5 stays (no anchor within 0.125 bars)");
    CHK(near(snap(1.05, 1000.0, anchors, false, &did), 1.05) && !did,
        "⑦ radius shrinks with zoom: 1.05 @1000px/bar stays (threshold 0.005)");
    CHK(near(snap(1.05, 40.0, anchors, true, &did), 1.05) && !did,
        "⑦ Shift disables snapping (DopeSheetArea.cs:927)");
    const std::vector<double> pair = {1.0, 1.08};
    CHK(near(snap(1.05, 40.0, pair, false, &did), 1.08),
        "⑦ nearest anchor wins (best force, SnapResult)");
  }

  // ⑧ Beat raster ladder (= BeatTimeRaster ScaleRanges): 16th ticks at high zoom, measure+bar
  //    gear at default, phrase+measure at far zoom; finest level fades; labels "bar.beat".
  //    injectBug = the OLD pow2 ladder (one spacing, no beats/fade) -> gear CHKs FAIL.
  {
    std::vector<tl::RasterTick> ticks;
    auto raster = [&](double px, double w) {
      if (!injectBug) { tl::computeRaster(px, 0.0, w, ticks); return; }
      ticks.clear();  // re-enact the 批次8 pow2 fork: single step >= 50px, plain numeric labels
      double step = 0.25;
      while (step * px < 50.0 && step < 1e6) step *= 2.0;
      for (double t = step; t * px < w; t += step) {
        tl::RasterTick k; k.bars = t; snprintf(k.label, sizeof(k.label), "%g", t);
        ticks.push_back(k);
      }
    };
    auto find = [&](double bars) -> const tl::RasterTick* {
      for (const tl::RasterTick& t : ticks)
        if (std::fabs(t.bars - bars) < 1e-9) return &t;
      return nullptr;
    };
    raster(2000.0, 1000.0);  // invertedScale 0.0005 -> finest gear (bar+beat+16th)
    CHK(find(0.0625) != nullptr, "⑧ high zoom gear: 16th ticks present (spacing 1/16 bar)");
    const tl::RasterTick* beat = find(0.25);
    CHK(beat && std::string(beat->label) == "0.1", "⑧ beat label format 'bar.beat' (BuildLabel)");
    raster(40.0, 400.0);  // invertedScale 0.025 -> measure+bar gear (range 0.012-0.03)
    CHK(find(0.25) == nullptr, "⑧ default zoom gear: no beat ticks (gear changed)");
    const tl::RasterTick* bar1 = find(1.0);
    const tl::RasterTick* meas = find(4.0);
    CHK(bar1 && meas && std::string(meas->label) == "4", "⑧ measure+bar ticks present");
    CHK(bar1 && bar1->labelAlpha < 1.0f && meas && near(meas->labelAlpha, 1.0),
        "⑧ finest level fades as you zoom away (fadeFactor)");
    raster(5.0, 400.0);  // invertedScale 0.2 -> phrase+measure gear (range 0.15-0.3)
    const tl::RasterTick* phrase = find(16.0);
    const tl::RasterTick* m4 = find(4.0);
    CHK(phrase && std::string(phrase->label) == "16" && m4 && m4->label[0] == '\0' &&
            m4->lineAlpha < 1.0f,
        "⑧ far zoom gear: labeled phrases + fading unlabeled measures");
  }

  // ⑨ Curve-view insert keys (= TiXL "Insert keyframes" macro): key lands ON the curve (sampled
  //    value, NOT the click v), one undo step restores byte-faithful.
  //    injectBug = the "value from the click point" shape -> the sampled-value CHK FAILS.
  {
    SymbolLibrary lib = libFromGraph(defaultParticleGraph());
    AnimTarget t = findFloatTarget(lib);
    CommandStack stack;
    seedThreeKeys(lib, stack, t);  // keys @1/@3/@5 = 1/3/1 (linear)
    Symbol* sym = lib.find(lib.rootId);
    const std::string preInsert = libToJsonV2(lib);
    tl::runInsertKeys(lib, stack, lib.rootId, {SelKey{t.childId, t.slotId, 0, 2.0}});
    Animator::CurveArray* arr = sym->animator.curvesFor(t.childId, t.slotId);
    if (injectBug)  // re-enact "value = double-click v": overwrite with the (wrong) click value
      (*arr)[0].table().at(Curve::roundTime(2.0)).value = 99.0;
    CHK((*arr)[0].count() == 4 && (*arr)[0].hasKeyAt(2.0), "⑨ insert lands a key at the time");
    CHK(near((*arr)[0].table().at(Curve::roundTime(2.0)).value, 2.0),
        "⑨ value = curve's SAMPLED value (TiXL InsertNewKeyframe), not the click v");
    stack.undo();
    CHK(libToJsonV2(lib) == preInsert, "⑨ ONE undo removes the whole insert, byte-faithful");
    stack.redo();
    CHK((*sym->animator.curvesFor(t.childId, t.slotId))[0].hasKeyAt(2.0), "⑨ redo re-inserts");
  }

  // ⑩ Canvas damping (= ScalableCanvas.DampScaling): the drawn scale/scroll EASE toward the
  //    targets (intermediate frame strictly between), converge to the exact target within ~1s of
  //    frames (completed snap), and NaN targets are guarded.
  //    injectBug = the 批次8 "no damping" fork (drawn jumps to target) -> the eased CHK FAILS.
  {
    tl::ViewState v;
    v.pxPerBarT = 80.0;   // wheel zoom wrote the target...
    v.scrollBarsT = 5.0;  // ...and an anchored scroll
    auto step = [&]() {
      if (injectBug) { v.pxPerBar = v.pxPerBarT; v.scrollBars = v.scrollBarsT; return; }
      tl::dampView(v, 600.0, 180.0, 1.0 / 60.0);
    };
    step();
    CHK(v.pxPerBar > 40.0 + 1e-6 && v.pxPerBar < 80.0 - 1e-6,
        "⑩ one frame in, the drawn scale is BETWEEN start and target (damped, not a jump)");
    for (int i = 0; i < 200; ++i) step();
    CHK(near(v.pxPerBar, 80.0) && near(v.scrollBars, 5.0),
        "⑩ converges to the exact target (completed snap, cs:192-201)");
    v.pxPerBarT = std::nan("");
    tl::dampView(v, 600.0, 180.0, 1.0 / 60.0);
    CHK(std::isfinite(v.pxPerBar) && std::isfinite(v.pxPerBarT), "⑩ NaN target guarded (cs:217)");
  }

  printf("[timeline] %s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}

}  // namespace sw::ui
