// ui/cjk_font — CJK glyphs in the imgui atlas so 柏為's Chinese compound names render
// as real glyphs instead of "?????". imgui's built-in font is ASCII-only; this MERGES a
// macOS system CJK face onto it (MergeMode) so ASCII stays byte-identical and only the CJK
// ranges gain shapes. Zone: ui (pure imgui atlas config; no graph, no platform — font
// FILES are probed with std::filesystem, never CoreText, so no platform dependency).
//
// imgui is pinned at 1.91.8 (no 1.92 dynamic atlas) → the atlas is built STATICALLY once at
// startup. Call loadCjkFont() right after ImGui::CreateContext() (before the first NewFrame).
#pragma once

namespace sw::ui {

// Outcome of the one-shot atlas load — returned for the startup log and asserted by the
// self-test. `loaded` false means every candidate font failed; the app still runs on the
// ASCII-only default font (defensive leg) and `font` covers ASCII exactly as before.
struct CjkFontResult {
  bool loaded = false;        // a CJK face was merged in (false → ASCII-only fallback)
  const char* path = "";      // chosen font file (static string; "" when none)
  int faceIndex = 0;          // chosen .ttc face (FontNo); 0 for single-face .ttf
  int texW = 0, texH = 0;     // built atlas dimensions (px)
  int glyphCount = 0;         // total glyphs in the merged font
  double buildMs = 0.0;       // wall time to build the atlas (ms)
  long atlasBytes = 0;        // alpha8 atlas size delta vs ASCII-only baseline (bytes)
};

// Build the imgui font atlas with a macOS CJK face merged onto the default font. Idempotent
// only in the sense of "call once"; relies on the current ImGui context's io.Fonts. Probes a
// candidate list (PingFang → STHeiti → Hiragino → Arial Unicode) in order, picking the first
// whose chosen face actually covers 柏為's Traditional-Chinese probe set (中灣體測). On total
// failure it leaves the default ASCII font in place and logs one stderr warning — never crashes.
CjkFontResult loadCjkFont();

// Isolation self-test (ARCHITECTURE.md 鐵律 5): headless, no GPU/window. Builds the atlas and
// asserts CJK glyphs resolve to real (non-fallback) shapes, ASCII 'A' metrics are unchanged by
// the merge, and the missing-font path degrades to ASCII-only without crashing. injectBug omits
// the CJK ranges so FindGlyph(中) must fall back → the test must go RED.
int runCjkFontSelfTest(bool injectBug);

}  // namespace sw::ui
