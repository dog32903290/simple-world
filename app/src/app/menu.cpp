// app/menu — the macOS NATIVE NSMenu, defined as data tables.
// Zone: app. One row per menu item; a builder turns tables into NSMenus.
// Adding a menu item = add one row below. No per-item boilerplate.
//
// FORK (named: fork-menubar-imgui-primary-nsmenu-minimal-appmenu): the user-facing menu content
// (File/Edit/View) now lives in the imgui top menu bar (ui/menu_bar.cpp, = TiXL AppMenuBar.cs).
// TiXL is not a macOS app and has no native menu, so 照TiXL does not cover the NSMenu question.
// We KEEP a MINIMAL NSMenu because macOS expects it (Cmd-Q is a system convention) AND because the
// File/Window key-equivalents below are the ONLY binding for Cmd-N/O/S/Shift-S/W — imgui MenuItem
// shortcut strings are display-only and register no handlers, and keymap.cpp does not bind these.
// So the File + Window submenus stay as HIDDEN KEY-EQUIVALENT CARRIERS (their VISIBLE content is
// duplicated in the imgui bar). The redundant View submenu was dropped (its Fullscreen is bound by
// keymap F11 + the imgui View menu). This is TiXL-shaped (imgui bar primary) AND macOS-correct, and
// is fully reversible (re-add addSubmenu rows to restore any NSMenu submenu).
#include "app/menu.h"

#include "app/document.h"
#include "platform/window_mode.h"  // P6 toggleOsFullScreen (View > Fullscreen, modes.md [polish])
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

const MenuItemDef kWindowMenu[] = {
    {"Close Window", "w", Cmd, "windowClose",
     [](void*, SEL, const NS::Object*) {
       if (sw::doc::confirmDiscardIfDirty())
         NS::Application::sharedApplication()->windows()->object<NS::Window>(0)->close();
     }},
};

// P6 OS-fullscreen (modes.md [polish]): one item in a new View submenu.
// Key equivalent "^$f" = Ctrl+Cmd+F (macOS convention for Enter Full Screen).
// F11 is also bound via keymap.cpp (handleOsFullScreen) as the TiXL-parity shortcut.
[[maybe_unused]] const MenuItemDef kViewMenu[] = {
    {"Enter Full Screen", "f", Cmd | NS::EventModifierFlagControl, "viewFullscreen",
     [](void*, SEL, const NS::Object*) { sw::platform::toggleOsFullScreen(); }},
};

}  // namespace

NS::Menu* buildMainMenu() {
  NS::Menu* mainMenu = NS::Menu::alloc()->init();
  // Minimal NSMenu per the fork above: App (Quit) + File/Window key-equivalent carriers. The View
  // submenu is intentionally NOT assembled — kViewMenu (Enter Full Screen) stays in the table for
  // reversibility but its Fullscreen is reachable via keymap F11 and the imgui View menu.
  NS::Menu* appMenu = buildMenu("simple_world", kAppMenu);
  NS::Menu* fileMenu = buildMenu("File", kFileMenu);  // carries Cmd-N/O/S/Shift-S key equivalents
  NS::Menu* windowMenu = buildMenu("Window", kWindowMenu);  // carries Cmd-W key equivalent
  addSubmenu(mainMenu, appMenu);
  addSubmenu(mainMenu, fileMenu);
  addSubmenu(mainMenu, windowMenu);
  appMenu->release();
  fileMenu->release();
  windowMenu->release();
  return mainMenu->autorelease();
}

}  // namespace sw::menu
