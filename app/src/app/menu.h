#pragma once
#include <AppKit/AppKit.hpp>

namespace sw::menu {

// Build the whole main menu bar from the static menu tables (see menu.cpp).
// Data-driven: adding a menu item is one row in a table, not four lines of
// boilerplate — so the menu can grow to TiXL scale without bloating.
NS::Menu* buildMainMenu();

}  // namespace sw::menu
