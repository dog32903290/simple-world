#pragma once
// ui/theme_editor — the Color Theme Editor window (= TiXL Editor/Gui/Styling/ColorThemeEditor.cs).
// A standalone floating tool window (like Variation / Asset Library), default OFF, opened on demand
// from the toolbar "Theme" button. Structure mirrors ColorThemeEditor.DrawEditor:
//   • theme dropdown (select → SetThemeAsUserTheme + live apply)
//   • Name / Author string inputs (change-tracked)
//   • Save (disabled unless changed) / Save As / Delete
//   • per-field color edits (ColorEdit4 over ui::theme::fieldNames()), live T3Style.Apply on edit
// Registry + persistence live in app/theme_registry (app zone); this file is the imgui surface only.
//
// FORK (named): "variations-deferred" — TiXL also edits the ColorVariations (HSV b/s/op) here; sw's
// variations aren't a themed/serialized table yet (they live in node_style.cpp), so that section is
// deferred. See app/theme_registry.h. The Colors section edits the 28 UiColors fields sw routes
// through the theme (incl. the canvas CanvasBackground/CanvasGrid sw renders) — NOT all of TiXL's 53
// themable UiColors fields. The other 25 (Widget*, Status*, GridLines, MiniMapItems, Gray, TextMuted,
// …) are DEFERRED (no sw consumer wired yet); see the "themed-field-subset" fork note in
// app/theme_registry.h.
namespace sw::ui {

// Window visibility (default OFF). Toggled from the toolbar, TiXL-tool-window style.
bool& themeEditorVisible();

// Draw the Color Theme Editor window. Call once per frame alongside drawVariationPanel / etc.
// No-op when themeEditorVisible() is false.
void drawThemeEditor();

}  // namespace sw::ui
