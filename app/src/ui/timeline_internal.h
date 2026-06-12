// ui/timeline_internal — shared state/geometry/pending-action contract between the timeline's
// views (S6 split along the view seam, ARCHITECTURE rule 4):
//   timeline_window.cpp       window shell: lanes, ruler, playhead, zoom/pan, context menu
//   timeline_dopesheet.cpp    dope-sheet view (lanes + key diamonds + fence + multi-drag)
//   timeline_curve_editor.cpp curve-editor view (value axis + curve lines + 2-axis drag + tangents)
//   timeline_select.cpp       selection state + helpers (session singleton)
//   timeline_edit.cpp         gesture core + executePending (the ONLY place curves mutate)
//   timeline_canvas.cpp       ScalableCanvas/ValueSnapHandler math ports (damping, zoom, snap)
//   timeline_raster.cpp       BeatTimeRaster port (zoom-adaptive bar/beat/tick ruler ladder)
//   timeline_selftest.cpp     --selftest-timeline teeth on the exported gesture-core seams
//
// 鐵律（批次7 BUG-B 前科）：視圖迴圈裡迭代 curve.table() 時絕不 addOrUpdate/removeAt——視圖只
// 「記錄」手勢進 Pending，timeline_edit::executePending 在所有迭代器收掉之後才執行。In-place 改
// VDefinition 欄位（value/tangent）不動 map 結構，但為了一條律全走 Pending。
// Zone: ui (internal header — only the timeline_*.cpp TUs include this).
#pragma once
#include <string>
#include <vector>

#include "imgui.h"

#include "runtime/compound_graph.h"
#include "runtime/curve.h"

namespace sw {
class CommandStack;  // app/command.h — timeline_edit pushes undoable gesture commands onto it
}

namespace sw::ui::tl {

// One animated channel to draw: which (child,input) + array index, plus its resolved label.
struct Lane {
  int childId;
  std::string inputId;
  int index;
  std::string label;
};

// A selected key, identified by (lane, rounded time) — session-only, like node selection.
// 殘留限制具名 (修5): (childId,inputId,index,roundTime) is NOT a stable key identity — a drag that
// clobbers an unselected key, or an undo that reorders the table, can leave two entries naming the
// same key. Guards in place: drag re-applies are RIGID (no intra-selection merge, timeline_edit),
// pruneSelection dedupes every frame, runDeleteSelected counts UNIQUE existing keys, and the
// window clears the selection on composition switch (cross-composition leak = refuter 盲區3).
struct SelKey {
  int childId = -1;
  std::string inputId;
  int index = 0;
  double time = 0.0;
};

// View transform state. Time axis = TiXL ScalableCanvas Scale.X/Scroll.X. Wheel zoom factor =
// ComputeZoomDeltaFromMouseWheel (ScalableCanvas.cs:453-477): integer 1.2 steps, factor clamped
// [0.02,100]; the resulting Scale.X is clamped [0.01,5000] (TimeLineCanvas branch of
// ClampScaleToValidRange, ScalableCanvas.cs:303-311). Value axis only meaningful in curve mode;
// TiXL's curve canvas skips the scale clamp entirely (IsCurveCanvas early-out) — our pxPerUnit
// clamp [1e-4,1e6] is a 具名 fork (NaN guard).
// Damped canvas (= TiXL Scale/Scroll vs ScaleTarget/ScrollTarget, ScalableCanvas.cs:182-188):
// input handlers write the *T targets; dampView eases the drawn values toward them every frame
// (DampScaling, cs:190-235). All geometry/transforms read the DRAWN values.
struct ViewState {
  double pxPerBar = 40.0;     // = TiXL Scale.X (px per bar, drawn/damped)
  double scrollBars = 0.0;    // = TiXL Scroll.X (bars at the content left edge, drawn/damped)
  double pxPerBarT = 40.0;    // = TiXL ScaleTarget.X
  double scrollBarsT = 0.0;   // = TiXL ScrollTarget.X
  bool curveMode = false;     // = TiXL TimeLineCanvas.Modes DopeView/CurveEditor
  double pxPerUnit = 40.0;    // curve view: px per value unit (= Scale.Y, drawn/damped)
  double valueBottom = -1.0;  // curve view: value at the content BOTTOM edge (drawn/damped)
  double pxPerUnitT = 40.0;   // = TiXL ScaleTarget.Y
  double valueBottomT = -1.0; // = TiXL ScrollTarget.Y
  bool needFit = true;        // refit V on mode entry / lane-set change (TimelineCurveEditor.cs:47-56)
  int lastLaneCount = -1;
};

// Multi-key drag in flight (= TiXL StartDragCommand/UpdateDragCommand/CompleteDragCommand,
// DopeSheetArea.cs:1051-1083). `keys` snapshots the dragged set with ORIGINAL time/value; each
// frame executePending restores `before` and re-applies (dt,dv) — no incremental drift.
struct DragKey {
  SelKey key;
  double startTime = 0.0;
  double startValue = 0.0;
  VDefinition def;  // full pre-drag keyframe (interp/tangents ride along the move)
};
struct GroupSnap { int childId = 0; std::string inputId; Animator::CurveArray before; };
struct DragState {
  bool active = false;
  int axis = 0;            // 0 undecided / 1 horizontal(time) / 2 vertical(value) — TiXL
                           // CurveInputEditing.MoveDirections latch (TimelineCurveEditor.cs:439-449)
  ImVec2 mouseStart{};
  double refStartTime = 0.0;  // grabbed key's ORIGINAL time — the snap reference (= TiXL snaps the
                              // dragged vDef's U, DopeSheetArea.cs:925-936)
  std::vector<DragKey> keys;
  std::vector<GroupSnap> before;
};

// Tangent-handle drag in flight (curve view; one key+side at a time = TiXL CurvePoint.cs).
struct TangentDrag {
  bool active = false;
  SelKey key;
  bool inSide = false;
  std::vector<GroupSnap> before;  // the single affected group
};

// Rubber-band fence (= TiXL SelectionFence; modes Replace/Add/Remove from modifiers at start).
struct Fence { bool active = false; ImVec2 start{}, end{}; int mode = 0; };  // 0=replace 1=add 2=remove

// What the views RECORDED this frame. Executed by executePending after all iteration closed.
// (Drag/tangent liveness is NOT recorded here — views only stageDrag/stageTangentDrag; the
// executor drives update/finish from raw mouse-down, because item ids are unstable mid-drag.)
struct Pending {
  bool addKey = false; SelKey addAt{};  // double-click empty lane
  std::vector<SelKey> insertKeys;       // curve-view double-click: one key per visible lane curve
                                        // (= TiXL "Insert keyframes" macro, TimelineCurveEditor.cs)
  bool deleteSelected = false;          // Delete/Backspace or context menu
  int setInterp = -1;                   // KeyInterpolation int (context menu)
};

struct State {
  ViewState view;
  std::vector<SelKey> selection;
  DragState drag;
  TangentDrag tan;
  Fence fence;
  Pending pending;
  // Set when a key click was a pure cmd-deselect: that press must not turn into a drag
  // (TiXL UpdateSelectionOnClickOrDrag's early-return, DopeSheetArea.cs:947-957). Cleared on release.
  bool suppressDragFromClick = false;
  // 修5: selection scope guard — the window clears `selection` whenever the composition changes,
  // so (childId,inputId) pairs never leak into a foreign symbol's lanes.
  std::string lastSymbolId;
  // Snap indicator (= TiXL ValueSnapHandler._lastSnapTime/_lastSnapValue): the window draws a
  // 1s-fading vertical line at snapBars while ImGui::GetTime() - snapStamp < 1 (DrawSnapIndicator).
  double snapStamp = -1e9;
  double snapBars = 0.0;
};
State& state();  // session singleton (timeline_select.cpp)

// Per-frame content geometry + the time/value <-> screen transforms both views share.
struct Geom {
  float x0 = 0, x1 = 0;  // content rect horizontal (right of the label gutter)
  float y0 = 0, y1 = 0;  // lanes / curve area vertical
  double pxPerBar = 40.0, scrollBars = 0.0;
  double pxPerUnit = 40.0, valueBottom = 0.0;
  float timeToX(double bars) const { return x0 + (float)((bars - scrollBars) * pxPerBar); }
  double xToTime(float x) const { return scrollBars + (double)(x - x0) / pxPerBar; }
  float valueToY(double v) const { return y1 - (float)((v - valueBottom) * pxPerUnit); }
  double yToValue(float y) const { return valueBottom + (double)(y1 - y) / pxPerUnit; }
};

constexpr float kLaneH = 22.0f;   // per-lane height (dope view)
constexpr float kKeyR = 5.0f;     // key diamond half-size (hit + draw)
constexpr float kDragLatchPx = 4.0f;  // axis-latch threshold (= TiXL MoveDirectionThreshold风格)

// --- selection helpers (timeline_select.cpp) ---
bool isSelected(const State& s, int childId, const std::string& inputId, int index, double time);
// = TiXL DopeSheetArea.UpdateSelectionOnClickOrDrag (cs:941-974): cmd(io.KeyCtrl)=deselect,
// shift=add, plain=replace-unless-already-selected. Returns true if the click was a pure
// deselect (caller must NOT start a drag from it).
bool selectOnClickOrDrag(State& s, const SelKey& k, bool alreadySelected, bool shift, bool cmd);
// Drop duplicate entries naming the same (lane, rounded time) — merge/clobber ghosts (修1②/修5).
void dedupeSelection(std::vector<SelKey>& sel);
// Drop selection entries whose key no longer exists on the animator (undo/load can orphan them),
// then dedupe (one key = one entry, always).
void pruneSelection(State& s, const Symbol& sym);
// Stage a key drag: fills drag.keys (selection w/ original time+value) + before snapshots.
// `grab` = the key whose button started the drag — recorded as drag.refStartTime, the time the
// snap handler pulls toward beats/keys (= TiXL's dragged vDef). nullptr -> first staged key.
void stageDrag(State& s, const Symbol& sym, ImVec2 mouseStart, const SelKey* grab = nullptr);
// Stage a tangent drag on one key (curve view).
void stageTangentDrag(State& s, const Symbol& sym, const SelKey& k, bool inSide);

// --- gesture core (timeline_edit.cpp / timeline_window.cpp) ---
// Pure of imgui INPUT state (the views/executor compute dt/dv/angle from the mouse) so
// --selftest-timeline can bite the real mutation code headless.
//
// Rigid-group translation: restore the pre-drag arrays, then re-apply ONE (dt,dv) offset to every
// staged key. dt is clamped ONCE for the whole group so min(selected times)+dt >= 0 — relative
// spacing is preserved, selected keys can never merge into each other (修1①). Returns the dt
// actually applied. FORK 具名 "rigid clamp at 0": TiXL applies the offset unclamped
// (UpdateDragCommand: U += dt, DopeSheetArea.cs:1056-1066); our timeline starts at 0 (既定).
double applyDragOffset(State& s, Symbol& sym, double dt, double dv);
// Complete a key drag: push ONE SetCurveGroupSnapshotCommand (before/after) onto `stack`.
void finishDrag(State& s, SymbolLibrary& lib, CommandStack& stack, const std::string& symbolId,
                Symbol& sym);
// Tangent-drag write on one key's side (= TiXL CurvePoint.HandleTangentDrag write phase,
// CurvePoint.cs:176-300): side -> Tangent + angle, cmd breaks, mirror when linked, broken
// promotes the OPPOSITE side Linear -> Tangent (cs:289-298, 修3). Deliberately NO updateTangents
// (修2): TiXL never calls UpdateTangents mid-drag, and ours unconditionally re-mirrors boundary
// keys (curve.cpp updateTangents), which would punch the authored broken angle right back.
void applyTangentDrag(Curve& c, double keyTime, bool inSide, double angle, bool breakTangents);
// Delete the selected keys (= TiXL AnimationOperations.DeleteSelectedKeyframesFromAnimation-
// Parameters, cs:57-99): all keys of an input selected -> RemoveAnimation (driver back to
// Constant), else per-key delete. Counts UNIQUE EXISTING selected keys so duplicate/ghost
// selection entries can never misroute a partial delete into RemoveAnimation (修1③).
void runDeleteSelected(State& s, SymbolLibrary& lib, CommandStack& stack,
                       const std::string& symbolId, Symbol& sym);
// Insert one keyframe per entry (curve-view double-click; = TiXL HandleCreateNewKeyframes ->
// "Insert keyframes" MacroCommand, TimelineCurveEditor.cs:299-348). Each key lands ON the curve
// (sampled value — AddKeyframeCommand semantics), Linear; selection untouched (TiXL parity).
void runInsertKeys(SymbolLibrary& lib, CommandStack& stack, const std::string& symbolId,
                   const std::vector<SelKey>& at);
// = TiXL ScalableCanvas.ComputeZoomDeltaFromMouseWheel (ScalableCanvas.cs:453-477): integer-step
// 1.2 loop (a fractional notch counts as one full step), clamped [0.02,100] (修4).
double zoomDeltaFromWheel(float wheel);

// --- canvas damping + snap (timeline_canvas.cpp; pure of imgui so the selftest bites them) ---
// = TiXL ScalableCanvas.DampScaling (ScalableCanvas.cs:190-235): ease the drawn pxPerBar/scroll/
// pxPerUnit/valueBottom toward their *T targets by lerping the viewport EXTENTS in canvas space,
// f = min(dt / ScrollSmoothing 0.06 (UserSettings.cs:98), 1). Completed (snap-to-target) when
// pxPerBar > 1000 OR |scroll-target| < 1 (both axes) AND |scale-target| < 0.05 (both axes).
void dampView(ViewState& v, double widthPx, double heightPx, double dt);
// = TiXL SnapResult.TryToImproveWithAnchorValue: threshold = SnapStrength 5px (UserSettings.cs:95)
// / pxPerBar, force = threshold - |anchor-target|, forces < 1e-5 rejected, best force wins.
// `snappingDisabled` = Shift held (DopeSheetArea.cs:927: snap only when !KeyShift). Returns the
// snapped time (or `target` untouched); *didSnap reports whether an anchor won.
double snapDragTime(double target, double pxPerBar, const std::vector<double>& anchors,
                    bool snappingDisabled, bool* didSnap = nullptr);
// Anchor set for time snapping (= TiXL SnapHandlerForU attractors, TimeLineCanvas.cs:48-54):
// visible raster ticks + playhead + every keyframe time (excludeSelected drops the dragged
// selection = TiXL SelectionDragSnapExclusions).
void collectSnapAnchors(const Symbol& sym, const State& s, const Geom& g, bool excludeSelected,
                        std::vector<double>& out);

// --- adaptive ruler raster (timeline_raster.cpp) ---
// = TiXL BeatTimeRaster (BeatTimeRaster.cs): zoom ladder of bar/beat/16th-tick rasters picked by
// invertedScale = 1/pxPerBar; the finest level fades out as you zoom away (fadeFactor =
// 1 - remap(invertedScale, ScaleMin, ScaleMax)). Labels: "b"=bar, "b.b"=bar.beat, ":t"=tick.
// Ticks are px-deduped (TiXL _usedPositions) and double as the raster's snap anchors.
struct RasterTick {
  double bars = 0.0;
  float lineAlpha = 1.0f;   // 0..1, multiplied into the grid-line color
  float labelAlpha = 1.0f;  // 0..1 for the label text (label[0]=='\0' -> no label)
  char label[16] = {0};
};
void computeRaster(double pxPerBar, double scrollBars, double widthPx, std::vector<RasterTick>& out);

// --- views (record-only; NO curve mutation inside) ---
void drawDopeSheet(Symbol& sym, const std::vector<Lane>& lanes, const Geom& g, ImDrawList* dl);
void drawCurveEditor(Symbol& sym, const std::vector<Lane>& lanes, const Geom& g, ImDrawList* dl);

// Execute everything recorded in state().pending; pushes undoable commands (timeline_edit.cpp).
void executePending(const std::string& symbolId, Symbol& sym, const Geom& g);

}  // namespace sw::ui::tl
