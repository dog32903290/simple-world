// platform/glyph_outline — native macOS CoreText glyph-outline extractor. The IMPL half of the
// runtime/glyph_points leaf seam (see runtime/glyph_points.h for the seam + TiXL-absent rationale).
//
// ZONE: platform (原生 macOS 接口). Uses CoreText (CTFont*) + CoreGraphics (CGPath*) — the correct
// region for a native font/outline reader, exactly like image_decode.mm uses ImageIO/CoreGraphics.
// It returns a PURE POD (sw::GlyphOutline, defined in runtime/glyph_points.h — a plain struct of
// std::vector<float pairs>, no framework type crosses the boundary). This file includes runtime/
// glyph_points.h ONLY for that POD type + to expose the C++ entry the app installs into the runtime
// seam; it pulls NO other runtime logic (the POD header is a shared data contract, like tixl_point.h,
// not a dependency on runtime behavior — the same posture image_decode returns MTL::Texture*).
//
// MECHANISM: codepoint (UTF-32) -> UTF-16 unichar(s) -> CTFontGetGlyphsForCharacters ->
// CTFontCreatePathForGlyph -> CGPath -> CGPathApply walk (move/line/quad/cubic/close) with beziers
// FLATTENED by fixed subdivision to a `flatness` point tolerance -> per-contour polylines + the
// glyph's CTFontGetAdvancesForGlyphs advance. All in CoreText text-space (y UP, origin at baseline,
// units already scaled by fontSize). Curves are DIFFERENT from any other tessellator (this is
// TiXL-absent — there is no parity oracle; the golden pins STRAIGHT-segment geometry from a fixture
// provider, and only smoke-checks the real CoreText leg — see texttopoints_golden.cpp).
#pragma once

#include <cstdint>

#include "runtime/glyph_points.h"  // sw::GlyphOutline POD (shared data contract, not runtime logic)

namespace sw {
namespace platform {

// Extract ONE Unicode codepoint's outline from `fontName` at `fontSize` points (see GlyphOutlineFn in
// runtime/glyph_points.h for the full contract). This is the function main.cpp installs via
// setGlyphOutlineProvider. Returns false only if the font could not be created; a valid font with no
// glyph for the codepoint returns true with out.valid=true, out.contours empty (a .notdef / space).
bool extractGlyphOutline(uint32_t codepoint, const char* fontName, float fontSize, float flatness,
                         sw::GlyphOutline& out);

}  // namespace platform
}  // namespace sw
