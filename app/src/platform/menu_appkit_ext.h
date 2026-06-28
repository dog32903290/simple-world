// platform/menu_appkit_ext — AppKit NSMenu/NSMenuItem behaviors metal-cpp's vendored
// wrappers do not expose: live check marks (NSMenuItem.state) + a `menuNeedsUpdate:`
// delegate to refresh them, plus item-walk helpers for the delegate callback.
//
// Zone: platform. macOS only: pure ObjC-runtime (objc_msgSend) calls on the raw
// `id` underlying each metal-cpp NS:: pointer — the same boundary window_mode.mm sits
// at (AppKit internals belong here, not in app/ or ui/). app→platform is legal, so
// app/menu.cpp reaches these through this header (mirrors platform/window_mode.h).
//
// WHY HERE, NOT IN THE VENDORED metal-cpp HEADERS: app/third_party/ is gitignored, so
// any extension added there vanishes on a clean checkout and the committed build breaks.
// These free functions are the tracked home for the sw-specific NSMenu extension.
#pragma once

namespace NS {
class Menu;
class MenuItem;
using Integer = long;  // matches metal-cpp's NS::Integer (NSInteger == long on 64-bit).
}  // namespace NS

namespace sw::platform {

// Set a menu item's check mark. NSControlStateValue: Off = 0, On = 1.
void setMenuItemChecked(NS::MenuItem* item, bool checked);

// Item access so a delegate callback can walk a menu's rows.
NS::Integer menuItemCount(NS::Menu* menu);
NS::MenuItem* menuItemAt(NS::Menu* menu, NS::Integer index);

// Install a `menuNeedsUpdate:` delegate on `menu`. macOS fires it right before the menu
// is displayed — the correct place to refresh check marks from live state. `cb` receives
// the menu being updated; loop its items (menuItemCount / menuItemAt) and setMenuItemChecked
// each. A one-off delegate class (NSObject subclass with the supplied C fn as its
// `menuNeedsUpdate:` IMP) is built once; an instance is created and set as the delegate.
//
// LIFETIME: the delegate instance is allocated ONCE (app-lifetime, one per menu — these are
// the menu-bar menus that live the whole app run) and deliberately never released. NSMenu's
// `delegate` is a WEAK reference, so we must keep it alive ourselves; a single permanent
// allocation is intentional, not a per-frame growing leak (metal-cpp-discipline Rule 2/3
// targets per-frame churn, which this is the explicit exception to).
typedef void (*MenuNeedsUpdateCallback)(void* unused, void* sel, NS::Menu* menu);
void installMenuUpdateDelegate(NS::Menu* menu, const char* delegateClassName,
                               MenuNeedsUpdateCallback cb);

}  // namespace sw::platform
