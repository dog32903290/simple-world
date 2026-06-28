// view_menu_state_selftest — the gate for the View-menu LIVE CHECK MARK data path.
// --selftest-view-menu-state / --selftest-view-menu-state-bug.
//
// The native View menu shows a check mark on each toggle by calling, in its `menuNeedsUpdate:`
// delegate, viewActionState(i) per item and MenuItem::setState (app/menu.cpp syncViewMenuChecks).
// The OS-side setState wiring is trivial and 柏為 eye-confirms it in the live bar; what is MACHINE-
// verifiable here is the load-bearing data path that feeds it: after the UI registers the action
// table, does invokeViewAction(x) flip the state that viewActionState(x) reports? If that round-trip
// holds for every stateful action, the check mark reflects live state.
//
// Proves:
//   1. After registerViewMenu(), every stateful action (3 windows + Toggle-All UI + Focus Mode) has a
//      live state getter (viewActionState reads a real bool, not the unregistered-false stub).
//   2. invokeViewAction toggles the underlying state and viewActionState reflects the flip both ways
//      (the exact getter syncViewMenuChecks calls, so the menu check mark tracks the live toggle).
//   3. Fullscreen has no enum row (kViewActionCount excludes it) → it is correctly check-less by
//      construction; the loop over [0,kViewActionCount) never touches it.
//   4. -bug: assert the WRONG polarity (state unchanged after a toggle) → RED, proving the test bites.
#include "app/view_menu_actions.h"
#include "ui/view_menu_register.h"
#include "ui/view_modes.h"  // restore g_showChrome / g_focusMode to defaults after the test

#include <cstdio>

namespace sw {

namespace {
using app::invokeViewAction;
using app::viewActionState;
using app::ViewAction;

// One stateful action's round-trip: read -> toggle -> must differ -> toggle back -> must restore.
// `expectFlip` is normally true; -bug passes false to assert the (wrong) "did not change" outcome.
int checkRoundTrip(ViewAction a, const char* name, bool expectFlip) {
  const bool before = viewActionState(a);
  invokeViewAction(a);
  const bool after = viewActionState(a);
  const bool flipped = (after != before);
  int fail = 0;
  if (flipped != expectFlip) {
    std::printf("[view-menu-state] %s: state %d->%d (flipped=%d, expected flip=%d) -> FAIL\n", name,
                before, after, flipped, expectFlip);
    ++fail;
  }
  invokeViewAction(a);  // toggle back to leave global state as we found it
  if (viewActionState(a) != before) {
    std::printf("[view-menu-state] %s: did not restore to %d after toggle-back -> FAIL\n", name,
                before);
    ++fail;
  }
  return fail;
}
}  // namespace

int runViewMenuStateSelfTest(bool injectBug) {
  int fail = 0;

  // Step 1: the UI registers the real (toggle,state) fn-pairs — the same call main.cpp makes at
  // startup. Without it viewActionState returns the unregistered-false stub for everything.
  ui::registerViewMenu();

  // Step 2: the three window toggles + Toggle-All UI all flip cleanly and independently.
  const bool expectFlip = !injectBug;  // -bug expects (wrongly) NO flip -> RED
  fail += checkRoundTrip(app::kAssetsWindow, "AssetsWindow", expectFlip);
  fail += checkRoundTrip(app::kVariationWindow, "VariationWindow", expectFlip);
  fail += checkRoundTrip(app::kThemeWindow, "ThemeWindow", expectFlip);
  fail += checkRoundTrip(app::kToggleAllUi, "ToggleAllUi", expectFlip);

  // Focus Mode is coupled (toggling focus also forces chrome hidden), so test its getter directly
  // rather than via the generic round-trip: focus off -> on must report on, on -> off must report off.
  {
    // Normalize to a known base: focus off, chrome shown.
    if (ui::g_focusMode) ui::toggleFocusMode();
    ui::g_showChrome = true;
    const bool f0 = viewActionState(app::kFocusMode);
    invokeViewAction(app::kFocusMode);  // -> focus on
    const bool f1 = viewActionState(app::kFocusMode);
    if (!injectBug && !(f0 == false && f1 == true)) {
      std::printf("[view-menu-state] FocusMode: off=%d on=%d (want 0 then 1) -> FAIL\n", f0, f1);
      ++fail;
    }
    if (injectBug && (f0 == false && f1 == true)) {
      ++fail;  // -bug: a correct flip here is the "bug not caught" failure
    }
    invokeViewAction(app::kFocusMode);  // restore: focus off
  }

  // Step 3: kViewActionCount excludes Fullscreen by construction — the menu loop in syncViewMenuChecks
  // runs [0, kViewActionCount) and so never sets a check on Fullscreen. Assert the enum shape so a
  // future reorder that accidentally pulls Fullscreen into the stateful range trips this gate.
  if (app::kViewActionCount != 5) {
    std::printf("[view-menu-state] kViewActionCount=%d (want 5: 3 windows + ToggleAll + Focus; "
                "Fullscreen is intentionally NOT in the stateful range) -> FAIL\n",
                (int)app::kViewActionCount);
    ++fail;
  }

  // Restore session view-globals to their launch defaults (focus off, chrome shown) so this test
  // leaves no residue for any other selftest sharing the process.
  if (ui::g_focusMode) ui::toggleFocusMode();
  ui::g_showChrome = true;

  if (injectBug) {
    std::printf("[view-menu-state] injectBug fail count=%d -> %s\n", fail,
                fail > 0 ? "PASS (red-proof)" : "FAIL (bug not caught)");
    return fail > 0 ? 1 : 0;
  }
  std::printf("[view-menu-state] fail=%d -> %s\n", fail, fail == 0 ? "PASS" : "FAIL");
  return fail;
}

}  // namespace sw
