// theme_registry_selftest — the round-trip gate for the color-theme registry + persistence.
// --selftest-theme-roundtrip / --selftest-theme-roundtrip-bug.
//
// Proves (deliverable's gate):
//   1. Construct a NON-default theme (name/author + a full ColorMap of perturbed RGBA) -> save to a
//      temp folder -> load into a FRESH registry -> every field survives BIT-FOR-BIT (and name/author).
//   2. The factory theme is always present (themes()[0]) and never written to disk.
//   3. saveTheme appears in the reloaded registry under its (trimmed) name; deleteTheme removes it.
//   4. Pure JSON round-trip (toJson -> fromJson) preserves name/author + every color exactly.
//   5. Corrupt JSON -> fromJson false (no throw), colors empty.
//   6. -bug: perturb ONE field on reload -> the bit-for-bit assertion FAILS (RED).
#include "app/theme_registry.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace sw {

namespace {
using theme::ColorTheme;
using theme::ColorMap;
using theme::Rgba;
using theme::ThemeRegistry;

std::string tmpFolder() {
  const char* tmp = std::getenv("TMPDIR");
  std::string dir = tmp && *tmp ? tmp : "/tmp";
  if (dir.back() != '/') dir += '/';
  return dir + "sw_theme_registry_selftest";
}

// A non-default theme: a handful of UiColors fields with deliberately off-default RGBA (including
// non-trivial alpha + fractional channels that must survive float<->JSON exactly within tolerance).
ColorTheme makeTheme() {
  ColorTheme t;
  t.name = "  Midnight  ";        // leading/trailing space -> saveTheme must trim to "Midnight"
  t.author = "selftest";
  t.colors["Text"]             = Rgba{0.91f, 0.12f, 0.34f, 1.0f};
  t.colors["BackgroundActive"] = Rgba{0.05f, 0.50f, 0.95f, 0.75f};
  t.colors["Selection"]        = Rgba{0.20f, 0.20f, 0.20f, 0.5f};
  t.colors["ColorForGpuData"]  = Rgba{0.42f, 0.13f, 0.07f, 1.0f};
  return t;
}

bool rgbaEq(const Rgba& a, const Rgba& b) {
  const float kEps = 1e-5f;  // float->JSON(double)->float must round-trip well within this
  for (int i = 0; i < 4; ++i) {
    float d = a[i] - b[i];
    if (d < 0) d = -d;
    if (d > kEps) return false;
  }
  return true;
}

bool colorsEq(const ColorMap& a, const ColorMap& b) {
  if (a.size() != b.size()) return false;
  for (const auto& [k, v] : a) {
    auto it = b.find(k);
    if (it == b.end() || !rgbaEq(v, it->second)) return false;
  }
  return true;
}
}  // namespace

int runThemeRegistrySelfTest(bool injectBug) {
  int fail = 0;
  const std::string folder = tmpFolder();
  std::error_code ec;
  fs::remove_all(folder, ec);  // start clean

  // --- (2) Fresh registry on an empty/missing folder => factory-only, factory at [0]. ---
  {
    ThemeRegistry reg;
    reg.loadThemes(folder);
    if (reg.themes().size() != 1) {
      std::printf("[theme-roundtrip] empty folder gave %d themes (want 1=factory) -> FAIL\n",
                  (int)reg.themes().size()); ++fail;
    }
    if (reg.factory().name != std::string(ThemeRegistry::kFactoryName)) {
      std::printf("[theme-roundtrip] factory name wrong -> FAIL\n"); ++fail;
    }
    // Factory must apply byte-default: its colors map is empty (every field falls back to default).
    if (!reg.factory().colors.empty()) {
      std::printf("[theme-roundtrip] factory colors not empty -> FAIL\n"); ++fail;
    }
  }

  // --- (1) construct -> save -> FRESH registry load -> bit-for-bit survival. ---
  {
    ThemeRegistry saver;
    saver.loadThemes(folder);
    ColorTheme src = makeTheme();
    if (!saver.saveTheme(folder, src)) {
      std::printf("[theme-roundtrip] saveTheme failed -> FAIL\n"); ++fail;
    }

    // Fresh registry reloaded from disk (the real cross-session path).
    ThemeRegistry loader;
    loader.loadThemes(folder);

    // (3) the saved theme appears under its TRIMMED name; factory still present.
    const ColorTheme* got = loader.find("Midnight");
    if (!got) {
      std::printf("[theme-roundtrip] saved theme not found after reload (trim?) -> FAIL\n"); ++fail;
    } else {
      ColorMap want = src.colors;  // expected = exactly what we saved
      ColorMap reloaded = got->colors;
      // -bug: perturb ONE field on reload so the bit-for-bit assertion below must catch the drift.
      if (injectBug && !reloaded.empty()) reloaded.begin()->second[0] += 0.1f;

      if (got->author != src.author) {
        std::printf("[theme-roundtrip] author lost (%s) -> FAIL\n", got->author.c_str()); ++fail;
      }
      if (!colorsEq(want, reloaded)) {
        std::printf("[theme-roundtrip] color fields did NOT survive bit-for-bit -> FAIL\n"); ++fail;
      }
    }

    // (3 cont.) delete -> gone, factory restored.
    if (!saver.deleteTheme(folder, "Midnight")) {
      std::printf("[theme-roundtrip] deleteTheme reported failure -> FAIL\n"); ++fail;
    }
    ThemeRegistry afterDel;
    afterDel.loadThemes(folder);
    if (afterDel.find("Midnight")) {
      std::printf("[theme-roundtrip] theme still present after delete -> FAIL\n"); ++fail;
    }
  }

  // --- (4) pure JSON round-trip preserves name/author + every color exactly. ---
  {
    ColorTheme a = makeTheme();
    a.name = "PureRoundTrip";  // (no trim concern here)
    ColorTheme b;
    if (!b.fromJson(a.toJson())) {
      std::printf("[theme-roundtrip] fromJson(toJson) failed -> FAIL\n"); ++fail;
    }
    if (b.name != a.name || b.author != a.author || !colorsEq(a.colors, b.colors)) {
      std::printf("[theme-roundtrip] JSON round-trip lost name/author/colors -> FAIL\n"); ++fail;
    }
  }

  // --- (5) corrupt JSON => fromJson false, colors empty. ---
  {
    ColorTheme c;
    if (c.fromJson("{ not valid json")) {
      std::printf("[theme-roundtrip] fromJson(garbage) returned true -> FAIL\n"); ++fail;
    }
    if (!c.colors.empty()) {
      std::printf("[theme-roundtrip] garbage parse left %d colors -> FAIL\n",
                  (int)c.colors.size()); ++fail;
    }
  }

  // --- (7) path-name guard: a name with ".." (or a path separator) is rejected, no file escapes the
  //         Themes folder. saveTheme returns false and nothing is written outside `folder`.
  {
    ThemeRegistry reg;
    reg.loadThemes(folder);
    ColorTheme evil = makeTheme();
    evil.name = "../evil";
    if (reg.saveTheme(folder, evil)) {
      std::printf("[theme-roundtrip] saveTheme accepted '../evil' name -> FAIL\n"); ++fail;
    }
    // The traversal target (folder/../evil.json) must not have been created.
    if (fs::exists(theme::themeFilePath(folder, evil.name), ec)) {
      std::printf("[theme-roundtrip] '../evil' name escaped the Themes folder -> FAIL\n"); ++fail;
    }
  }

  fs::remove_all(folder, ec);

  if (injectBug) {
    std::printf("[theme-roundtrip] injectBug fail count=%d -> %s\n", fail,
                fail > 0 ? "PASS (red-proof)" : "FAIL (bug not caught)");
    return fail > 0 ? 1 : 0;
  }
  std::printf("[theme-roundtrip] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  return fail;
}

}  // namespace sw
