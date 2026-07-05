// LoadSvg pointlist op — loads an .svg and emits its flattened paths as a host SwPoint polyline list.
// Consumer of the svg_parse seam + the pointlist STRING-path channel (fork-pointlist-string-path-channel,
// same as LoadObjAsPoints). TiXL authority: external/tixl/Operators/Lib/point/io/LoadSvg.cs (mirrored for
// the Lines/Points import modes).
//
//   LoadSvg.cs Update() (the load-bearing path, distilled):
//     svgDoc = SvgLoader.TryLoad(FilePath);                                   // → svg_parse (nanosvg)
//     bounds = svgDoc.Bounds.Size;                                            // → SvgPolylines.width/height
//     fitBoundsFactor = ScaleToBounds ? 2 / bounds.Y : 1;                     // :49
//     scale = Scale * fitBoundsFactor;                                        // :50
//     pathElements = ConvertAllNodesIntoGraphicPaths(...) then Flatten(reduceFactor)  // → svg_parse polylines
//     centerOffset = CenterToBounds ? (-bounds.X/2, +bounds.Y/2, 0) : 0       // :95
//     per flattened vertex:                                                   // :119-130
//        Position    = (new Vector3(x, 1 - y, 0) + centerOffset) * scale;     // :124  (Y-FLIP + offset + scale)
//        F1 = 1; Orientation = Identity(then overwritten); Color = (1,1,1,1); F2 = 1; Scale = (1,1,1)
//     per figure: Orientation from neighbor positions (RotationFromTwoPositions, :176-179), then a
//        trailing Point.Separator() (:167) between figures.
//
// NAMED FORKS (vs LoadSvg.cs — that file IS the parity reference):
//   • fork-svg-flatten-algorithm (svg_parse.h): curve tessellation is nanosvg's, not GDI+'s → curve
//     vertex counts/positions differ. Straight geometry (M/L/Z, rect) is byte-identical. The golden pins
//     straight geometry only.
//   • fork-svg-nanosvg-closes-figure: nanosvg's Z / rect / circle emit the sub-path with the first
//     vertex ALREADY repeated at the end (its own path-close), which is EXACTLY what LoadSvg produces
//     after its manual NeedsClosing append (:159-163). So this op does NOT re-append on `closed` — the
//     observable (start vertex duplicated once for a closed figure) already matches. `SvgPolyline.closed`
//     is carried for documentation; re-appending would double-close (a real bug), so we don't.
//   • fork-svg-path-order: nanosvg walks sub-paths in its own order (reverse of document order for a
//     multi-figure `d`); TiXL walks GraphicsPath sub-paths in SplitGraphicsPathIntoSubPaths order. Figure
//     ORDER within the emitted list can differ; the per-figure geometry is identical. A cosmetic fork
//     (a downstream consumer treats the list as a bag of separator-delimited polylines).
//   • fork-svg-importas-lines-only: ImportAs Lines(0) and Points(1) share this emit loop (both walk every
//     flattened vertex — Points is Lines without the polyline-render interpretation, LoadSvg.cs:53-54).
//     Shape(2) (single-shape fill select) is NOT ported (fork-svg-no-shape-mode, svg_parse.h). An
//     out-of-range ImportAs falls back to the Lines/Points emit.
//   • fork-svg-bounds-are-canvas: LoadSvg's bounds = svgDoc.Bounds.Size — the CONTENT bounding box (the
//     drawn extent of the paths). nanosvg exposes only NSVGimage.width/height (the SVG width/height
//     ATTRIBUTES = the canvas), not a content bbox. So sw's CenterToBounds/ScaleToBounds use the CANVAS
//     size, TiXL uses the CONTENT size. They COINCIDE when the artwork fills its canvas (the common
//     authoring case + what the golden pins); they diverge for art with margin. Named, not silent.
//   • fork-svg-color-white: LoadSvg hardcodes every point Color = Vector4.One ("We need a better fix,
//     maybe with the colors from the SVG file", :127) — sw reproduces the white default, NOT the SVG
//     fill color. A genuine TiXL limitation, mirrored not "fixed".
//   • fork-pointlist-string-path-channel / fork-pointlist-flat-only-no-resident / fork-no-hot-reload:
//     identical to LoadObjAsPoints (Path via inputStrings[0], flat-cook only, re-read every cook).
#include <cmath>
#include <string>
#include <vector>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug / swPointDefault
#include "runtime/svg_parse.h"               // svgParsePolylinesFromFile / SvgPolylines
#include "runtime/tixl_point.h"              // SwPoint full def (the host point currency)

namespace sw {

namespace {

// RotationFromTwoPositions (LoadSvg.cs:176-179): a Z-axis quaternion from the direction p1→p2.
//   angle = Atan2(p1.x - p2.x, -(p1.y - p2.y)) + π/2 ; q = axisAngle(Z, angle).
simd::float4 rotationFromTwoPositions(const simd::float3& p1, const simd::float3& p2) {
  float angle = std::atan2(p1.x - p2.x, -(p1.y - p2.y)) + (float)M_PI * 0.5f;
  float h = angle * 0.5f;
  return simd::float4{0.0f, 0.0f, std::sin(h), std::cos(h)};  // axisAngle about +Z
}

void cookLoadSvg(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // Path: PointListCookCtx::inputStrings[0] (the String channel). null (resident) / empty → no doc.
  std::string path;
  if (c.inputStrings && !c.inputStrings->empty()) path = (*c.inputStrings)[0];

  // ReduceFactor (LoadSvg.cs:56, clamp 0.001..1) → curve flatness. Straight geometry ignores it.
  float reduceFactor = pointListParam(c.params, "ReduceFactor", 1.0f);
  if (reduceFactor < 0.001f) reduceFactor = 0.001f;
  if (reduceFactor > 1.0f) reduceFactor = 1.0f;

  SvgPolylines doc;
  if (!svgParsePolylinesFromFile(path, doc, reduceFactor)) {
    // svgDoc == null → empty list (LoadSvg.cs:39-44).
    if (pointListInjectBug()) c.output->clear();
    return;
  }

  const bool centerToBounds = pointListParam(c.params, "CenterToBounds", 0.0f) > 0.5f;
  const bool scaleToBounds = pointListParam(c.params, "ScaleToBounds", 0.0f) > 0.5f;
  const float scaleKnob = pointListParam(c.params, "Scale", 1.0f);

  // bounds = svgDoc.Bounds.Size (LoadSvg.cs:48). fitBoundsFactor = ScaleToBounds ? 2/bounds.Y : 1 (:49).
  const float boundsX = doc.width;
  const float boundsY = doc.height;
  const float fitBoundsFactor = (scaleToBounds && boundsY != 0.0f) ? (2.0f / boundsY) : 1.0f;
  const float scale = scaleKnob * fitBoundsFactor;

  // centerOffset = CenterToBounds ? (-bounds.X/2, +bounds.Y/2, 0) : 0 (LoadSvg.cs:95). Note the +Y/2 (the
  // Y-flip's origin is at y=bounds.Y, so the centered offset is +bounds.Y/2 in flipped space).
  const simd::float3 centerOffset = centerToBounds
                                        ? simd::float3{-boundsX * 0.5f, boundsY * 0.5f, 0.0f}
                                        : simd::float3{0.0f, 0.0f, 0.0f};

  auto emitPos = [&](const SvgPoint2& sp) -> simd::float3 {
    // Position = (Vector3(x, 1 - y, 0) + centerOffset) * scale (LoadSvg.cs:124).
    simd::float3 v{sp.x, 1.0f - sp.y, 0.0f};
    v.x = (v.x + centerOffset.x) * scale;
    v.y = (v.y + centerOffset.y) * scale;
    v.z = (v.z + centerOffset.z) * scale;
    return v;
  };

  for (const SvgPolyline& poly : doc.polylines) {
    const size_t n = poly.points.size();
    if (n == 0) continue;
    const size_t base = c.output->size();
    for (size_t i = 0; i < n; ++i) {
      SwPoint p = swPointDefault();          // F1=1, Rotation=identity, Color=(1,1,1,1), Scale=(1,1,1), F2=1
      simd::float3 pos = emitPos(poly.points[i]);
      p.Position = {pos.x, pos.y, 0.0f};
      c.output->push_back(p);
    }
    // Per-figure orientation from neighbor positions (LoadSvg.cs:133-156). Needs ≥2 points.
    auto posOf = [&](size_t idx) -> simd::float3 {
      const SwPoint& q = (*c.output)[base + idx];
      return simd::float3{q.Position.x, q.Position.y, q.Position.z};
    };
    if (n > 1) {
      for (size_t i = 0; i < n; ++i) {
        SwPoint& cur = (*c.output)[base + i];
        simd::float3 a, b;
        if (i == 0) {
          a = posOf(0);
          b = posOf(1);
        } else if (i == n - 1) {
          a = posOf(n - 2);
          b = posOf(n - 1);
        } else {
          a = posOf(i);
          b = posOf(i + 1);
        }
        cur.Rotation = rotationFromTwoPositions(a, b);
      }
    }
    // Trailing separator between figures (LoadSvg.cs:167). Scale = NaN (separator-nan-scale).
    SwPoint sep = swPointDefault();
    sep.Scale = {std::nanf(""), std::nanf(""), std::nanf("")};
    c.output->push_back(sep);
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list. Off in production. Same hook as every leaf.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. ONE PointList output "ResultList" + Path(String, wire-OR-const) + the LoadSvg knobs.
// PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity,
//                       multiInput, strDef}.
static const PointListOp _reg_loadsvg{
    {"LoadSvg", "LoadSvg",
     {{"ResultList", "ResultList", "PointList", false},
      // Path (String input, wire-OR-const). strDef "" → no doc by default (TiXL FilePath has no default
      // asset; the user points it at an .svg). Last positional field.
      {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      // Scale (LoadSvg.cs Scale input; .t3 default 1). Float knob.
      {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 100.0f},
      // CenterToBounds / ScaleToBounds (bool). Default false.
      {"CenterToBounds", "CenterToBounds", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"ScaleToBounds", "ScaleToBounds", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // ImportAs enum (LoadSvg.ImportModes: Lines/Points/Shape). Default Lines(0). Shape unported (fork).
      {"ImportAs", "ImportAs", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"Lines", "Points", "Shape"}, true},
      // ReduceFactor (curve flatness; clamp 0.001..1). Default 1 (coarsest). Only affects curves (fork).
      {"ReduceFactor", "ReduceFactor", "Float", true, 1.0f, 0.001f, 1.0f}},
     /*evaluate=*/nullptr},
    cookLoadSvg};

}  // namespace sw
