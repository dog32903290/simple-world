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
//
// Split: gesture core lives in annotation_interact.cpp (rule 4 — was 539 lines).
// Shared state + helper contract: annotation_internal.h (= timeline_internal.h precedent).
#include "ui/annotation_draw.h"
#include "ui/annotation_internal.h"

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

// ---------------------------------------------------------------------------
// Constant + state definitions (extern in annotation_internal.h).
// ---------------------------------------------------------------------------
namespace ann {

const float kRounding      = 8.0f;   // DrawAnnotation.cs:40
const float kHeaderHeight  = 16.0f;  // DrawAnnotation.cs:65
const float kResizeThumb   = 10.0f;  // DrawAnnotation.cs:214
const float kCreateW       = 100.0f; // NodeActions.cs:109
const float kCreateH       = 140.0f;
const float kClickThreshSq = 9.0f;   // ~3px drag threshold²

State       g_state = State::Default;
std::string g_activeId;
std::string g_selectedId;

ImVec2 g_dragMouseStart;
ImVec2 g_dragHandleStart;
ImVec2 g_geomStart;
ImVec2 g_sizeStart;
std::vector<DragNode> g_dragNodes;
std::vector<std::pair<std::string, ImVec2>> g_dragAnns;

char        g_labelBuf[256]  = {0};
char        g_titleBuf[1024] = {0};
std::string g_origTitle, g_origLabel;
bool        g_renameJustOpened = false;

bool g_createPending = false;

// ---------------------------------------------------------------------------
// Shared helper definitions (declared extern in annotation_internal.h).
// ---------------------------------------------------------------------------

Annotation* annById(Symbol* s, const std::string& id) {
  if (!s) return nullptr;
  for (Annotation& a : s->annotations)
    if (a.id == id) return &a;
  return nullptr;
}

float smootherStep(float edge0, float edge1, float x) {
  // TiXL MathUtils.SmootherStep: 6t^5-15t^4+10t^3 over [edge0,edge1], clamped.
  if (edge1 == edge0) return x < edge0 ? 0.0f : 1.0f;
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float currentScale() {
  // TiXL canvas.Scale.X = ViewScale = 1/InvScale; GetCurrentZoom()=InvScale.
  float inv = ed::GetCurrentZoom();
  return (inv > 0.0001f) ? (1.0f / inv) : 1.0f;
}

bool ptInRect(ImVec2 mouse, ImVec2 a, ImVec2 b) {
  return mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y;
}

std::string freshAnnotationId(const Symbol* s) {
  // Mint a fresh annotation id collision-free within the current symbol.
  return uniqueAnnotationId("ann", s ? s->annotations : std::vector<Annotation>{});
}

void framedChildren(const Symbol* cur, const Annotation& a, std::vector<DragNode>& out) {
  // Children of `cur` whose top-left lies inside the annotation rect (= TiXL FindAnnotatedOps).
  out.clear();
  if (!cur) return;
  float x0 = a.x, y0 = a.y, x1 = a.x + a.w, y1 = a.y + a.h;
  for (const SymbolChild& c : cur->children) {
    if (c.id == kSymbolBoundary) continue;
    if (c.x >= x0 && c.y >= y0 && c.x <= x1 && c.y <= y1) out.push_back({c.id, c.x, c.y});
  }
}

void framedAnnotations(const Symbol* cur, const Annotation& a,
                       std::vector<std::pair<std::string, ImVec2>>& out) {
  // Nested annotations fully contained in `a`'s rect (= TiXL FindAnnotatedOps nested loop).
  out.clear();
  if (!cur) return;
  for (const Annotation& b : cur->annotations) {
    if (b.id == a.id) continue;
    if (annotationRectContainedIn(b, a.x, a.y, a.x + a.w, a.y + a.h))
      out.push_back({b.id, ImVec2(b.x, b.y)});
  }
}

}  // namespace ann

// ---------------------------------------------------------------------------
// drawOne — Draw + interact with ONE annotation. `dl` is the suspended window drawlist.
// ---------------------------------------------------------------------------
static void drawOne(Symbol* cur, const std::string& symId, Annotation& a, ImDrawList* dl) {
  using namespace ann;
  const float scale = currentScale();
  const bool collapsed = a.collapsed;
  const float drawH = collapsed ? std::max(kHeaderHeight, 1.0f) / scale : a.h;
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void requestCreateAnnotation() { ann::g_createPending = true; }

bool annotationRenameActive() { return ann::g_state == ann::State::Rename; }

void drawAnnotations(Symbol* cur) {
  using namespace ann;
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

}  // namespace sw::ui
