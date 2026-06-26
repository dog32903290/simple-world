// ui/theme — faithful port of TiXL's default color theme (Editor/Gui/Styling).
//
// TiXL ground-truth:
//   - UiColors.cs      : the named palette (datatype base colors + every ImGui-style slot color)
//   - ColorVariations.cs: the HSV variation factors (b/s/op) applied on top of a base color
//   - T3Style.Apply()  : copies UiColors into ImGui::GetStyle().Colors[...] + sets style metrics
//
// This file holds the default theme's literal constants (the "default theme table"), a NAMED FIELD
// TABLE (mirrors TiXL's reflection over typeof(UiColors).GetFields()), and a theme::apply() that
// drives ImGui's global style — faithful to T3Style.Apply(). The HSV variation MATH stays in
// node_style.cpp (it consumes defaultTheme().base* for the per-node tints).
//
// Zone: ui (pure ImGui styling, same as cjk_font / node_style). Reads no graph, mutates only the
// ImGui style. The DEFAULT apply() is wired from the app shell init (app_delegate.cpp) right after
// StyleColorsDark(), exactly like loadCjkFont(). The Color-Theme-Editor window + the registry that
// persists user themes drive applyColors() at runtime (registry = app zone, see app/theme_registry).
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace sw::ui::theme {

// The default theme = TiXL's compiled-in UiColors defaults. All values are byte-for-byte the
// literals in external/tixl/Editor/Gui/Styling/UiColors.cs (file:line noted per field in theme.cpp).
struct DefaultTheme {
  // --- Datatype base colors (UiColors "Datatype base colors", adjusted by ColorVariations) ---
  // node_style maps a node's category dataType to one of these, then applies a ColorVariation.
  ImVec4 colorForValues;    // UiColors.ColorForValues   (gray)    — sw "Float" / unknown
  ImVec4 colorForString;    // UiColors.ColorForString   (green)
  ImVec4 colorForTextures;  // UiColors.ColorForTextures (magenta) — sw "Texture2D"
  ImVec4 colorForDX11;      // UiColors.ColorForDX11
  ImVec4 colorForCommands;  // UiColors.ColorForCommands (cyan)    — sw "Command" / "ParticleForce"
  ImVec4 colorForGpuData;   // UiColors.ColorForGpuData  (red)     — sw "Points"
  ImVec4 colorForShaderGraph;// UiColors.ColorForShaderGraph

  // --- ImGui-style palette (the UiColors slots T3Style.Apply() copies into style.Colors[...]) ---
  ImVec4 text;
  ImVec4 textDisabled;
  ImVec4 backgroundButton;
  ImVec4 backgroundHover;
  ImVec4 backgroundActive;       // Color.FromString("#4592FF")
  ImVec4 popupBorder;
  ImVec4 backgroundGaps;
  ImVec4 backgroundInputField;
  ImVec4 backgroundInputFieldHover;
  ImVec4 backgroundInputFieldActive;
  ImVec4 scrollbarBackground;
  ImVec4 scrollbarHandle;
  ImVec4 windowResizeHandle;
  ImVec4 windowBackground;       // ChildBg (graph see-through)
  ImVec4 backgroundPopup;
  ImVec4 checkMark;
  ImVec4 backgroundTabActive;    // Color.FromString("#3A3A3A")
  ImVec4 backgroundTabInActive;  // Color.FromString("#CC282828")
  ImVec4 selection;              // UiColors.Selection (white) — selected node outline
};

// The compiled-in default theme (TiXL UiColors defaults). Stable reference for the app's lifetime.
const DefaultTheme& defaultTheme();

// ---- Named field table (mirrors TiXL's typeof(UiColors).GetFields() reflection) ------------------
// A theme is, faithfully to TiXL, a string-keyed map of color fields. ColorMap is that map; the field
// NAMES are exactly the UiColors field names TiXL persists into ColorTheme.Colors (PascalCase). The
// fieldNames() order is the canonical editor + serialization order (stable across runs).
using ColorMap = std::map<std::string, std::array<float, 4>>;

// The ordered list of theme color field names (UiColors PascalCase, e.g. "ColorForValues", "Text").
const std::vector<std::string>& fieldNames();

// The default theme as a ColorMap (every fieldNames() key → its TiXL default RGBA). This is the
// FactoryTheme palette (ThemeHandling.FactoryTheme). Built once from defaultTheme().
const ColorMap& defaultColorMap();

// = T3Style.Apply() driven by the COMPILED-IN default theme. Call once at app init (after
// ImGui::StyleColorsDark(), before the first frame). Idempotent. Equivalent to applyColors(defaultColorMap()).
void apply();

// = T3Style.Apply() driven by an arbitrary theme palette (the active registry theme). Any field
// missing from `colors` falls back to its TiXL default, so a partial/hand-edited theme still yields a
// complete, valid style (mirrors ThemeHandling.ApplyTheme: only present keys override). The style
// metrics (padding/rounding/…) are theme-independent and always the TiXL values.
void applyColors(const ColorMap& colors);

// Isolation test (ARCHITECTURE.md rule 5): assert defaultTheme() RGBA == the TiXL constants
// (closed-form per-field), with an inject-bug RED leg (perturb one field → mismatch).
int runThemeSelfTest(bool injectBug);

}  // namespace sw::ui::theme
