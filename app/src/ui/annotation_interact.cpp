// ui/annotation_interact — annotation gesture core (drag-move / corner-resize / rename / create) +
// headless selftest. Paired with annotation_draw.cpp (draw layer) via annotation_internal.h.
// Zone: ui. Precedent: timeline_internal.h split (ARCHITECTURE rule 4).
//
// All gesture functions are defined in sw::ui::ann so their signatures match the extern declarations
// in annotation_internal.h exactly — no ambiguity when annotation_draw.cpp calls them.
#include "ui/annotation_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/annotation_commands.h"
#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"  // MoveChildrenCommand
#include "runtime/annotation.h"
#include "runtime/compound_graph.h"

namespace ed = ax::NodeEditor;

namespace sw::ui::ann {

// ---------------------------------------------------------------------------
// commitGesture
// Push the move/resize as one undo step. The lib currently holds the NEW geometry (we wrote it live
// each frame); the command stores oldGeom (gesture start) so undo reverts. Children that traveled
// get their own MoveChildrenCommand inside the same macro (= TiXL ModifyCanvasElementsCommand moving
// the whole _draggedNodes set as one unit). Resets the lib to old first so doIt's set isn't
// double-logged.
// ---------------------------------------------------------------------------
void commitGesture(Symbol* cur, const std::string& symId, const std::string& annId,
                   float newX, float newY, float newW, float newH) {
  Annotation* a = annById(cur, annId);
  if (!a) return;
  // No real move? (click without drag) — drop, no dead undo entry (= TiXL wasDragging gate).
  bool annMoved = (g_geomStart.x != newX || g_geomStart.y != newY ||
                   g_sizeStart.x != newW || g_sizeStart.y != newH);
  bool kidsMoved = false;
  for (const DragNode& d : g_dragNodes) {
    SymbolChild* c = childById(*cur, d.childId);
    if (c && (c->x != d.ox || c->y != d.oy)) { kidsMoved = true; break; }
  }
  for (const auto& [nid, sp] : g_dragAnns) {
    Annotation* na = annById(cur, nid);
    if (na && (na->x != sp.x || na->y != sp.y)) { kidsMoved = true; break; }
  }
  if (!annMoved && !kidsMoved) return;

  auto macro = std::make_unique<MacroCommand>("Move/Resize Annotation");
  // Reset the lib to the gesture-start state so each command's doIt re-applies the delta exactly once.
  a->x = g_geomStart.x; a->y = g_geomStart.y; a->w = g_sizeStart.x; a->h = g_sizeStart.y;
  macro->add(std::make_unique<MoveResizeAnnotationCommand>(doc::g_lib(), symId, annId,
                                                           newX, newY, newW, newH));
  if (!g_dragNodes.empty()) {
    std::vector<MoveChildrenCommand::Move> moves;
    for (const DragNode& d : g_dragNodes) {
      SymbolChild* c = childById(*cur, d.childId);
      if (!c) continue;
      float nx = c->x, ny = c->y;       // the live (moved) pos
      c->x = d.ox; c->y = d.oy;          // reset so MoveChildrenCommand's doIt applies the delta once
      if (nx != d.ox || ny != d.oy) moves.push_back({d.childId, d.ox, d.oy, nx, ny});
    }
    if (!moves.empty())
      macro->add(std::make_unique<MoveChildrenCommand>(doc::g_lib(), symId, moves));
  }
  // Nested annotations that traveled: one MoveResizeAnnotationCommand each (size unchanged).
  for (const auto& [nid, sp] : g_dragAnns) {
    Annotation* na = annById(cur, nid);
    if (!na) continue;
    float nx = na->x, ny = na->y;
    na->x = sp.x; na->y = sp.y;          // reset so the command applies the delta once
    if (nx != sp.x || ny != sp.y)
      macro->add(std::make_unique<MoveResizeAnnotationCommand>(doc::g_lib(), symId, nid,
                                                               nx, ny, na->w, na->h));
  }
  if (!macro->empty()) {
    g_commands.push(std::move(macro));
    doc::g_status = "moved annotation";
    doc::g_relayout = true;  // children moved in the lib; re-seed canvas node positions
  }
}

// ---------------------------------------------------------------------------
// runDrag
// Run the active drag gesture (write the live lib each frame; commit on release).
// ---------------------------------------------------------------------------
void runDrag(Symbol* cur, const std::string& symId) {
  Annotation* a = annById(cur, g_activeId);
  if (!a) { g_state = State::Default; g_activeId.clear(); return; }
  // Delta in screen -> canvas (divide by scale = multiply by InvScale = GetCurrentZoom()).
  ImVec2 mouse = ImGui::GetMousePos();
  float inv = ed::GetCurrentZoom();
  float dxC = (mouse.x - g_dragMouseStart.x) * inv;
  float dyC = (mouse.y - g_dragMouseStart.y) * inv;
  a->x = g_geomStart.x + dxC;
  a->y = g_geomStart.y + dyC;
  for (const DragNode& d : g_dragNodes) {
    if (SymbolChild* c = childById(*cur, d.childId)) { c->x = d.ox + dxC; c->y = d.oy + dyC; }
  }
  for (const auto& [nid, sp] : g_dragAnns) {
    if (Annotation* na = annById(cur, nid)) { na->x = sp.x + dxC; na->y = sp.y + dyC; }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    bool wasDrag = (dd.x * dd.x + dd.y * dd.y) > kClickThreshSq;
    if (wasDrag) {
      commitGesture(cur, symId, g_activeId, a->x, a->y, a->w, a->h);
    } else {
      // Click without drag = select (AnnotationDragging.cs:165-182): revert the (nil) move, select.
      a->x = g_geomStart.x; a->y = g_geomStart.y;
      for (const DragNode& d : g_dragNodes)
        if (SymbolChild* c = childById(*cur, d.childId)) { c->x = d.ox; c->y = d.oy; }
      for (const auto& [nid, sp] : g_dragAnns)
        if (Annotation* na = annById(cur, nid)) { na->x = sp.x; na->y = sp.y; }
      if (g_selectedId == g_activeId && ImGui::GetIO().KeyShift) g_selectedId.clear();
      else g_selectedId = g_activeId;
    }
    g_state = State::Default;
    g_activeId.clear();
    g_dragNodes.clear();
    g_dragAnns.clear();
  }
}

// ---------------------------------------------------------------------------
// runResize
// ---------------------------------------------------------------------------
void runResize(Symbol* cur, const std::string& symId) {
  Annotation* a = annById(cur, g_activeId);
  if (!a) { g_state = State::Default; g_activeId.clear(); return; }
  ImVec2 mouse = ImGui::GetMousePos();
  ImVec2 cnv = ed::ScreenToCanvas(mouse);
  // Size = newDragPosInCanvas - PosOnCanvas (AnnotationResizing.cs:99), floored to a minimum.
  float nw = std::max(cnv.x - a->x, 20.0f);
  float nh = std::max(cnv.y - a->y, 20.0f);
  a->w = nw; a->h = nh;
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    bool wasDrag = (dd.x * dd.x + dd.y * dd.y) > kClickThreshSq;
    if (wasDrag) {
      commitGesture(cur, symId, g_activeId, a->x, a->y, a->w, a->h);
    } else {
      a->w = g_sizeStart.x; a->h = g_sizeStart.y;  // revert nil resize
      if (g_selectedId == g_activeId && ImGui::GetIO().KeyShift) g_selectedId.clear();
      else g_selectedId = g_activeId;
    }
    g_state = State::Default;
    g_activeId.clear();
  }
}

// ---------------------------------------------------------------------------
// runRename
// Inline rename editor (= AnnotationRenaming.cs): a label single-line + a title multi-line, both
// anchored at the annotation's screen rect. Commits a ChangeAnnotationTextCommand (both fields,
// fork-F) on close if anything changed; Esc cancels.
// ---------------------------------------------------------------------------
void runRename(Symbol* cur, const std::string& symId) {
  Annotation* a = annById(cur, g_activeId);
  if (!a) { g_state = State::Default; g_activeId.clear(); return; }
  ImVec2 pMin = ed::CanvasToScreen(ImVec2(a->x, a->y));

  if (g_renameJustOpened) {
    std::snprintf(g_labelBuf, sizeof(g_labelBuf), "%s", a->label.c_str());
    std::snprintf(g_titleBuf, sizeof(g_titleBuf), "%s", a->title.c_str());
    g_origLabel = a->label;
    g_origTitle = a->title;
    ImGui::SetKeyboardFocusHere();
  }

  ImGui::PushID(a->id.c_str());
  ImGui::SetCursorScreenPos(pMin);
  ImGui::SetNextItemWidth(std::max(ed::CanvasToScreen(ImVec2(a->x + a->w, a->y)).x - pMin.x, 80.0f));
  ImGui::InputTextWithHint("##annLabel", "Label...", g_labelBuf, sizeof(g_labelBuf));
  bool labelActive = ImGui::IsItemActive();
  ImVec2 labelMax = ImGui::GetItemRectMax();

  ImGui::SetCursorScreenPos(ImVec2(pMin.x, labelMax.y));
  ImVec2 titleSize(std::max(ed::CanvasToScreen(ImVec2(a->x + a->w, a->y)).x - pMin.x, 80.0f),
                   std::max(ed::CanvasToScreen(ImVec2(a->x, a->y + a->h)).y - labelMax.y - 3.0f, 24.0f));
  ImGui::InputTextMultiline("##annTitle", g_titleBuf, sizeof(g_titleBuf), titleSize);
  bool titleActive = ImGui::IsItemActive();
  ImGui::PopID();

  // Write the live buffers onto the annotation so the frame previews the edit (= TiXL :96-97).
  a->label = g_labelBuf;
  a->title = g_titleBuf;

  if (g_renameJustOpened) { g_renameJustOpened = false; return; }  // skip the close test on the open frame

  // Close on Esc, or when both fields lose focus (click-out / deactivate).
  bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  bool deactivated = !labelActive && !titleActive && !g_renameJustOpened &&
                     (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                      !ptInRect(ImGui::GetMousePos(), pMin,
                                ed::CanvasToScreen(ImVec2(a->x + a->w, a->y + a->h))));
  if (!esc && !deactivated) return;

  std::string newLabel = g_labelBuf, newTitle = g_titleBuf;
  if (esc) {  // cancel: restore originals, no command
    a->label = g_origLabel;
    a->title = g_origTitle;
  } else if (newLabel != g_origLabel || newTitle != g_origTitle) {
    // Reset to original then push the command (its doIt re-applies the new値 once = clean undo).
    a->label = g_origLabel;
    a->title = g_origTitle;
    auto cmd = std::make_unique<ChangeAnnotationTextCommand>(doc::g_lib(), symId, a->id, newTitle, newLabel);
    if (!cmd->refused()) { g_commands.push(std::move(cmd)); doc::g_status = "renamed annotation"; }
  }
  g_state = State::Default;
  g_activeId.clear();
}

// ---------------------------------------------------------------------------
// applyPendingCreate
// Apply a pending Shift+A create (= NodeActions.AddAnnotation + SetState(Rename)).
// ---------------------------------------------------------------------------
void applyPendingCreate(Symbol* cur, const std::string& symId) {
  if (!g_createPending || !cur) { g_createPending = false; return; }
  g_createPending = false;
  ImVec2 cnv = ed::ScreenToCanvas(ImGui::GetMousePos());
  Annotation a;
  a.id = freshAnnotationId(cur);
  a.x = cnv.x; a.y = cnv.y; a.w = kCreateW; a.h = kCreateH;  // no-selection default (NodeActions.cs:109)
  // (With-selection bbox+Expand is a later refinement — our selection set is single-annotation,
  //  fork-H; a Shift+A always creates the 100x140 default at the mouse for now. Named.)
  auto cmd = std::make_unique<AddAnnotationCommand>(doc::g_lib(), symId, a);
  if (cmd->refused()) return;
  g_commands.push(std::move(cmd));
  doc::g_status = "added annotation";
  // Enter rename immediately (TiXL KeyboardActions.cs:138).
  g_state = State::Rename;
  g_activeId = a.id;
  g_renameJustOpened = true;
  g_selectedId = a.id;
}

}  // namespace sw::ui::ann

namespace sw::ui {

// ---------------------------------------------------------------------------
// Headless isolation test (鐵律5): the pure-geometry helpers this module owns, WITHOUT an imgui
// context. injectBug breaks the contained-in-rect test so a point clearly inside reads as outside.
// Lives here because selftest exercises the gesture-logic helpers (framedChildren point-in-rect,
// create sizing, smootherStep, annotationRectContainedIn) — all used by interact functions.
// ---------------------------------------------------------------------------
int runAnnotationDrawSelfTest(bool injectBug) {
  using namespace ann;
  int fail = 0;

  // 1) framedChildren-style point-in-rect: a child at (50,50) is inside a 0,0..100,140 frame.
  {
    Annotation a; a.x = 0; a.y = 0; a.w = 100; a.h = 140;
    auto pointInside = [&](float px, float py) {
      bool inside = px >= a.x && py >= a.y && px <= a.x + a.w && py <= a.y + a.h;
      if (injectBug) inside = !inside;  // break the test
      return inside;
    };
    if (!pointInside(50, 50)) { std::printf("[anndraw] (50,50) should be inside frame -> FAIL\n"); ++fail; }
    if (pointInside(200, 50)) { std::printf("[anndraw] (200,50) should be outside frame -> FAIL\n"); ++fail; }
  }

  // 2) create sizing constants match TiXL (100x140 default).
  if (kCreateW != 100.0f || kCreateH != 140.0f) {
    std::printf("[anndraw] create size != TiXL 100x140 -> FAIL\n"); ++fail;
  }

  // 3) smootherStep ramp: 0 below edge0, 1 above edge1, monotone in between.
  if (smootherStep(0.25f, 0.6f, 0.1f) != 0.0f) { std::printf("[anndraw] smootherStep below edge0 != 0 -> FAIL\n"); ++fail; }
  if (smootherStep(0.25f, 0.6f, 1.0f) != 1.0f) { std::printf("[anndraw] smootherStep above edge1 != 1 -> FAIL\n"); ++fail; }
  {
    float lo = smootherStep(0.25f, 0.6f, 0.35f), hi = smootherStep(0.25f, 0.6f, 0.5f);
    if (!(hi > lo)) { std::printf("[anndraw] smootherStep not monotone -> FAIL\n"); ++fail; }
  }

  // 4) nested-annotation containment uses the runtime helper (= R-AN geometry shared with combine/copy).
  {
    Annotation outer; outer.x = 0; outer.y = 0; outer.w = 200; outer.h = 200;
    Annotation inner; inner.x = 20; inner.y = 20; inner.w = 40; inner.h = 40;
    bool contained = annotationRectContainedIn(inner, outer.x, outer.y, outer.x + outer.w, outer.y + outer.h);
    if (injectBug) contained = !contained;
    if (!contained) { std::printf("[anndraw] inner annotation should be contained -> FAIL\n"); ++fail; }
  }

  // 5) collapsed frame carries NOTHING (refuter-R-ANB 攻擊5: visual-only collapse must not
  //    drag the expanded area's nodes via a one-line header). Real framedChildren, real Symbol.
  {
    Symbol sym;
    sym.id = "AnnT";
    SymbolChild c; c.id = 1; c.symbolId = "X"; c.x = 50; c.y = 50;
    sym.children.push_back(c);
    Annotation a; a.id = "ann-c1"; a.x = 0; a.y = 0; a.w = 100; a.h = 140;
    a.collapsed = injectBug ? false : true;  // bug face: pretend expanded -> carries -> FAIL
    std::vector<DragNode> out;
    framedChildren(&sym, a, out);
    if (!out.empty()) { std::printf("[anndraw] collapsed frame must carry nothing -> FAIL\n"); ++fail; }
  }

  std::printf("[anndraw] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  // injectBug must make this nonzero (red-proof). Without the bug, fail must be 0.
  return fail;
}

}  // namespace sw::ui
