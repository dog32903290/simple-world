// app/theme_registry — named color themes: the factory default + user themes loaded from disk.
// Zone: app (product behavior: cross-session memory of user themes). Depends on runtime crude_json +
// app/user_settings only; NO imgui (the ColorMap is a plain string→RGBA map, like ui/theme::ColorMap
// but defined here to keep app imgui-free). The ui/theme_editor (ui zone) drives this registry and
// hands its active theme's colors to ui::theme::applyColors().
//
// = TiXL Editor/Gui/Styling/ThemeHandling.cs:
//   • ColorTheme { Name, Author, Dictionary<string,Vector4> Colors }  (ThemeHandling.cs:232-251)
//     — sw mirrors this 1:1 (we DEFER the Variations dict; see fork note below).
//   • Themes list = FactoryTheme + every theme JSON in the Themes folder (LoadThemes, :84-110).
//   • SaveTheme: write {Name}.json into the Themes folder, then reload (:32-49).
//   • DeleteTheme: delete the file, revert to factory, reload (:51-64).
//   • GetUserOrFactoryTheme: UserSettings.Config.ColorThemeName → that theme, else factory (:66-82).
//   • Active name persisted in UserSettings (SetThemeAsUserTheme :23-30) — sw: UserSettings::colorThemeName.
//
// Persistence: one JSON file per theme at {themeFolder()}/{Name}.json. themeFolder() mirrors TiXL's
//   SettingsDirectory/Themes — sw uses ~/.simple_world/Themes/ (machine-level home-dir, same family as
//   the ~/.simple_world_settings.json store). JSON schema:
//     { "version":1, "name":"...", "author":"...", "colors": { "Text":[r,g,b,a], ... } }
//   The "colors" keys are the UiColors PascalCase field names (ui::theme::fieldNames()), so the file
//   is cross-readable with TiXL's ColorTheme.Colors dictionary keys.
//
// FORK (named): "variations-deferred". TiXL's ColorTheme also carries a Variations dict (the HSV
//   b/s/op factors, ColorVariations.cs). sw's variation MATH lives in node_style.cpp and is NOT yet a
//   themed/serialized table, so theming the variations would be dead weight here. We persist + edit
//   only the Colors dict (the 26 UiColors fields). The schema is forward-compatible: a "variations"
//   object can be added later with no migration.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace sw::theme {

// RGBA in [0,1]. Plain array so app stays imgui-free; identical layout to ui::theme::ColorMap value.
using Rgba = std::array<float, 4>;
using ColorMap = std::map<std::string, Rgba>;

// One named theme = TiXL ColorTheme { Name, Author, Colors }. (Variations deferred — see header note.)
struct ColorTheme {
  std::string name = "untitled";
  std::string author = "unknown";
  ColorMap colors;  // UiColors-field-name → RGBA. Partial maps allowed (missing → caller's default).

  // JSON round-trip (crude_json). toJson is pure; fromJson returns false on parse failure (and is
  // exception-safe — a corrupt file yields false, not a throw).
  std::string toJson() const;
  bool fromJson(const std::string& json);
};

// The process-wide theme registry: the factory default theme + every user theme loaded from disk.
class ThemeRegistry {
 public:
  // The factory theme = ui::theme's compiled-in default palette. Its name is "Factory". Always
  // present, never on disk, never deletable (mirrors ThemeHandling.FactoryTheme).
  static const char* kFactoryName;

  // (Re)build the registry: factory theme + load every {name}.json under `themeFolder`. Existing
  // themes are cleared first. Missing folder => factory-only (no error). = ThemeHandling.LoadThemes.
  void loadThemes(const std::string& themeFolder);

  // All themes, factory first then user themes in load order. The dropdown iterates this.
  const std::vector<ColorTheme>& themes() const { return themes_; }
  std::vector<std::string> themeNames() const;

  // The theme with this name, or nullptr if absent.
  const ColorTheme* find(const std::string& name) const;

  // The factory theme (always valid after construction).
  const ColorTheme& factory() const { return themes_.front(); }

  // = ThemeHandling.GetUserOrFactoryTheme: the theme named `selectedName`, else the factory theme.
  const ColorTheme& userOrFactory(const std::string& selectedName) const;

  // = ThemeHandling.SaveTheme: trim/default the name, write {name}.json into `themeFolder`, reload.
  // Returns false if the file could not be written (the registry is still reloaded). The factory
  // theme cannot be overwritten by name "Factory" (it would shadow the built-in) — named guard.
  bool saveTheme(const std::string& themeFolder, ColorTheme theme);

  // = ThemeHandling.DeleteTheme: delete {name}.json, reload. No-op (returns false) for the factory
  // theme or a theme with no backing file.
  bool deleteTheme(const std::string& themeFolder, const std::string& name);

 private:
  void addFactory();                          // push the compiled-in default as themes_.front()
  bool loadFile(const std::string& filepath); // append one theme from a JSON file (false on failure)

  std::vector<ColorTheme> themes_;  // [0] = factory, then user themes
};

// Process-wide singleton + its default themes folder (machine-level, ~/.simple_world/Themes).
ThemeRegistry& registry();
std::string defaultThemeFolder();
std::string themeFilePath(const std::string& themeFolder, const std::string& name);

// Startup convenience: load themes from defaultThemeFolder() into the singleton (wired from main,
// right after loadUserSettings()). = ThemeHandling.Initialize (factory + LoadThemes + apply happens
// in the ui layer, which then reads registry().userOrFactory(settings().colorThemeName())).
void loadThemes();

}  // namespace sw::theme
