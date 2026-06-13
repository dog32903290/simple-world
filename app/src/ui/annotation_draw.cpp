// ui/annotation_draw — see annotation_draw.h. Zone: ui. Draws + drives the canvas annotation frames
// (= TiXL MagGraphCanvas.DrawAnnotation.cs + AnnotationDragging/Resizing/Renaming.cs). Depends on
// app(annotation_commands/document/command) + runtime + verify(thin eye hook); never the reverse.
//
// fork-G (named): TiXL draws annotations on the canvas drawlist UNDER the node layer
// (MagGraphCanvas.Drawing.cs:120,317). imgui-node-editor splits its drawlist into channels and the
// only screen-space surface where imgui WIDGETS (the InvisibleButton hit-tests + the rename
// InputText) compose with the canvas is inside ed::Suspend/Resume — which lands ON TOP of the node
// layer. We accept on-top here: the bg fill is the faithful translucent tint (op0.7 * Fade0.8 ≈ a
// 0.56-alpha wash) so nodes underneath still read, and a frame that needs to sit UNDER its nodes is
// a visual nicety, not a correctness leg. Same Suspend/Resume招 as quick_add.cpp. If柏為 wants the
// under-layer look later, it moves to a pre-Begin base-drawlist pass (transform is available there,
// cf. drawCanvasBackgroundGrids) — the gesture state below stays put.
#include "ui/annotation_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/annotation_commands.h"
#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"  // MoveChildrenCommand (framed children travel with the frame)
#include "runtime/annotation.h"
#include "runtime/compound_graph.h"
#include "ui/node_style.h"     // annotationBgColor / annotationOutlineColor / annotationTextColor
#include "verify/eye/eye.h"

namespace ed = ax::NodeEditor;

namespace sw::ui {
namespace {

// TiXL constants (DrawAnnotation.cs / NodeActions.cs / AnnotationDragging.cs).
constexpr float kRounding      = 8.0f;   // DrawAnnotation.cs:40 — fixed 8px (NOT zoom-aware, unlike nodes)
constexpr float kHeaderHeight  = 16.0f;  // DrawAnnotation.cs:65 — header click strip = min(16, height)
constexpr float kResizeThumb   = 10.0f;  // DrawAnnotation.cs:214 — 10px corner handle
constexpr float kCreateW       = 100.0f; // NodeActions.cs:109 — default create size when nothing selected
constexpr float kCreateH       = 140.0f;
constexpr float kClickThreshSq = 9.0f;   // ~3px drag threshold² (= TiXL UserSettings ClickThreshold spirit)

// ---- gesture / session state (transient view state, never serialized — = TiXL MagGraph context) ----
enum class State { Default, Drag, Resize, Rename };
State       g_state = State::Default;
std::string g_activeId;        // the annotation a gesture / rename is bound to
std::string g_selectedId;      // the selected annotation (our standalone selection set, fork-H)

// Drag/resize live values (the lib is written each frame so the frame follows the mouse; the
// undoable command is pushed only on release with a real delta — mirrors editor_ui's node drag).
ImVec2 g_dragMouseStart;       // screen mouse pos when the gesture began
ImVec2 g_dragHandleStart;      // canvas pos/size the gesture started from
ImVec2 g_geomStart;            // the annotation's (x,y) at gesture start (for the undo command's old値)
ImVec2 g_sizeStart;            // the annotation's (w,h) at gesture start
struct DragNode { int childId; float ox, oy; };       // a framed child carried by a drag-move
std::vector<DragNode> g_dragNodes;                     // children whose pos travels with the frame
std::vector<std::pair<std::string, ImVec2>> g_dragAnns;// nested annotations carried (id -> start pos)

// Rename buffers (= AnnotationRenaming.cs _labelBuffer/_titleBuffer + _originalTitle/_originalLabel).
char        g_labelBuf[256] = {0};
char        g_titleBuf[1024] = {0};
std::string g_origTitle, g_origLabel;
bool        g_renameJustOpened = false;

// Pending create (Shift+A) — deferred to the next drawAnnotations so the mouse canvas pos + the
// rename seed are read in this module (= TiXL KeyboardActions.cs:136-138 AddAnnotation then SetState).
bool g_createPending = false;

Annotation* annById(Symbol* s, const std::string& id) {
  if (!s) return nullptr;
  for (Annotation& a : s->annotations)
    if (a.id == id) return &a;
  return nullptr;
}

// SmootherStep (TiXL MathUtils.SmootherStep): 6t^5-15t^4+10t^3 over the [edge0,edge1] ramp, clamped.
float smootherStep(float edge0, float edge1, float x) {
  if (edge1 == edge0) return x < edge0 ? 0.0f : 1.0f;
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float currentScale() {  // TiXL canvas.Scale.X = ViewScale = 1/InvScale; GetCurrentZoom()=InvScale.
  float inv = ed::GetCurrentZoom();
  return (inv > 0.0001f) ? (1.0f / inv) : 1.0f;
}

// Is `mouse` (screen) inside the screen rect [a,b]?
bool ptInRect(ImVec2 mouse, ImVec2 a, ImVec2 b) {
  return mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y;
}

// Mint a fresh annotation id collision-free within the current symbol (the runtime has no Guid gen;
// uniqueAnnotationId derives "ann-cN" off a base — we seed with "ann" so created ids read as ann-cN).
std::string freshAnnotationId(const Symbol* s) {
  return uniqueAnnotationId("ann", s ? s->annotations : std::vector<Annotation>{});
}

// Children of `cur` whose top-left point lies inside the annotation rect [ax,ay]..[ax+w,ay+h]
// (= TiXL FindAnnotatedOps aRect.Contains(nRect); our runtime child has no Size so we test the
// point — same招 combine/copy use, named in copy_paste.cpp). Output: (childId, x, y).
void framedChildren(const Symbol* cur, const Annotation& a, std::vector<DragNode>& out) {
  out.clear();
  if (!cur) return;
  float x0 = a.x, y0 = a.y, x1 = a.x + a.w, y1 = a.y + a.h;
  for (const SymbolChild& c : cur->children) {
    if (c.id == kSymbolBoundary) continue;
    if (c.x >= x0 && c.y >= y0 && c.x <= x1 && c.y <= y1) out.push_back({c.id, c.x, c.y});
  }
}

// Nested annotations fully contained in `a`'s rect (= TiXL FindAnnotatedOps nested loop).
void framedAnnotations(const Symbol* cur, const Annotation& a,
                       std::vector<std::pair<std::string, ImVec2>>& out) {
  out.clear();
  if (!cur) return;
  for (const Annotation& b : cur->annotations) {
    if (b.id == a.id) continue;
    if (annotationRectContainedIn(b, a.x, a.y, a.x + a.w, a.y + a.h))
      out.push_back({b.id, ImVec2(b.x, b.y)});
  }
}

// Push the move/resize as one undo step. The lib currently holds the NEW geometry (we wrote it live
// each frame); the command stores oldGeom (gesture start) so undo reverts. Children that traveled get
// their own MoveChildrenCommand inside the same macro (= TiXL ModifyCanvasElementsCommand moving the
// whole _draggedNodes set as one unit). Resets the lib to old first so doIt's set isn't double-logged.
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
  macro->add(std::make_unique<MoveResizeAnnotationCommand>(doc::g_lib, symId, annId,
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
      macro->add(std::make_unique<MoveChildrenCommand>(doc::g_lib, symId, moves));
  }
  // Nested annotations that traveled: one MoveResizeAnnotationCommand each (size unchanged).
  for (const auto& [nid, sp] : g_dragAnns) {
    Annotation* na = annById(cur, nid);
    if (!na) continue;
    float nx = na->x, ny = na->y;
    na->x = sp.x; na->y = sp.y;          // reset so the command applies the delta once
    if (nx != sp.x || ny != sp.y)
      macro->add(std::make_unique<MoveResizeAnnotationCommand>(doc::g_lib, symId, nid,
                                                               nx, ny, na->w, na->h));
  }
  if (!macro->empty()) {
    g_commands.push(std::move(macro));
    doc::g_status = "moved annotation";
    doc::g_relayout = true;  // children moved in the lib; re-seed canvas node positions
  }
}

// Draw + interact with ONE annotation. `dl` is the suspended window drawlist (screen space).
void drawOne(Symbol* cur, const std::string& symId, Annotation& a, ImDrawList* dl) {
  const float scale = currentScale();
  const bool collapsed = a.collapsed;
  const float drawH = collapsed ? std::max(kHeaderHeight, 1.0f) / scale : a.h;  // collapsed = single header strip in canvas units
  // Canvas->screen rect.
  ImVec2 pMin = ed::CanvasToScreen(ImVec2(a.x, a.y));
  ImVec2 pMax = ed::CanvasToScreen(ImVec2(a.x + a.w, a.y + (collapsed ? drawH : a.h)));
  const bool selected = (a.id == g_selectedId);

  // --- frame fill + border (DrawAnnotation.cs:38-59) ---
  ImDrawFlags rcFlags = ImDrawFlags_RoundCornersTop | ImDrawFlags_RoundCornersBottomLeft;
  dl->AddRectFilled(ImVec2(pMin.x + 1, pMin.y + 1), pMax, annotationBgColor(a.color), kRounding, rcFlags);
  dl->AddRect(pMin, pMax, annotationOutlineColor(a.color, selected), kRounding, rcFlags, 1.0f);

  // --- header click strip (DrawAnnotation.cs:64-65) ---
  ImVec2 hdrMin = pMin;
  ImVec2 hdrMax = ImVec2(pMax.x, std::min(pMin.y + kHeaderHeight, pMax.y));

  // Hover highlight on the header (DrawAnnotation.cs:139-143 ForegroundFull.Fade(0.1)).
  bool headerHovered = (g_state == State::Default) && ptInRect(ImGui::GetMousePos(), hdrMin, hdrMax) &&
                       ImGui::IsWindowHovered();
  if (headerHovered)
    dl->AddRectFilled(hdrMin, hdrMax, IM_COL32(255, 255, 255, (int)(0.1f * 255)), kRounding,
                      ImDrawFlags_RoundCornersTop);

  // --- label + title text (DrawAnnotation.cs:161-204), suppressed while THIS frame renames ---
  bool renamingThis = (g_state == State::Rename && a.id == g_activeId);
  if (!renamingThis && !collapsed) {
    float labelH = 0.0f;
    if (!a.label.empty()) {
      float fade = smootherStep(0.1f, 0.2f, scale) * 0.8f;
      dl->AddText(ImVec2(pMin.x + 18, pMin.y + 3), annotationTextColor(a.color, fade), a.label.c_str());
      labelH = ImGui::GetTextLineHeight();
    }
    if (!a.title.empty()) {
      bool big = a.title.rfind("# ", 0) == 0;  // "# " prefix = large font (we have one font; tint only)
      (void)big;
      float fade = smootherStep(0.25f, 0.6f, scale) * 0.8f;
      dl->PushClipRect(pMin, pMax, true);
      dl->AddText(ImVec2(pMin.x + 8, pMin.y + 8 + labelH), annotationTextColor(a.color, fade),
                  a.title.c_str());
      dl->PopClipRect();
    }
  } else if (!renamingThis && collapsed && !a.label.empty()) {
    // Collapsed: show just the label on the header strip.
    float fade = smootherStep(0.1f, 0.2f, scale) * 0.8f;
    dl->AddText(ImVec2(pMin.x + 18, pMin.y + 1), annotationTextColor(a.color, fade), a.label.c_str());
  }

  // --- resize triangle (DrawAnnotation.cs:206-228) bottom-right ---
  if (!collapsed) {
    ImVec2 c = pMax;
    dl->AddTriangleFilled(ImVec2(c.x - 11, c.y - 1), ImVec2(c.x - 1, c.y - 11), ImVec2(c.x - 1, c.y - 1),
                          IM_COL32(80, 80, 90, 220));  // ~UiColors.BackgroundButton
  }

  // --- collapse chevron (DrawAnnotation.cs:75): small button top-left ---
  ImGui::PushID(a.id.c_str());
  ImGui::SetCursorScreenPos(ImVec2(pMin.x + 2, pMin.y + 1));
  if (ImGui::InvisibleButton("##chevron", ImVec2(14, 14))) {
    a.collapsed = !a.collapsed;
    doc::g_status = a.collapsed ? "collapsed annotation" : "expanded annotation";
  }
  // Draw the chevron glyph as a tiny triangle (no icon font): right=collapsed, down=expanded.
  {
    ImVec2 cp = ImVec2(pMin.x + 4, pMin.y + 3);
    ImU32 ch = IM_COL32(220, 220, 230, 200);
    if (a.collapsed)  // ▶
      dl->AddTriangleFilled(ImVec2(cp.x, cp.y), ImVec2(cp.x, cp.y + 8), ImVec2(cp.x + 6, cp.y + 4), ch);
    else              // ▼
      dl->AddTriangleFilled(ImVec2(cp.x, cp.y), ImVec2(cp.x + 8, cp.y), ImVec2(cp.x + 4, cp.y + 6), ch);
  }

  // --- header InvisibleButton (drag / select / double-click rename) ---
  // Placed AFTER the chevron so the chevron wins its 14px corner. The header strip from x+18 right.
  if (g_state == State::Default) {
    ImVec2 hdrBtnMin = ImVec2(pMin.x + 18, pMin.y);
    float hw = std::max(hdrMax.x - hdrBtnMin.x, 1.0f);
    float hh = std::max(hdrMax.y - hdrBtnMin.y, 1.0f);
    ImGui::SetCursorScreenPos(hdrBtnMin);
    ImGui::InvisibleButton("##hdr", ImVec2(hw, hh));
    bool hdrHov = ImGui::IsItemHovered();
    if (hdrHov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      // Double-click -> rename (DrawAnnotation.cs:153-159).
      g_state = State::Rename;
      g_activeId = a.id;
      g_renameJustOpened = true;
    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt) {
      // Begin drag (DrawAnnotation.cs:146-150 -> AnnotationDragging start).
      g_state = State::Drag;
      g_activeId = a.id;
      g_dragMouseStart = ImGui::GetMousePos();
      g_geomStart = ImVec2(a.x, a.y);
      g_sizeStart = ImVec2(a.w, a.h);
      // Build the carried set (AnnotationDragging.cs:49-60): if this frame is already selected we
      // would carry the whole selection; we have a single-annotation selection set (fork-H), so a
      // selected frame carries only itself + (unless Cmd held) its geometrically-framed children.
      g_dragNodes.clear();
      g_dragAnns.clear();
      if (!ImGui::GetIO().KeyCtrl) {  // io.KeyCtrl == Mac Cmd (ConfigMacOSX swap, fork-E); Cmd = frame-only
        framedChildren(cur, a, g_dragNodes);
        framedAnnotations(cur, a, g_dragAnns);
      }
    }
  }

  // --- resize InvisibleButton (corner) ---
  if (g_state == State::Default && !collapsed) {
    ImVec2 rzMin = ImVec2(pMax.x - kResizeThumb, pMax.y - kResizeThumb);
    ImGui::SetCursorScreenPos(rzMin);
    ImGui::InvisibleButton("##resize", ImVec2(kResizeThumb, kResizeThumb));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      g_state = State::Resize;
      g_activeId = a.id;
      g_dragMouseStart = ImGui::GetMousePos();
      g_geomStart = ImVec2(a.x, a.y);
      g_sizeStart = ImVec2(a.w, a.h);
    }
  }
  ImGui::PopID();

  // eye hook (契約5): one line — annotation rect into the map (under nodes, key ann:<id>).
  sw::eye::recordRect(("ann:" + a.id).c_str(), pMin.x, pMin.y, pMax.x, pMax.y);
}

// Run the active drag gesture (write the live lib each frame; commit on release).
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

// Inline rename editor (= AnnotationRenaming.cs): a label single-line + a title multi-line, both
// anchored at the annotation's screen rect. Commits a ChangeAnnotationTextCommand (both fields,
// fork-F) on close if anything changed; Esc cancels.
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
    auto cmd = std::make_unique<ChangeAnnotationTextCommand>(doc::g_lib, symId, a->id, newTitle, newLabel);
    if (!cmd->refused()) { g_commands.push(std::move(cmd)); doc::g_status = "renamed annotation"; }
  }
  g_state = State::Default;
  g_activeId.clear();
}

// Apply a pending Shift+A create (= NodeActions.AddAnnotation + SetState(Rename)).
void applyPendingCreate(Symbol* cur, const std::string& symId) {
  if (!g_createPending || !cur) { g_createPending = false; return; }
  g_createPending = false;
  ImVec2 cnv = ed::ScreenToCanvas(ImGui::GetMousePos());
  Annotation a;
  a.id = freshAnnotationId(cur);
  a.x = cnv.x; a.y = cnv.y; a.w = kCreateW; a.h = kCreateH;  // no-selection default (NodeActions.cs:109)
  // (With-selection bbox+Expand is a later refinement — our selection set is single-annotation,
  //  fork-H; a Shift+A always creates the 100x140 default at the mouse for now. Named.)
  auto cmd = std::make_unique<AddAnnotationCommand>(doc::g_lib, symId, a);
  if (cmd->refused()) return;
  g_commands.push(std::move(cmd));
  doc::g_status = "added annotation";
  // Enter rename immediately (TiXL KeyboardActions.cs:138).
  g_state = State::Rename;
  g_activeId = a.id;
  g_renameJustOpened = true;
  g_selectedId = a.id;
}

}  // namespace

void requestCreateAnnotation() { g_createPending = true; }

bool annotationRenameActive() { return g_state == State::Rename; }

void drawAnnotations(Symbol* cur) {
  if (!cur) return;
  const std::string symId = doc::currentSymbolId();

  // All annotation draw + interaction in screen space over the canvas (ed::Suspend/Resume, fork-G).
  ed::Suspend();

  applyPendingCreate(cur, symId);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Draw every annotation (a copy of the id list first: gestures can push commands that mutate the
  // vector mid-loop — iterate by id to stay valid).
  std::vector<std::string> ids;
  ids.reserve(cur->annotations.size());
  for (const Annotation& a : cur->annotations) ids.push_back(a.id);
  for (const std::string& id : ids) {
    Annotation* a = annById(cur, id);
    if (a) drawOne(cur, symId, *a, dl);
  }

  // Run the active gesture AFTER drawing (so the InvisibleButtons above could start one this frame).
  switch (g_state) {
    case State::Drag:   runDrag(cur, symId);   break;
    case State::Resize: runResize(cur, symId); break;
    case State::Rename: runRename(cur, symId); break;
    case State::Default: break;
  }

  ed::Resume();
}

// ---------------------------------------------------------------------------
// Headless isolation test (鐵律5): the pure-geometry helpers this module owns, WITHOUT an imgui
// context. injectBug breaks the contained-in-rect test so a point clearly inside reads as outside.
// ---------------------------------------------------------------------------
int runAnnotationDrawSelfTest(bool injectBug) {
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

  std::printf("[anndraw] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  // injectBug must make this nonzero (red-proof). Without the bug, fail must be 0.
  return fail;
}

}  // namespace sw::ui
