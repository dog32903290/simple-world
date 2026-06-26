// ui/theme_editor — see theme_editor.h. The Color Theme Editor window (mirrors ColorThemeEditor.cs).
#include "ui/theme_editor.h"

#include <array>
#include <cstring>
#include <string>

#include "imgui.h"

#include "app/theme_registry.h"   // registry + persistence (app zone)
#include "app/user_settings.h"    // active theme name (= UserSettings.Config.ColorThemeName)
#include "ui/theme.h"             // field table + defaultColorMap + applyColors (= T3Style.Apply)

namespace sw::ui {
namespace {

// The working copy being edited + the baseline it was loaded from (for change tracking + revert).
// = ColorThemeEditor._currentTheme / _currentThemeWithoutChanges.
struct EditState {
  bool initialized = false;
  sw::theme::ColorTheme current;          // name/author/colors being edited
  sw::theme::ColorTheme baseline;         // last loaded/saved state
  std::array<char, 128> nameBuf{};
  std::array<char, 128> authorBuf{};
};
EditState g_st;

// Set the name/author input buffers from a string.
void setBuf(std::array<char, 128>& buf, const std::string& s) {
  std::snprintf(buf.data(), buf.size(), "%s", s.c_str());
}

// Resolve a field's RGBA for DISPLAY: the theme's value if present, else the compiled-in default.
// (The factory theme's colors map is empty, so this shows TiXL defaults — see theme_registry.h.)
ImVec4 displayColor(const sw::theme::ColorTheme& t, const std::string& field) {
  auto it = t.colors.find(field);
  if (it != t.colors.end()) {
    const auto& a = it->second;
    return ImVec4(a[0], a[1], a[2], a[3]);
  }
  const sw::theme::ColorMap& d = sw::ui::theme::defaultColorMap();
  auto di = d.find(field);
  if (di != d.end()) {
    const auto& a = di->second;
    return ImVec4(a[0], a[1], a[2], a[3]);
  }
  return ImVec4(0, 0, 0, 1);
}

// Live-apply the current edit state's colors to the ImGui style (= T3Style.Apply on the working set).
void applyCurrent() { sw::ui::theme::applyColors(g_st.current.colors); }

// Load `theme` as the new working copy + baseline, refresh the input buffers, and apply it live.
void selectTheme(const sw::theme::ColorTheme& theme) {
  g_st.current = theme;
  g_st.baseline = theme;
  setBuf(g_st.nameBuf, theme.name);
  setBuf(g_st.authorBuf, theme.author);
  applyCurrent();
}

// Has the working copy diverged from its baseline? (name/author/any color) = _somethingChanged.
bool somethingChanged() {
  return g_st.current.name != g_st.baseline.name ||
         g_st.current.author != g_st.baseline.author ||
         g_st.current.colors != g_st.baseline.colors;
}

// First-open init: load the user-or-factory theme (= ColorThemeEditor first-frame init).
void ensureInitialized() {
  if (g_st.initialized) return;
  const std::string& active = sw::settings::settings().colorThemeName();
  selectTheme(sw::theme::registry().userOrFactory(active));
  g_st.initialized = true;
}

}  // namespace

bool& themeEditorVisible() {
  static bool g_visible = false;  // default OFF: opened on demand from the toolbar
  return g_visible;
}

void drawThemeEditor() {
  if (!themeEditorVisible()) return;
  ensureInitialized();

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 376.0f, vp->WorkPos.y + 48.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::Begin("Color Theme");

  auto& reg = sw::theme::registry();

  // ── Theme dropdown (= FormInputs.AddDropdown over ThemeHandling.Themes names) ──────────────────
  // Selecting a theme makes it the user theme (persisted) + loads + applies it live.
  std::string active = sw::settings::settings().colorThemeName();
  const std::string activeLabel = active.empty() ? std::string(sw::theme::ThemeRegistry::kFactoryName) : active;
  if (ImGui::BeginCombo("Theme", activeLabel.c_str())) {
    for (const std::string& name : reg.themeNames()) {
      const bool isSel = (name == activeLabel);
      if (ImGui::Selectable(name.c_str(), isSel)) {
        const sw::theme::ColorTheme* t = reg.find(name);
        if (t) {
          // = ThemeHandling.SetThemeAsUserTheme: persist the active name + apply.
          const bool isFactory = (name == sw::theme::ThemeRegistry::kFactoryName);
          sw::settings::settings().setColorThemeName(isFactory ? std::string() : name);
          sw::settings::settings().save(sw::settings::defaultSettingsPath());
          selectTheme(*t);
        }
      }
      if (isSel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Spacing();

  // ── Name / Author (= FormInputs.AddStringInput, change-tracked) ────────────────────────────────
  if (ImGui::InputText("Name", g_st.nameBuf.data(), g_st.nameBuf.size()))
    g_st.current.name = g_st.nameBuf.data();
  if (ImGui::InputText("Author", g_st.authorBuf.data(), g_st.authorBuf.size()))
    g_st.current.author = g_st.authorBuf.data();

  const bool changed = somethingChanged();

  ImGui::Spacing();

  // ── Save (disabled unless changed) / Save As / Delete (= CustomComponents.DisablableButton etc.) ─
  ImGui::BeginDisabled(!changed);
  if (ImGui::Button("Save")) {
    // = ThemeHandling.SaveTheme + UserSettings.Config.ColorThemeName = name.
    if (reg.saveTheme(sw::theme::defaultThemeFolder(), g_st.current)) {
      const std::string savedName = g_st.current.name;
      sw::settings::settings().setColorThemeName(savedName);
      sw::settings::settings().save(sw::settings::defaultSettingsPath());
      if (const sw::theme::ColorTheme* t = reg.find(savedName)) selectTheme(*t);
    }
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  // Save As: write under the CURRENT name as a NEW file even when unchanged (factory → user theme).
  if (ImGui::Button("Save As")) {
    if (reg.saveTheme(sw::theme::defaultThemeFolder(), g_st.current)) {
      const std::string savedName = g_st.current.name;
      sw::settings::settings().setColorThemeName(savedName);
      sw::settings::settings().save(sw::settings::defaultSettingsPath());
      if (const sw::theme::ColorTheme* t = reg.find(savedName)) selectTheme(*t);
    }
  }

  ImGui::SameLine();
  // Delete: factory theme has no file → the registry no-ops; we just guard the button label.
  const bool isFactory = (g_st.current.name == sw::theme::ThemeRegistry::kFactoryName);
  ImGui::BeginDisabled(isFactory);
  if (ImGui::Button("Delete")) {
    reg.deleteTheme(sw::theme::defaultThemeFolder(), g_st.current.name);
    sw::settings::settings().setColorThemeName(std::string());
    sw::settings::settings().save(sw::settings::defaultSettingsPath());
    selectTheme(reg.factory());  // = DeleteTheme reverts to factory
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // ── Colors section (= DrawColorEdits over typeof(UiColors).GetFields()) ────────────────────────
  // One ColorEdit4 per UiColors field; editing writes into the working copy + applies live. A field
  // matching the default shows as such; editing it inserts an override into current.colors.
  ImGui::TextDisabled("Colors");
  for (const std::string& field : sw::ui::theme::fieldNames()) {
    ImGui::PushID(field.c_str());
    ImVec4 col = displayColor(g_st.current, field);
    if (ImGui::ColorEdit4(field.c_str(), &col.x,
                          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
      g_st.current.colors[field] = {col.x, col.y, col.z, col.w};
      applyCurrent();  // = FrameStats.UiColorsChanged + T3Style.Apply() on edit
    }
    // Click the LABEL to revert this field to its default (= ColorThemeEditor label-click reset).
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      g_st.current.colors.erase(field);
      applyCurrent();
    }
    ImGui::PopID();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled("Right-click a swatch label to reset that color to default.");
  ImGui::TextDisabled("Data-type variations: deferred (see theme_editor.h).");

  ImGui::End();
}

}  // namespace sw::ui
