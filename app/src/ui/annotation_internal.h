// ui/annotation_internal — shared state + helper contract between annotation_draw.cpp (draw) and
// annotation_interact.cpp (gesture core). Internal seam only — do NOT include outside these two TUs.
// Precedent: timeline_internal.h (批次7 S6 split, ARCHITECTURE rule 4).
// Zone: ui (internal header).
#pragma once

#include <string>
#include <vector>

#include "imgui.h"

namespace sw { struct Symbol; struct Annotation; struct SymbolChild; }

namespace sw::ui::ann {

// ---------------------------------------------------------------------------
// TiXL constants (DrawAnnotation.cs / NodeActions.cs / AnnotationDragging.cs).
// Defined in annotation_draw.cpp (one definition); declared here for interact.cpp.
// ---------------------------------------------------------------------------
extern const float kRounding;       // DrawAnnotation.cs:40
extern const float kHeaderHeight;   // DrawAnnotation.cs:65
extern const float kResizeThumb;    // DrawAnnotation.cs:214
extern const float kCreateW;        // NodeActions.cs:109
extern const float kCreateH;
extern const float kClickThreshSq;  // ~3px drag threshold²

// ---------------------------------------------------------------------------
// Gesture / session state (transient view state, never serialised).
// Definitions in annotation_draw.cpp; interact.cpp reads + writes them.
// ---------------------------------------------------------------------------
enum class State { Default, Drag, Resize, Rename };
extern State       g_state;
extern std::string g_activeId;
extern std::string g_selectedId;

extern ImVec2 g_dragMouseStart;
extern ImVec2 g_dragHandleStart;
extern ImVec2 g_geomStart;
extern ImVec2 g_sizeStart;

struct DragNode { int childId; float ox, oy; };
extern std::vector<DragNode> g_dragNodes;
extern std::vector<std::pair<std::string, ImVec2>> g_dragAnns;

extern char        g_labelBuf[256];
extern char        g_titleBuf[1024];
extern std::string g_origTitle, g_origLabel;
extern bool        g_renameJustOpened;

extern bool g_createPending;

// ---------------------------------------------------------------------------
// Shared helpers — definitions in annotation_draw.cpp.
// ---------------------------------------------------------------------------
sw::Annotation* annById(sw::Symbol* s, const std::string& id);
bool ptInRect(ImVec2 mouse, ImVec2 a, ImVec2 b);
std::string freshAnnotationId(const sw::Symbol* s);
void framedChildren(const sw::Symbol* cur, const sw::Annotation& a, std::vector<DragNode>& out);
void framedAnnotations(const sw::Symbol* cur, const sw::Annotation& a,
                       std::vector<std::pair<std::string, ImVec2>>& out);
float smootherStep(float edge0, float edge1, float x);
float currentScale();

// ---------------------------------------------------------------------------
// Gesture functions — definitions in annotation_interact.cpp.
// ---------------------------------------------------------------------------
void commitGesture(sw::Symbol* cur, const std::string& symId, const std::string& annId,
                   float newX, float newY, float newW, float newH);
void runDrag(sw::Symbol* cur, const std::string& symId);
void runResize(sw::Symbol* cur, const std::string& symId);
void runRename(sw::Symbol* cur, const std::string& symId);
void applyPendingCreate(sw::Symbol* cur, const std::string& symId);

}  // namespace sw::ui::ann
