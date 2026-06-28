// app/view_menu_actions — see view_menu_actions.h. Stores the fn-ptr table the UI registers and
// lets app/menu.cpp call through it without ever including a ui/* header (leaf-inversion seam).
#include "app/view_menu_actions.h"

namespace sw::app {
namespace {
ViewMenuAction s_actions[kViewActionCount];  // zero-init: all nullptr until UI registers
}  // namespace

void registerViewMenuActions(const ViewMenuAction (&actions)[kViewActionCount]) {
  for (int i = 0; i < kViewActionCount; ++i) s_actions[i] = actions[i];
}

void invokeViewAction(ViewAction which) {
  if (which < 0 || which >= kViewActionCount) return;
  if (s_actions[which].toggle) s_actions[which].toggle();
}

bool viewActionState(ViewAction which) {
  if (which < 0 || which >= kViewActionCount) return false;
  return s_actions[which].state ? s_actions[which].state() : false;
}

}  // namespace sw::app
