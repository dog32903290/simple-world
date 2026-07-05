// runtime/svg_parse — implementation. The ONLY TU that includes nanosvg (heavy header contained here,
// same discipline obj_parse.cpp keeps its parse internals private). See svg_parse.h for the parity
// story + named forks. TiXL authority: LoadSvg.cs / LoadSvgAsTexture2D.cs.
//
// nanosvg (third_party/nanosvg, zlib/public-domain, header-only) replaces System.Drawing.GraphicsPath:
//   • nsvgParse → NSVGimage { width, height, NSVGshape* }; each NSVGshape has NSVGpath* (one per
//     sub-path — nanosvg splits at move-to itself, matching LoadSvg's SplitGraphicsPathIntoSubPaths).
//   • NSVGpath.pts is a cubic-bezier control-point stream x0,y0,cp1x,cp1y,cp2x,cp2y,x1,y1,... (npts
//     total control points, (npts-1)/3 segments). We FLATTEN each cubic to line segments via recursive
//     subdivision to `tessTol` (fork-svg-flatten-algorithm: nanosvg's own flattening; NOT GDI+'s) — the
//     SAME recursive scheme nanosvgrast uses, so straight segments (a single M/L pair per bezier with
//     collinear control points) emit exactly the two endpoints (no spurious midpoints).
//   • NSVGpath.closed → SvgPolyline.closed (LoadSvg NeedsClosing).

#include "runtime/svg_parse.h"

#include <cmath>
#include <fstream>
#include <sstream>

// nanosvg: pull the IMPLEMENTATION into THIS TU only. NANOSVG_ALL_COLOR_KEYWORDS so named CSS colors
// (e.g. fill="red") resolve; the implementation macros must precede the includes.
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

namespace sw {

namespace {

// Recursive cubic-bezier flattening to a straight-segment polyline (the nanosvgrast scheme, distilled).
// Appends the SUBDIVIDED interior points; the caller seeds the first endpoint and this appends up to and
// including (x4,y4). A straight bezier (control points on the chord) passes the flatness test at depth 0,
// so it emits ONLY the endpoint — no phantom vertices on M/L geometry (load-bearing for the golden's
// impl-independent straight-segment pins). tol = squared distance flatness in path units.
void flattenCubic(std::vector<SvgPoint2>& out, float x1, float y1, float x2, float y2, float x3,
                  float y3, float x4, float y4, float tol, int level) {
  if (level > 10) {
    out.push_back({x4, y4});
    return;
  }
  // Distance of the two control points from the chord (x1,y1)-(x4,y4). If both are within tol, the
  // curve is flat enough → emit the endpoint.
  float dx = x4 - x1, dy = y4 - y1;
  float d2 = std::fabs((x2 - x4) * dy - (y2 - y4) * dx);
  float d3 = std::fabs((x3 - x4) * dy - (y3 - y4) * dx);
  if ((d2 + d3) * (d2 + d3) <= tol * (dx * dx + dy * dy)) {
    out.push_back({x4, y4});
    return;
  }
  // Subdivide (de Casteljau at t=0.5).
  float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
  float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
  float x34 = (x3 + x4) * 0.5f, y34 = (y3 + y4) * 0.5f;
  float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
  float x234 = (x23 + x34) * 0.5f, y234 = (y23 + y34) * 0.5f;
  float x1234 = (x123 + x234) * 0.5f, y1234 = (y123 + y234) * 0.5f;
  flattenCubic(out, x1, y1, x12, y12, x123, y123, x1234, y1234, tol, level + 1);
  flattenCubic(out, x1234, y1234, x234, y234, x34, y34, x4, y4, tol, level + 1);
}

// NSVGimage → SvgPolylines (RAW SVG-space coords). tessTol is a flatness in path units (squared-distance
// test inside flattenCubic). Returns the count of polylines added.
size_t imageToPolylines(NSVGimage* image, SvgPolylines& out, float tessTol) {
  out.polylines.clear();
  out.width = image->width;
  out.height = image->height;
  const float tol = tessTol * tessTol;  // flattenCubic compares squared magnitudes
  for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
    // Skip shapes explicitly hidden (visibility flag). nanosvg sets NSVG_FLAGS_VISIBLE on drawables.
    if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;
    for (NSVGpath* p = shape->paths; p != nullptr; p = p->next) {
      SvgPolyline poly;
      poly.closed = p->closed != 0;
      if (p->npts > 0) {
        // First endpoint.
        poly.points.push_back({p->pts[0], p->pts[1]});
        // Each cubic segment = 3 control points after the current endpoint.
        for (int i = 0; i + 3 < p->npts; i += 3) {
          const float* q = &p->pts[i * 2];
          flattenCubic(poly.points, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], tol, 0);
        }
      }
      if (!poly.points.empty()) out.polylines.push_back(std::move(poly));
    }
  }
  return out.polylines.size();
}

// Read a whole file raw/binary/verbatim (obj_parse.cpp / string_ops_readfile.cpp posture).
bool readWholeFile(const std::string& path, std::string& out) {
  if (path.empty()) return false;
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

}  // namespace

bool svgParsePolylinesFromText(std::string text, SvgPolylines& out, float tessTol, float dpi) {
  out = SvgPolylines{};
  if (text.empty()) return false;
  // nsvgParse mutates the buffer; `text` is our owned copy (taken by value).
  NSVGimage* image = nsvgParse(&text[0], "px", dpi);
  if (!image) return false;
  size_t n = imageToPolylines(image, out, tessTol);
  nsvgDelete(image);
  return n > 0;
}

bool svgParsePolylinesFromFile(const std::string& path, SvgPolylines& out, float tessTol, float dpi) {
  out = SvgPolylines{};
  std::string text;
  if (!readWholeFile(path, text)) return false;
  return svgParsePolylinesFromText(std::move(text), out, tessTol, dpi);
}

bool svgRasterizeRgba8FromText(std::string text, int w, int h, float scale,
                               std::vector<unsigned char>& rgbaOut, float dpi) {
  rgbaOut.clear();
  if (text.empty() || w <= 0 || h <= 0) return false;
  NSVGimage* image = nsvgParse(&text[0], "px", dpi);
  if (!image) return false;
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) {
    nsvgDelete(image);
    return false;
  }
  rgbaOut.assign((size_t)w * (size_t)h * 4, 0);
  // nsvgRasterize(rast, image, tx, ty, scale, dst, w, h, stride). tx/ty=0 (no pan). Straight RGBA out.
  nsvgRasterize(rast, image, 0.0f, 0.0f, scale, rgbaOut.data(), w, h, w * 4);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);
  return true;
}

}  // namespace sw
