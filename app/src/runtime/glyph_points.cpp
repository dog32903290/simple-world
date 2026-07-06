// runtime/glyph_points — the glyph-outline leaf-seam fn-ptr storage (mirror of field_graph.cpp's
// g_sourceCompiler). runtime holds only the slot; platform/glyph_outline.mm supplies the impl and
// main.cpp (app) installs it. See glyph_points.h for the seam rationale (TiXL-absent, ARCHITECTURE.md
// 葉子接縫).
#include "runtime/glyph_points.h"

namespace sw {

namespace {
GlyphOutlineFn g_glyphProvider = nullptr;
}  // namespace

void setGlyphOutlineProvider(GlyphOutlineFn fn) { g_glyphProvider = fn; }
GlyphOutlineFn glyphOutlineProvider() { return g_glyphProvider; }

}  // namespace sw
