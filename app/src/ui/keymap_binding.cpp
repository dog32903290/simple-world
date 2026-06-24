// ui/keymap_binding — see keymap_binding.h.
#include "ui/keymap_binding.h"

#include "imgui.h"

namespace sw::ui {

bool chordPressed(const char* action, const sw::km::KeyChord& factoryDefault) {
  const sw::km::KeyChord c = sw::km::prefs().effective(action ? action : "", factoryDefault);
  if (c.key == 0) return false;  // unbound -> never fires
  const ImGuiIO& io = ImGui::GetIO();
  // Exact modifier match (= TiXL KeyCombination.Matches: Ctrl/Shift/Alt must all agree).
  // On macOS io.KeyCtrl is physical Cmd (ConfigMacOSXBehaviors swap), matching keymap.cpp's convention.
  if (io.KeyCtrl != c.ctrl || io.KeyShift != c.shift || io.KeyAlt != c.alt) return false;
  return ImGui::IsKeyPressed((ImGuiKey)c.key, /*repeat=*/false);
}

}  // namespace sw::ui
