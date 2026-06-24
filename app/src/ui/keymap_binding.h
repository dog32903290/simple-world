// ui/keymap_binding — rebindable-chord resolver between keymap.cpp's handlers and the user
// override store (app/keymap_prefs). Zone: ui. Depends on app(keymap_prefs) + imgui.
//
// keymap.cpp is at its line-count cap (ARCHITECTURE rule 4 ratchet); this leaf holds the
// override-aware key test so a handler swaps a hardcoded `ImGui::IsKeyPressed(ImGuiKey_X)` for a
// line-neutral `chordPressed("Action", {ImGuiKey_X,...})`. The resolver asks the user store for the
// effective chord (user override else the factory default passed in = TiXL GetUserOrFactoryKeyMap)
// and reports whether that chord was pressed this frame.
#pragma once

#include "app/keymap_prefs.h"  // sw::km::KeyChord / prefs()

namespace sw::ui {

// True iff the effective binding for `action` (user override if any, else `factoryDefault`) was
// pressed this frame. The chord's ctrl/shift/alt are matched exactly against io modifiers; key uses
// ImGui::IsKeyPressed(repeat=false). An unbound chord (key==0) never fires. This is the single live
// seam that makes a rebound key actually take effect.
bool chordPressed(const char* action, const sw::km::KeyChord& factoryDefault);

}  // namespace sw::ui
