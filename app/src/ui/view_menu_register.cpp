// ui/view_menu_register — UI side of the View-menu leaf-inversion seam (app/view_menu_actions).
// Zone: ui. This is the ONE place allowed to bridge the native View menu to the real UI toggles:
// it includes both the ui/ toggle headers AND the app/ seam header, then registers each action's
// (toggle, state) fn-pair with the app seam. main.cpp calls registerViewMenu() once at startup
// (same spot it wires setOsFullScreenFn). app/menu.cpp then calls back through the seam — so
// app/menu.cpp itself includes NO ui/* header and ui→app stays one-way.
//
// The toggle fns are non-capturing (plain function pointers). State getters read the live
// visibility bool the window's draw fn checks, so a future native validator can show a check mark.
#include "ui/view_menu_register.h"

#include "ui/asset_browser.h"     // assetBrowserVisible()
#include "ui/theme_editor.h"      // themeEditorVisible()
#include "ui/variation_panel.h"   // variationPanelVisible()
#include "ui/view_modes.h"        // toggleShowChrome / toggleFocusMode

#include "app/view_menu_actions.h"

namespace sw::ui {

void registerViewMenu() {
  sw::app::ViewMenuAction actions[sw::app::kViewActionCount] = {};

  actions[sw::app::kAssetsWindow] = {
      [] { sw::ui::assetBrowserVisible() = !sw::ui::assetBrowserVisible(); },
      [] { return sw::ui::assetBrowserVisible(); }};
  actions[sw::app::kVariationWindow] = {
      [] { sw::ui::variationPanelVisible() = !sw::ui::variationPanelVisible(); },
      [] { return sw::ui::variationPanelVisible(); }};
  actions[sw::app::kThemeWindow] = {
      [] { sw::ui::themeEditorVisible() = !sw::ui::themeEditorVisible(); },
      [] { return sw::ui::themeEditorVisible(); }};
  // Toggle-All UI + Focus Mode are momentary flips (no meaningful persistent check mark) → state null.
  actions[sw::app::kToggleAllUi] = {[] { sw::ui::toggleShowChrome(); }, nullptr};
  actions[sw::app::kFocusMode] = {[] { sw::ui::toggleFocusMode(); }, nullptr};

  sw::app::registerViewMenuActions(actions);
}

}  // namespace sw::ui
