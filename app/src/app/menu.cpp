// app/menu — the app's menu bar, defined as data tables.
// Zone: app. One row per menu item; a builder turns tables into NSMenus.
// Adding a menu item = add one row below. No per-item boilerplate.
#include "app/menu.h"

#include "app/document.h"

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

}  // namespace

NS::Menu* buildMainMenu() {
  NS::Menu* mainMenu = NS::Menu::alloc()->init();
  NS::Menu* appMenu = buildMenu("simple_world", kAppMenu);
  NS::Menu* fileMenu = buildMenu("File", kFileMenu);
  NS::Menu* windowMenu = buildMenu("Window", kWindowMenu);
  addSubmenu(mainMenu, appMenu);
  addSubmenu(mainMenu, fileMenu);
  addSubmenu(mainMenu, windowMenu);
  appMenu->release();
  fileMenu->release();
  windowMenu->release();
  return mainMenu->autorelease();
}

}  // namespace sw::menu
