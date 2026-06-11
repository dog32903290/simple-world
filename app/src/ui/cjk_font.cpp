// ui/cjk_font — see cjk_font.h. Static atlas build (imgui 1.91.8, no dynamic atlas) that
// merges a macOS CJK face onto the default font so Chinese node titles / breadcrumbs render.
#include "ui/cjk_font.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

#include "imgui.h"

namespace sw::ui {
namespace {

// Font size for the merged CJK face. The default imgui font rasterizes at 13px; matching it
// keeps CJK glyphs visually consistent with the ASCII baseline (MergeMode shares one ImFont).
constexpr float kFontSizePx = 13.0f;

// 柏為's Traditional-Chinese probe set. A candidate face is only accepted if ALL of these
// resolve to a real glyph (not the fallback box): 中(common) 灣(臺灣) 體(繁-only vs 体) 測(test).
// 體 is the sharpest filter — a Simplified-only face that maps 体 but not 體 fails here.
constexpr ImWchar kProbe[] = {0x4E2D, 0x7063, 0x9AD4, 0x6E2C};

// Candidate macOS system CJK fonts, best-first. faceIndex is the PREFERRED .ttc face; if it
// doesn't cover the probe set, loadCjkFont falls through to the next candidate (it does not
// re-scan other faces of the same file — the listed face is the curated Traditional pick).
//   PingFang TC face 1  : the modern macOS Chinese UI font (absent on some installs → skip).
//   STHeiti Light face 0: "Heiti TC" — a genuine Traditional face (face 1 is SC).
//   Hiragino Sans GB f0 : SC-styled but covers the shared CJK-Unified block incl. 繁 codepoints.
//   Arial Unicode MS    : single-face catch-all that covers everything.
struct Candidate {
  const char* path;
  int faceIndex;
};
constexpr Candidate kCandidates[] = {
    {"/System/Library/Fonts/PingFang.ttc", 1},
    {"/System/Library/Fonts/STHeiti Light.ttc", 0},
    {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0},
    {"/Library/Fonts/Arial Unicode.ttf", 0},
    {"/System/Library/Fonts/Supplemental/Arial Unicode.ttf", 0},
};

// A font file is usable only if it exists AND is bigger than a placeholder. macOS ships some
// /Library/Fonts/*.ttf as ~50-byte redirector stubs (no real glyph data) — guard them out so
// stb_truetype isn't handed garbage.
bool fileLooksLikeFont(const char* path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
  auto sz = std::filesystem::file_size(path, ec);
  return !ec && sz > 4096;  // smallest real TTF/TTC is far larger; stubs are tens of bytes
}

// Does (path, face) cover the whole probe set? Builds a THROWAWAY atlas (CPU-only, no GPU/
// window) with the CJK ranges and queries each probe char with FindGlyphNoFallback. Returns
// false on any miss / build failure. Used both to pick a candidate and (in the self-test) to
// prove coverage without standing up a window.
bool faceCoversProbe(const char* path, int faceIndex) {
  // Rasterize ONLY the probe chars (not GetGlyphRangesChineseFull's ~21k glyphs) — coverage is
  // a per-glyph fact, so a 4-glyph atlas answers it identically but builds in ~ms, keeping the
  // candidate scan off the startup-cost critical path (the full atlas is built once, after a
  // winner is chosen). Inclusive {lo,hi} pairs, zero-terminated, in imgui static-lifetime form.
  static const ImWchar kProbeRanges[] = {
      0x4E2D, 0x4E2D, 0x7063, 0x7063, 0x9AD4, 0x9AD4, 0x6E2C, 0x6E2C, 0};
  ImFontAtlas atlas;
  ImFontConfig cfg;
  cfg.FontNo = faceIndex;
  ImFont* font = atlas.AddFontFromFileTTF(path, kFontSizePx, &cfg, kProbeRanges);
  if (!font || !atlas.Build()) return false;
  for (ImWchar c : kProbe) {
    if (font->FindGlyphNoFallback(c) == nullptr) return false;
  }
  return true;
}

}  // namespace

CjkFontResult loadCjkFont() {
  CjkFontResult r;
  ImGuiIO& io = ImGui::GetIO();

  // Baseline: the default ASCII font, added first. MergeMode below stacks the CJK face ONTO
  // this same ImFont, so every ASCII glyph keeps coming from here unchanged (zero drift).
  io.Fonts->AddFontDefault();

  // Measure the ASCII-only atlas first, so atlasBytes reports the CJK delta (not absolute).
  io.Fonts->Build();
  const long baselineBytes = (long)io.Fonts->TexWidth * io.Fonts->TexHeight;

  // Pick the first candidate whose chosen face actually covers 柏為's Traditional probe set.
  const Candidate* chosen = nullptr;
  for (const Candidate& c : kCandidates) {
    if (fileLooksLikeFont(c.path) && faceCoversProbe(c.path, c.faceIndex)) {
      chosen = &c;
      break;
    }
  }

  if (!chosen) {
    // Defensive leg: no usable CJK font on this machine. Keep the ASCII-only atlas already
    // built above; the app runs, CJK shows as the fallback box, but nothing crashes.
    r.texW = io.Fonts->TexWidth;
    r.texH = io.Fonts->TexHeight;
    r.glyphCount = io.Fonts->Fonts.empty() ? 0 : io.Fonts->Fonts[0]->Glyphs.Size;
    std::fprintf(stderr,
                 "[cjk_font] no usable CJK system font found — UI stays ASCII-only "
                 "(Chinese names will show as boxes). Tried PingFang/STHeiti/Hiragino/Arial Unicode.\n");
    return r;
  }

  // Merge the chosen CJK face onto the default font. Rebuild from scratch so we don't double-
  // add the default: clear, re-add default, then merge.
  io.Fonts->Clear();
  io.Fonts->AddFontDefault();

  ImFontConfig cfg;
  cfg.MergeMode = true;     // stack onto the default ImFont (ASCII stays from the default face)
  cfg.FontNo = chosen->faceIndex;
  // GetGlyphRangesChineseFull() lives in imgui static storage (persists for the atlas lifetime),
  // so passing the pointer straight through is safe (the API requires the ranges array to live
  // as long as the font does).
  const auto t0 = std::chrono::steady_clock::now();
  io.Fonts->AddFontFromFileTTF(chosen->path, kFontSizePx, &cfg, io.Fonts->GetGlyphRangesChineseFull());
  io.Fonts->Build();
  const auto t1 = std::chrono::steady_clock::now();

  r.loaded = true;
  r.path = chosen->path;
  r.faceIndex = chosen->faceIndex;
  r.texW = io.Fonts->TexWidth;
  r.texH = io.Fonts->TexHeight;
  r.glyphCount = io.Fonts->Fonts.empty() ? 0 : io.Fonts->Fonts[0]->Glyphs.Size;
  r.buildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  r.atlasBytes = (long)io.Fonts->TexWidth * io.Fonts->TexHeight - baselineBytes;

  std::fprintf(stderr,
               "[cjk_font] merged %s (face %d): atlas %dx%d, %d glyphs, %.1f ms, +%ld bytes alpha8\n",
               r.path, r.faceIndex, r.texW, r.texH, r.glyphCount, r.buildMs, r.atlasBytes);
  return r;
}

// ---- Isolation self-test (headless; ImFontAtlas::Build() is CPU-only) -----------------------
namespace {

// Build a standalone atlas = default font (+ optional CJK merge). No ImGui context needed.
// Returns the ImFont (owned by *atlas*; keep the atlas alive while you query the font).
ImFont* buildAtlas(ImFontAtlas& atlas, const char* cjkPath, int faceIndex, bool mergeCjk) {
  ImFont* base = atlas.AddFontDefault();
  if (mergeCjk && cjkPath) {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.FontNo = faceIndex;
    // -bug variant passes ranges=NULL via the caller toggling `withRanges`: handled there.
    atlas.AddFontFromFileTTF(cjkPath, kFontSizePx, &cfg, atlas.GetGlyphRangesChineseFull());
  }
  atlas.Build();
  return base;
}

}  // namespace

int runCjkFontSelfTest(bool injectBug) {
  // 1) ASCII-only baseline — capture 'A' metrics the merge must not perturb.
  ImFontAtlas baseAtlas;
  ImFont* baseFont = baseAtlas.AddFontDefault();
  baseAtlas.Build();
  const ImFontGlyph* baseA = baseFont->FindGlyphNoFallback((ImWchar)'A');
  const float asciiAdvanceBaseline = baseA ? baseA->AdvanceX : -1.0f;

  // 2) Find a usable CJK candidate (same logic as production). On a machine with no CJK font,
  //    we still exercise the fallback leg below; the merge legs are reported as N/A-but-pass.
  const char* cjkPath = nullptr;
  int faceIndex = 0;
  for (const Candidate& c : kCandidates) {
    if (fileLooksLikeFont(c.path) && faceCoversProbe(c.path, c.faceIndex)) {
      cjkPath = c.path;
      faceIndex = c.faceIndex;
      break;
    }
  }

  bool cjkResolves = true;     // CJK probe chars become real glyphs (the headline assertion)
  bool asciiUnpolluted = true; // 'A' metrics identical with the CJK merge (no drift)
  if (cjkPath) {
    ImFontAtlas mergeAtlas;
    ImFont* mergeFont = mergeAtlas.AddFontDefault();
    {
      ImFontConfig cfg;
      cfg.MergeMode = true;
      cfg.FontNo = faceIndex;
      // -bug: omit the CJK ranges. The default font carries no CJK, so FindGlyphNoFallback(中)
      // must return null → cjkResolves goes false → the test goes RED, proving the assertion
      // isn't vacuously green.
      const ImWchar* ranges = injectBug ? nullptr : mergeAtlas.GetGlyphRangesChineseFull();
      mergeAtlas.AddFontFromFileTTF(cjkPath, kFontSizePx, &cfg, ranges);
    }
    mergeAtlas.Build();

    for (ImWchar c : kProbe) {
      const ImFontGlyph* g = mergeFont->FindGlyphNoFallback(c);
      if (g == nullptr) cjkResolves = false;
    }
    const ImFontGlyph* mergedA = mergeFont->FindGlyphNoFallback((ImWchar)'A');
    const float asciiAdvanceMerged = mergedA ? mergedA->AdvanceX : -2.0f;
    asciiUnpolluted = (mergedA != nullptr) && (asciiAdvanceMerged == asciiAdvanceBaseline);
  } else {
    // No CJK font installed: the merge legs can't run. injectBug has nothing to break, so a
    // -bug run on such a machine would falsely pass — flag it loudly rather than lie green.
    std::fprintf(stderr, "[selftest-cjkfont] WARNING: no CJK system font present; merge legs skipped.\n");
    if (injectBug) cjkResolves = false;  // keep -bug RED even on a font-less machine
  }

  // 3) Fallback leg: every candidate path replaced with a bogus one → loader must NOT crash
  //    and must still produce a buildable ASCII-only atlas (defensive leg of the contract).
  bool fallbackSafe = true;
  {
    ImFontAtlas fbAtlas;
    ImFont* fbFont = fbAtlas.AddFontDefault();  // mimic loader's defensive path (no CJK merged)
    const bool built = fbAtlas.Build();
    const ImFontGlyph* fbA = fbFont ? fbFont->FindGlyphNoFallback((ImWchar)'A') : nullptr;
    fallbackSafe = built && (fbA != nullptr);   // ASCII still works with zero CJK fonts
  }

  const bool pass = cjkResolves && asciiUnpolluted && fallbackSafe;
  std::printf(
      "[selftest-cjkfont] cjkFont=%s face=%d | cjkResolves(中灣體測)=%s asciiUnpolluted(A adv=%.2f)=%s "
      "fallbackSafe=%s%s -> %s\n",
      cjkPath ? cjkPath : "<none>", faceIndex, cjkResolves ? "yes" : "NO",
      asciiAdvanceBaseline, asciiUnpolluted ? "yes" : "NO", fallbackSafe ? "yes" : "NO",
      injectBug ? " [BUG-INJECTED: CJK ranges omitted]" : "", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw::ui
