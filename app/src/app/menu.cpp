// app/menu — the macOS NATIVE NSMenu, defined as data tables.
// Zone: app. One row per menu item; a builder turns tables into NSMenus.
// Adding a menu item = add one row below. No per-item boilerplate.
//
// FORK (named: fork-menubar-nsmenu-primary): the native NSMenu is the SOLE menu bar.
// macOS draws its menu bar at the top of the screen; a second in-window imgui menu bar would stack
// two bars and is macOS-wrong, so 柏為 (2026-06-29) ruled OUT the in-window imgui bar (the former
// ui/menu_bar.cpp = TiXL AppMenuBar.cs, now removed). TiXL is not a macOS app and has no native
// menu, so 照TiXL does not cover the NSMenu question — this fork answers it: ALL user-facing menu
// content (File / Edit / View / Window) lives HERE, in the native bar.
//   • File   — New/Open/Save/Save As (Cmd+N/O/S/Shift+S key-equivalents; the ONLY binding for these).
//   • Edit   — Undo/Redo (Cmd+Z / Cmd+Shift+Z); also fire via the imgui canvas, so the menu just
//              adds the visible entry — no key collision.
//   • View   — Assets/Variation/Theme windows + Toggle-All UI + Focus Mode + Fullscreen.
//   • Window — Close Window (Cmd+W key-equivalent).
// The View toggles live in the UI zone; app/menu.cpp MUST NOT include ui/* (ui→app is one-way). So
// View reaches the UI through a startup-registered fn-ptr seam (app/view_menu_actions, filled by
// ui/view_menu_register at startup), exactly like the OS-fullscreen inversion. Fullscreen reaches
// platform directly (app→platform is legal).
#include "app/menu.h"

#include "app/command.h"           // g_commands undo/redo + canUndo/canRedo
#include "app/document.h"
#include "app/view_menu_actions.h"  // View leaf-inversion seam (invokeViewAction; UI registers it)
#include "platform/menu_appkit_ext.h"  // live check marks + menuNeedsUpdate: delegate — app→platform legal
#include "platform/window_mode.h"  // toggleOsFullScreen (View > Fullscreen) — app→platform legal
#include "verify/eye/eye.h"  // one-line hook: native menu rows -> map.json (鐵律 3)

namespace sw::menu {
namespace {

using NS::StringEncoding::UTF8StringEncoding;

const auto Cmd = NS::EventModifierFlagCommand;
const auto CmdShift = NS::EventModifierFlagCommand | NS::EventModifierFlagShift;

// One menu item as data: title, key equivalent, modifier, a unique callback
// name, and a NON-capturing callback (so it converts to a C function pointer).
struct MenuItemDef {
  const char* title;
  const char* key;
  NS::KeyEquivalentModifierMask mods;
  const char* cbName;
  NS::MenuItemCallback cb;
};

NS::String* str(const char* s) { return NS::String::string(s, UTF8StringEncoding); }

// Build one submenu from a table. Template recovers the array length, so callers
// write buildMenu("File", kFileMenu) with no count argument.
template <unsigned long N>
NS::Menu* buildMenu(const char* title, const MenuItemDef (&items)[N]) {
  NS::Menu* menu = NS::Menu::alloc()->init(str(title));
  for (unsigned long i = 0; i < N; ++i) {
    SEL cb = NS::MenuItem::registerActionCallback(items[i].cbName, items[i].cb);
    NS::MenuItem* it = menu->addItem(str(items[i].title), cb, str(items[i].key));
    it->setKeyEquivalentModifierMask(items[i].mods);
    // one central hook = the whole data table lands in map.json (builder pattern, 鐵律 7)
    sw::eye::recordNativeMenuItem(title, items[i].title, items[i].key,
                                  (items[i].mods & NS::EventModifierFlagShift) != 0);
  }
  return menu;
}

void addSubmenu(NS::Menu* mainMenu, NS::Menu* sub) {
  NS::MenuItem* item = NS::MenuItem::alloc()->init();
  item->setSubmenu(sub);
  mainMenu->addItem(item);
  item->release();
}

// View-menu check marks. The native menu has no per-item validator, so we refresh state the
// macOS-correct way: an NSMenu `menuNeedsUpdate:` delegate fires right before the View submenu is
// shown, and we set each item's check from the app-zone seam getter (viewActionState). The View
// items are added 1:1 from kViewMenu, so item index i == ViewAction(i) for the first kViewActionCount
// rows; row index kViewActionCount (Fullscreen) has no seam state and stays uncheck-able (it is also
// visually obvious to the user). Non-capturing → converts to the platform delegate's C fn-ptr slot.
// The AppKit binding (setState / item-walk / delegate install) lives in platform/menu_appkit_ext
// (tracked code), NOT the gitignored vendored metal-cpp headers.
void syncViewMenuChecks(void*, void*, NS::Menu* viewMenu) {
  if (!viewMenu) return;
  const NS::Integer n = sw::platform::menuItemCount(viewMenu);
  for (NS::Integer i = 0; i < n && i < sw::app::kViewActionCount; ++i) {
    NS::MenuItem* it = sw::platform::menuItemAt(viewMenu, i);
    if (it)
      sw::platform::setMenuItemChecked(
          it, sw::app::viewActionState(static_cast<sw::app::ViewAction>(i)));
  }
}

// ---- The menu tables. Add a row to grow a menu. ----------------------------

const MenuItemDef kAppMenu[] = {
    {"Quit simple_world", "q", Cmd, "appQuit",
     [](void*, SEL, const NS::Object* s) {
       if (sw::doc::confirmDiscardIfDirty()) NS::Application::sharedApplication()->terminate(s);
     }},
};

const MenuItemDef kFileMenu[] = {
    {"New", "n", Cmd, "fileNew", [](void*, SEL, const NS::Object*) { sw::doc::doNew(); }},
    {"Open…", "o", Cmd, "fileOpen", [](void*, SEL, const NS::Object*) { sw::doc::doOpen(); }},
    {"Save", "s", Cmd, "fileSave", [](void*, SEL, const NS::Object*) { sw::doc::doSave(); }},
    {"Save As…", "s", CmdShift, "fileSaveAs", [](void*, SEL, const NS::Object*) { sw::doc::doSaveAs(); }},
};

// Edit (Undo/Redo). Reuses the EXACT g_commands path the imgui canvas Cmd+Z uses, then sets the
// status line + asks for a relayout — same as the canvas handler. Undo/Redo no-op safely on an
// empty stack, so we leave them always-enabled (native menu validation could grey them via the same
// menuNeedsUpdate: delegate the View menu uses for check marks, but graying undo/redo is not asked
// for here).
const MenuItemDef kEditMenu[] = {
    {"Undo", "z", Cmd, "editUndo",
     [](void*, SEL, const NS::Object*) {
       sw::g_commands.undo(); sw::doc::g_status = "undo"; sw::doc::g_relayout = true;
     }},
    {"Redo", "z", CmdShift, "editRedo",
     [](void*, SEL, const NS::Object*) {
       sw::g_commands.redo(); sw::doc::g_status = "redo"; sw::doc::g_relayout = true;
     }},
};

// View. Window toggles + Toggle-All UI + Focus Mode reach the UI via the app/view_menu_actions
// seam (UI registers the fns at startup); Fullscreen reaches platform directly. No key-equivalents
// here — those toggles are bound by ui/keymap (F11/F12/Shift+Esc); the menu just adds the entries.
const MenuItemDef kViewMenu[] = {
    {"Assets Window", "", 0, "viewAssets",
     [](void*, SEL, const NS::Object*) { sw::app::invokeViewAction(sw::app::kAssetsWindow); }},
    {"Variation Window", "", 0, "viewVariation",
     [](void*, SEL, const NS::Object*) { sw::app::invokeViewAction(sw::app::kVariationWindow); }},
    {"Theme Window", "", 0, "viewTheme",
     [](void*, SEL, const NS::Object*) { sw::app::invokeViewAction(sw::app::kThemeWindow); }},
    {"Toggle All UI", "", 0, "viewToggleAll",
     [](void*, SEL, const NS::Object*) { sw::app::invokeViewAction(sw::app::kToggleAllUi); }},
    {"Focus Mode", "", 0, "viewFocus",
     [](void*, SEL, const NS::Object*) { sw::app::invokeViewAction(sw::app::kFocusMode); }},
    {"Enter Full Screen", "f", Cmd | NS::EventModifierFlagControl, "viewFullscreen",
     [](void*, SEL, const NS::Object*) { sw::platform::toggleOsFullScreen(); }},
};

const MenuItemDef kWindowMenu[] = {
    {"Close Window", "w", Cmd, "windowClose",
     [](void*, SEL, const NS::Object*) {
       if (sw::doc::confirmDiscardIfDirty())
         NS::Application::sharedApplication()->windows()->object<NS::Window>(0)->close();
     }},
};

}  // namespace

NS::Menu* buildMainMenu() {
  NS::Menu* mainMenu = NS::Menu::alloc()->init();
  // Standard macOS order: App / File / Edit / View / Window (fork-menubar-nsmenu-primary above).
  NS::Menu* appMenu = buildMenu("simple_world", kAppMenu);
  NS::Menu* fileMenu = buildMenu("File", kFileMenu);
  NS::Menu* editMenu = buildMenu("Edit", kEditMenu);
  NS::Menu* viewMenu = buildMenu("View", kViewMenu);
  // Live check marks: refresh each View toggle's state from the seam right before the menu opens.
  sw::platform::installMenuUpdateDelegate(viewMenu, "SwViewMenuDelegate", &syncViewMenuChecks);
  NS::Menu* windowMenu = buildMenu("Window", kWindowMenu);
  addSubmenu(mainMenu, appMenu);
  addSubmenu(mainMenu, fileMenu);
  addSubmenu(mainMenu, editMenu);
  addSubmenu(mainMenu, viewMenu);
  addSubmenu(mainMenu, windowMenu);
  appMenu->release();
  fileMenu->release();
  editMenu->release();
  viewMenu->release();
  windowMenu->release();
  return mainMenu->autorelease();
}

}  // namespace sw::menu
