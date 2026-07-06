// runtime/glyph_points — the LEAF SEAM between a pure-CPU glyph→point-list op and the native macOS
// CoreText outline extractor. TiXL-ABSENT node (柏為 2026-07-06 拍板破例 — memory
// baiwei-approved-tixl-absent-glyph-points): TiXL's whole tree has NO TTF/OTF glyph-outline parser
// (grep Typography/OpenFont/FreeType/glyf all empty), yet「中文字 as points」is one of the MV 四承重
// (memory simple-world-real-target-mv-tooling), so this is sw-original with NO TiXL golden to copy.
//
// WHY A SEAM (ARCHITECTURE.md 葉子接縫, check_arch pass 1): the glyph→SwPoint OP lives in `runtime`
// (pure CPU currency, exactly like pointlist_ops_loadsvg), but the OUTLINE EXTRACTION is CoreText —
// a `platform` framework (CTFont*/CGPath*). runtime MUST NOT #include platform/. So — exactly like
// field_graph's SourceCompileFn (metal_compile lives in platform, the runtime field cook calls it via
// a fn-ptr the app installs) and the AssetTextureDecoder seam — runtime here exposes a fn-ptr + POD
// contract; `platform/glyph_outline.mm` implements it; `main.cpp` (app) wires the two. A golden may
// install a HAND-BUILT fixture provider (impl-independent geometry) OR the real CoreText one.
//
// POD CONTRACT (crosses the seam, zero framework types): the provider is handed ONE Unicode codepoint
// (UTF-32) + a font name + a font size, and returns that glyph's outline as a list of CONTOURS, each a
// flattened polyline of 2D points IN GLYPH-LOCAL SPACE (CoreText text-space: x right, y UP, origin at
// the glyph's baseline-left; units already scaled by fontSize so 1 unit = 1 point), plus the glyph's
// horizontal ADVANCE (also in points). The runtime OP owns everything downstream — pen/cursor advance,
// line breaks, the (x, 1-y)-style flip is NOT applied here (CoreText is already y-up like TiXL Point
// space), spacing/line-height knobs, scale, SwPoint emit, per-figure separators. Splitting it this way
// keeps CoreText's concern (bytes→outline) in platform and the currency/layout concern in runtime, and
// lets the golden pin layout math with a fixture that never touches a font file.
#pragma once

#include <cstdint>
#include <vector>

namespace sw {

// One point of a flattened glyph contour, in glyph-local text-space (y UP, points).
struct GlyphPoint2 { float x, y; };

// One closed contour (sub-path) of a glyph outline. A CJK '口' has 2 (outer + inner); 'I' has 1;
// a space/undrawable glyph has 0 contours (but still a nonzero advance). `closed` = the contour
// returns to its first vertex — a glyph outline sub-path is ALWAYS closed (CGPath kCGPathElementClose
// or an implicit close), carried for documentation & the same first==last convention LoadSvg uses.
struct GlyphContour {
  std::vector<GlyphPoint2> points;
  bool closed = true;
};

// One extracted glyph: its outline (0+ contours, curves already flattened) + horizontal advance.
struct GlyphOutline {
  std::vector<GlyphContour> contours;
  float advance = 0.0f;  // horizontal pen advance for this glyph, in points (fontSize-scaled).
  bool valid = false;    // true = the font had a glyph for this codepoint (even if 0 contours, e.g. space).
};

// The leaf-seam fn: extract ONE codepoint's outline from `fontName` at `fontSize` points, flattening
// curves to a straight-segment tolerance of `flatness` points (smaller = denser). Writes `out`.
// Returns false if the font could not be created (out.valid=false). `fontName` is a system font
// family/PostScript name (e.g. "PingFang SC", "STHeiti", "Helvetica") OR a font-file path; an empty
// name uses the platform default font. Implemented in platform/glyph_outline.mm (CoreText); NULL until
// the app installs it (a runtime-only build / a golden with no fixture → op emits nothing).
using GlyphOutlineFn = bool (*)(uint32_t codepoint, const char* fontName, float fontSize, float flatness,
                                GlyphOutline& out);

// Install / read the provider (app owns platform/glyph_outline, installs it in main; a golden installs
// a fixture). Same shape as setFieldSourceCompiler / setAssetTextureDecoder.
void setGlyphOutlineProvider(GlyphOutlineFn fn);
GlyphOutlineFn glyphOutlineProvider();

}  // namespace sw
