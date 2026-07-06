// platform/glyph_outline — native macOS CoreText glyph-outline extractor. The IMPL half of the
// runtime/glyph_points leaf seam (see runtime/glyph_points.h for the seam + TiXL-absent rationale).
//
// ZONE: platform (原生 macOS 接口). Uses CoreText (CTFont*) + CoreGraphics (CGPath*) — the correct
// region for a native font/outline reader, exactly like image_decode.mm uses ImageIO/CoreGraphics.
//
// ZONE HYGIENE (check_arch pass 1: platform ↛ runtime): this header does NOT include runtime/. It
// returns a PLATFORM-LOCAL POD (PlatformGlyphOutline below) — plain vectors of float pairs, no
// framework type, no runtime type. The app (main.cpp) owns the tiny bridge that copies this into the
// runtime seam's sw::GlyphOutline (main legitimately includes both zones), EXACTLY like the field seam
// where platform/metal_compile returns MTL::Library* and main casts it into the runtime void* seam.
// Keeping the two structs identical in shape makes the bridge a mechanical field-copy.
//
// MECHANISM: codepoint (UTF-32) -> UTF-16 unichar(s) -> CTFontGetGlyphsForCharacters ->
// CTFontCreatePathForGlyph -> CGPath -> CGPathApply walk (move/line/quad/cubic/close) with beziers
// FLATTENED by fixed subdivision to a `flatness` point tolerance -> per-contour polylines + the
// glyph's CTFontGetAdvancesForGlyphs advance. All in CoreText text-space (y UP, origin at baseline,
// units already scaled by fontSize). Curves are DIFFERENT from any other tessellator (TiXL-absent —
// no parity oracle; the golden pins straight fixture geometry, smoke-checks the real CoreText leg).
#pragma once

#include <cstdint>
#include <vector>

namespace sw {
namespace platform {

// One flattened contour (sub-path) of a glyph outline, in glyph-local text-space (y UP, points).
struct PlatformGlyphContour {
  std::vector<float> xy;  // interleaved x0,y0,x1,y1,… — a plain buffer (no nested struct crosses).
  bool closed = true;
};

// One extracted glyph: outline (0+ contours, curves flattened) + horizontal advance (points).
struct PlatformGlyphOutline {
  std::vector<PlatformGlyphContour> contours;
  float advance = 0.0f;
  bool valid = false;
};

// Extract ONE Unicode codepoint's outline from `fontName` at `fontSize` points, flattening curves to a
// straight-segment tolerance of `flatness` points. Returns false only if the font could not be created
// (out.valid=false). `fontName` is a system font family/PostScript name (e.g. "PingFang SC",
// "STHeiti") OR a font-file path; empty → platform default. A valid font with no glyph for the
// codepoint returns true with out.valid=true and 0 contours (a .notdef / space — advance still set).
bool extractGlyphOutline(uint32_t codepoint, const char* fontName, float fontSize, float flatness,
                         PlatformGlyphOutline& out);

}  // namespace platform
}  // namespace sw
