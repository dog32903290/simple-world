// runtime/svg_parse — pure-CPU SVG → flattened-polyline parser (the SVG device-IO sub-lane core).
//
// TiXL authority: external/tixl/Operators/Lib/point/io/LoadSvg.cs. TiXL parses the .svg with the
// `Svg` (SharpVectors-family) library into GraphicsPath objects, then calls GraphicsPath.Flatten(null,
// reduceFactor) to tessellate curves into straight segments, and finally walks path.PathPoints emitting
// one Point per flattened vertex (LoadSvg.cs:100-169). sw mirrors the SAME SHAPE — parse → flatten →
// per-vertex polylines — using nanosvg (third_party/nanosvg, zlib/public-domain, header-only) as the
// parse+curve-tessellation library in place of System.Drawing.GraphicsPath.
//
// PLACEMENT: runtime zone, a pure leaf. Text → arrays only. NO platform/Cocoa, NO Metal. The PARSE
// entry (svgParsePolylinesFromText) is hermetic (no file I/O); a convenience svgParsePolylinesFromFile
// reads the file with std::ifstream (raw path, binary, verbatim — the obj_parse.cpp / string_ops_
// readfile.cpp posture) then parses. Keeping the parse pure lets a golden assert it with zero
// filesystem dependency. This header does NOT include nanosvg — only svg_parse.cpp does (the heavy
// header is contained to one TU, same discipline as obj_parse owning its own parse internals).
//
// WHAT IS MIRRORED (LoadSvg.cs, file:line) — the LINES/POINTS import shape (ImportAs 0/1):
//   • Each SvgPath / shape element → one-or-more sub-paths (a "figure"). LoadSvg splits a GraphicsPath
//     at each StartPoint marker into sub-paths (LoadSvg.cs:303-372 SplitGraphicsPathIntoSubPaths);
//     nanosvg ALREADY yields one NSVGpath per sub-path (its own move-to split), so each NSVGpath maps
//     to one SvgPolyline here — the same granularity, reached by the library's own splitting.
//   • Curve tessellation: LoadSvg calls Flatten(null, reduceFactor) (:102). nanosvg tessellates cubic
//     beziers via recursive subdivision at parse-adjacent time; svgParsePolylinesFromText takes a
//     `tessTol` (pixel flatness) forwarded to nanosvg's flattening. fork-svg-flatten-algorithm (below):
//     the two tessellators are DIFFERENT algorithms → curve vertex COUNTS/positions differ. A golden
//     therefore pins STRAIGHT-SEGMENT geometry (M/L/Z, rect, polyline) where flattening is a NO-OP and
//     the emitted vertices ARE the authored path vertices exactly (impl-independent, GOLDEN_STANDARD
//     期望值 independent-of-impl). Curve parity is a named fork, not a golden claim.
//   • NeedsClosing (LoadSvg.cs:159-163): a closed figure (Z / rect / circle / ellipse) repeats its
//     first vertex at the end to close the loop. We carry `closed` per polyline; the OP appends the
//     duplicate first point when closed (parity kept in the op, matching LoadSvg's per-path closing).
//   • Coordinate convention: LoadSvg emits Position = (x, 1 - y, 0) — a Y-FLIP (SVG Y-down → TiXL
//     Y-up, offset by the "1 - " origin). svg_parse stays RAW (SVG-space x,y as parsed); the OP applies
//     (x, 1 - y) + centerOffset, then * scale, faithful to LoadSvg.cs:124. Keeping the parser raw lets
//     the golden verify the parse independent of the op's scale/center knobs.
//
// NAMED FORKS (vs LoadSvg.cs — that file IS the parity reference):
//   • fork-svg-flatten-algorithm: nanosvg's recursive-subdivision bezier tessellation ≠ .NET GDI+
//     GraphicsPath.Flatten. Curve-containing paths yield a DIFFERENT number of vertices at DIFFERENT
//     positions (both are valid flattenings to their own tolerance). Straight-segment geometry is
//     byte-identical (no curve → tessellator never runs) — that is what the golden pins. reduceFactor
//     (TiXL 0.001..1, a flatness in path units) maps to nanosvg tessTol (a pixel flatness); the mapping
//     is a knob, not a parity claim (only affects curve density, which is already forked).
//   • fork-svg-shape-primitives: nanosvg expands <rect>/<circle>/<ellipse>/<line>/<polyline>/<polygon>
//     into path/bezier data at parse time (its own primitive→path conversion), matching LoadSvg's
//     element.Path(renderer) for SvgPathBasedElement (:250). A rect closes (NeedsClosing), same as TiXL.
//   • fork-svg-no-shape-mode: LoadSvg ImportAs=Shape (mode 2, single-shape select + GetBounds) is NOT
//     ported — only Lines/Points (per-vertex polylines). Shape mode is a filled-region selector, a
//     later concern; named, not silent.
//   • fork-svg-no-svgfont: nanosvg does NOT parse SVG <font>/<glyph> line-font definitions (only
//     drawable shapes). LineTextPoints (which reads SvgFont glyph outlines) is therefore BLOCKED on
//     this library — see the LineTextPoints diagnostic. LoadSvg does not need fonts.
//   • fork-no-hot-reload: TiXL wraps the doc in Resource<SvgDocument> with a file watcher; sw re-reads
//     every cook (svgParsePolylinesFromFile). Observable Result is the current on-disk svg either way.
#pragma once

#include <string>
#include <vector>

namespace sw {

// A single parsed 2D point in RAW SVG space (x right, y DOWN — the op applies the Y-flip).
struct SvgPoint2 { float x, y; };

// One flattened sub-path: a straight-segment polyline (curves already tessellated by the parser).
// `closed` = the figure closes back to its first vertex (Z / rect / circle / ellipse) — LoadSvg's
// NeedsClosing. Points are in file/traversal order.
struct SvgPolyline {
  std::vector<SvgPoint2> points;
  bool closed = false;
};

// The parsed document: raw canvas size + every flattened polyline in document order.
struct SvgPolylines {
  float width = 0.0f;   // NSVGimage.width  (the SVG canvas width in user units)
  float height = 0.0f;  // NSVGimage.height (the SVG canvas height in user units)
  std::vector<SvgPolyline> polylines;
};

// Parse SVG TEXT (no file I/O) into `out`. Returns true on a parseable document with ≥1 polyline.
//   `tessTol` = curve flatness forwarded to nanosvg (pixel tolerance for cubic subdivision). Only
//   affects CURVE density (fork-svg-flatten-algorithm); straight geometry is tessTol-independent.
//   `dpi` = user-unit → pixel scale for unit resolution (nanosvg default 96). Empty/invalid text or a
//   document with no drawable path → false (out cleared). Faithful to LoadSvg's svgDoc==null early-out
//   (:39-44: empty list).
// NOTE: nanosvgParse MUTATES its input buffer, so this takes the text by value and parses a copy.
bool svgParsePolylinesFromText(std::string text, SvgPolylines& out, float tessTol = 1.0f,
                               float dpi = 96.0f);

// Convenience: read the file at `path` (raw path, binary, verbatim — obj_parse.cpp posture) then
// svgParsePolylinesFromText. Empty/missing/unreadable path → false (mirrors the svgDoc==null early-out).
// NOT pure (does file I/O) — kept out of the text entry so the parse stays hermetic.
bool svgParsePolylinesFromFile(const std::string& path, SvgPolylines& out, float tessTol = 1.0f,
                               float dpi = 96.0f);

// --- Rasterize (LoadSvgAsTexture2D) ---
// Parse SVG TEXT and rasterize to a tightly-packed RGBA8 buffer (straight-alpha byte order R,G,B,A;
// nanosvgrast premultiplies internally then the buffer carries the composited result). `w`/`h` are the
// output pixel dimensions; `scale` maps SVG user units → pixels (nsvgRasterize scale). Returns true and
// fills `rgbaOut` (size = w*h*4) on success. Empty/invalid text or w/h ≤ 0 → false. TiXL authority:
// LoadSvgAsTexture2D.cs (svgDocument.Draw(w,h) → System.Drawing.Bitmap → RGBA Texture2D, :127-139).
// fork-svg-rasterizer: nanosvgrast (scanline AA) ≠ GDI+ System.Drawing rasterization — antialiased edge
// pixels differ. A golden pins INTERIOR-of-fill / clear-background texels (far from any edge) where both
// rasterizers agree on the solid color, NOT edge AA (impl-independent).
bool svgRasterizeRgba8FromText(std::string text, int w, int h, float scale,
                               std::vector<unsigned char>& rgbaOut, float dpi = 96.0f);

}  // namespace sw
