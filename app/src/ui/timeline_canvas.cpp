// ui/timeline_canvas — ScalableCanvas/ValueSnapHandler math ports for the S6 timeline (S6 四殘項
// batch 9). Pure of imgui INPUT/context so --selftest-timeline bites the real code headless:
//   zoomDeltaFromWheel  = ComputeZoomDeltaFromMouseWheel (ScalableCanvas.cs:453-477, 修4)
//   dampView            = DampScaling (ScalableCanvas.cs:190-235): targets vs drawn, extent lerp
//   snapDragTime        = ValueSnapHandler.TryCheckForSnapping + SnapResult.TryToImprove…
//                         (ValueSnapHandler.cs / SnapResult.cs), Shift gate = DopeSheetArea.cs:927
//   collectSnapAnchors  = the SnapHandlerForU attractor set (TimeLineCanvas.cs:48-54), trimmed to
//                         our scope: raster ticks + playhead + non-selected keys (no clip/loop)
// Zone: ui (timeline-internal); mutation-free — all writes stay in timeline_edit (BUG-B 律).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "app/frame_cook.h"  // transportPosition: the playhead snap attractor (_currentTimeMarker)
#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "ui/timeline_internal.h"

namespace sw::ui::tl {

double zoomDeltaFromWheel(float wheel) {
  double sum = 1.0;
  if (wheel < 0.0f)
    for (float z = wheel; z < 0.0f; z += 1.0f) sum /= 1.2;
  if (wheel > 0.0f)
    for (float z = wheel; z > 0.0f; z -= 1.0f) sum *= 1.2;
  return std::clamp(sum, 0.02, 100.0);
}

void dampView(ViewState& v, double widthPx, double heightPx, double dt) {
  // NaN guards = ScalableCanvas.cs:217-235 (targets first, then drawn snaps to target).
  if (!std::isfinite(v.pxPerBarT)) v.pxPerBarT = 40.0;
  if (!std::isfinite(v.pxPerUnitT)) v.pxPerUnitT = 40.0;
  if (!std::isfinite(v.scrollBarsT)) v.scrollBarsT = 0.0;
  if (!std::isfinite(v.valueBottomT)) v.valueBottomT = 0.0;
  if (!std::isfinite(v.pxPerBar) || !std::isfinite(v.pxPerUnit) || v.pxPerUnit <= 0.0) {
    v.pxPerBar = v.pxPerBarT;
    v.pxPerUnit = v.pxPerUnitT;
  }
  if (!std::isfinite(v.scrollBars) || !std::isfinite(v.valueBottom)) {
    v.scrollBars = v.scrollBarsT;
    v.valueBottom = v.valueBottomT;
  }
  // Completed test = cs:192-201 verbatim (incl. the Scale.X > 1000 high-zoom early-out; thresholds
  // are in canvas units/scale units — same constants as TiXL's timeline canvas).
  const bool completed = v.pxPerBar > 1000.0 ||
                         (std::fabs(v.scrollBars - v.scrollBarsT) < 1.0 &&
                          std::fabs(v.valueBottom - v.valueBottomT) < 1.0 &&
                          std::fabs(v.pxPerBar - v.pxPerBarT) < 0.05 &&
                          std::fabs(v.pxPerUnit - v.pxPerUnitT) < 0.05);
  if (completed) {
    v.scrollBars = v.scrollBarsT;
    v.valueBottom = v.valueBottomT;
    v.pxPerBar = v.pxPerBarT;
    v.pxPerUnit = v.pxPerUnitT;
    return;
  }
  // Lerp the viewport EXTENTS in canvas space, then rebuild scale+scroll from the new extents
  // (cs:204-215): zoom and pan converge together without the anchor swimming.
  const double f = std::min(dt / 0.06, 1.0);  // ScrollSmoothing 0.06, clamp [0.01,0.99] upstream
  {
    const double mn = v.scrollBars, mx = v.scrollBars + widthPx / v.pxPerBar;
    const double mnT = v.scrollBarsT, mxT = v.scrollBarsT + widthPx / v.pxPerBarT;
    const double nmn = mn + (mnT - mn) * f, nmx = mx + (mxT - mx) * f;
    if (nmx - nmn > 1e-12) { v.pxPerBar = widthPx / (nmx - nmn); v.scrollBars = nmn; }
  }
  {
    const double mn = v.valueBottom, mx = v.valueBottom + heightPx / v.pxPerUnit;
    const double mnT = v.valueBottomT, mxT = v.valueBottomT + heightPx / v.pxPerUnitT;
    const double nmn = mn + (mnT - mn) * f, nmx = mx + (mxT - mx) * f;
    if (nmx - nmn > 1e-12) { v.pxPerUnit = heightPx / (nmx - nmn); v.valueBottom = nmn; }
  }
}

bool snapEnabledForView(bool curveMode, bool shiftHeld) {
  // 修2: dope = snap default-ON, Shift off (DopeSheetArea.cs:927); curve editor = snap default-
  // OFF, Shift on (TimelineCurveEditor.cs:461). Contract in timeline_internal.h.
  return curveMode ? shiftHeld : !shiftHeld;
}

bool snapIndicatorShouldStamp(bool didSnap, double dtSnapped, double dtApplied) {
  // 修3: the rigid clamp either keeps dt or replaces it outright (max(dt, -minStart)), so exact
  // compare is the right test — a clamp-eaten snap must NOT light the indicator / hit eye tl_snap.
  return didSnap && dtApplied == dtSnapped;
}

double snapDragTime(double target, double pxPerBar, const std::vector<double>& anchors,
                    bool snappingDisabled, bool* didSnap) {
  if (didSnap) *didSnap = false;
  if (snappingDisabled || pxPerBar <= 0.0) return target;  // Shift gate (DopeSheetArea.cs:927)
  const double threshold = 5.0 / pxPerBar;  // SnapStrength 5 px / CanvasScale (SnapResult.cs)
  double bestForce = 0.0, bestAnchor = 0.0;
  bool valid = false;
  for (double a : anchors) {
    const double force = threshold - std::fabs(a - target);
    if (force < 0.00001) continue;            // SnapResult: newForce < 0.00001 -> reject
    if (valid && bestForce > force) continue;  // keep the strongest (closest) attractor
    bestForce = force;
    bestAnchor = a;
    valid = true;
  }
  if (!valid) return target;
  if (didSnap) *didSnap = true;
  return bestAnchor;
}

void collectSnapAnchors(const Symbol& sym, const State& s, const Geom& g, bool excludeSelected,
                        std::vector<double>& out) {
  out.clear();
  // Raster ticks (= TimeRasterSwitcher attractor: AbstractTimeRaster.CheckForSnap snaps to the
  // DRAWN tick positions, _usedPositions).
  static std::vector<RasterTick> ticks;
  computeRaster(g.pxPerBar, g.scrollBars, (double)(g.x1 - g.x0), ticks);
  for (const RasterTick& t : ticks) out.push_back(t.bars);
  // Playhead (= _currentTimeMarker attractor).
  out.push_back(sw::framecook::transportPosition());
  // Every keyframe time, minus the dragged selection (= DopeSheetArea.CheckForSnap with
  // SelectionDragSnapExclusions, cs:1096-1111).
  for (const auto& [childId, byInput] : sym.animator.all()) {
    for (const auto& [inputId, curves] : byInput) {
      for (int idx = 0; idx < (int)curves.size(); ++idx) {
        for (const auto& [u, vdef] : curves[idx].table()) {
          (void)vdef;
          if (excludeSelected && isSelected(s, childId, inputId, idx, u)) continue;
          out.push_back(u);
        }
      }
    }
  }
}

}  // namespace sw::ui::tl
