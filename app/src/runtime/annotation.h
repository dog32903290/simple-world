// runtime/annotation — the Annotation data model (= TiXL Editor/UiModel/Annotation.cs). A pure
// UI/edit-time object: a titled frame on the canvas that visually groups a region of nodes. It
// DOES NOT participate in evaluation — cook/Slot/DirtyFlag never touch it (契約 0). So it is a
// runtime LEAF: pure CPU data, zero imgui, zero upward deps (ARCHITECTURE.md runtime rule).
//
// Faithful to TiXL Annotation.cs:7-15 — a FLAT struct (id + two text fields + color + pos/size +
// collapsed). It does NOT store a list of contained children: "which nodes are framed" is a live
// GEOMETRIC query made at drag/draw time (rect contains rect, AnnotationDragging.cs:193-233), never
// a persisted membership. The ONLY persistent ownership flag is `CollapsedIntoAnnotationFrameId` on
// the CHILD (written on collapse) — that lives on SymbolChild's UI side, not here (批 B/C territory).
#pragma once
#include <string>
#include <vector>

namespace sw {

// UiColors.Gray (external/tixl Editor/Gui/Styling/UiColors.cs:51 = new(0.6,0.6,0.6,1)). The default
// frame color an annotation is born with. Kept as a named constant so the save/load omission rule
// ("a default-gray color is omitted") and the struct default agree on ONE source of truth.
constexpr float kAnnotationGrayRGBA[4] = {0.6f, 0.6f, 0.6f, 1.0f};

// One canvas annotation frame (= TiXL Annotation). `title` = the description body ("# " prefix bumps
// to a large font at draw time, 批 C); `label` = the small heading. Both are free user text and MAY
// hold CJK/emoji — crude_json dumps/parses raw UTF-8 byte-stable (sw-patch utf8, 批次4), so the
// roundtrip survives (golden in annotation_save_selftest covers a 中文 title). Color/pos/size mirror
// TiXL's Color/PosOnCanvas/Size. `collapsed` folds the framed nodes (the fold flag itself lives on
// the child, 批 B/C — this bool is only the annotation's own collapsed state).
struct Annotation {
  std::string id;        // stable UUID (= TiXL Guid)
  std::string title;     // description body ("# " prefix = large font at draw)
  std::string label;     // small heading
  float color[4] = {kAnnotationGrayRGBA[0], kAnnotationGrayRGBA[1], kAnnotationGrayRGBA[2],
                    kAnnotationGrayRGBA[3]};  // RGBA, default gray
  float x = 0.0f, y = 0.0f;  // PosOnCanvas
  float w = 0.0f, h = 0.0f;  // Size
  bool collapsed = false;
};

// Is this annotation's color the (omittable) default gray? (= the save-side "省略預設灰" gate, kept
// here so save/load share one definition of "default".)
bool annotationColorIsDefault(const Annotation& a);

// Is the annotation's rect [x,y]..[x+w,y+h] fully CONTAINED in the axis-aligned box
// [bx0,by0]..[bx1,by1]? (= TiXL aRect.Contains(nRect) spirit, AnnotationDragging.cs:193-233, the
// "is this NESTED annotation inside that frame" case). Inclusive on all four edges. Pure geometry.
bool annotationRectContainedIn(const Annotation& a, float bx0, float by0, float bx1, float by1);

// Does the annotation's rect [x,y]..[x+w,y+h] CONTAIN the axis-aligned box [bx0,by0]..[bx1,by1]?
// (= the FRAMING direction: a frame travels with a selection iff it surrounds the selected children's
// point-bbox, AnnotationDragging.cs FindAnnotatedOps aRect.Contains(nRect) with the FRAME as outer).
// combine/copy use THIS to decide which annotations travel with a selection (R-AN #1): a real frame
// surrounds its nodes, so it is LARGER than the children's bbox — it contains the box, not vice versa.
// Inclusive on all four edges. Pure geometry, no model deps.
bool annotationContainsBox(const Annotation& a, float bx0, float by0, float bx1, float by1);

// Mint an annotation id that does NOT collide with any id already in `existing`. TiXL Clone() mints a
// fresh Guid; the runtime leaf has no Guid generator, so we derive a deterministic, collision-free id
// from `base` (the clone source's id) + a counter suffix. Deterministic = golden-friendly; distinct
// from the original = the loader's last-wins dedup (compound_load.cpp:87-94) never merges a clone back
// onto its source. Used by combine/copy clone.
std::string uniqueAnnotationId(const std::string& base, const std::vector<Annotation>& existing);

}  // namespace sw
