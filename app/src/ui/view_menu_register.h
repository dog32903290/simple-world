// ui/view_menu_register — see view_menu_register.cpp. Mounts the native View menu's UI bridge.
#pragma once

namespace sw::ui {

// Register the View-menu action fn-ptrs with the app seam (app/view_menu_actions). Call once at
// startup from main.cpp, alongside setOsFullScreenFn. After this, the native NSMenu View items
// reach the real UI toggles through the seam — without app/menu.cpp including any ui/* header.
void registerViewMenu();

}  // namespace sw::ui
