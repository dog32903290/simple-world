// app/keymap_prefs — user keymap override store (cross-session rebinding).
// Zone: app (product behavior: cross-session memory). Depends on runtime crude_json only.
//
// = TiXL Editor/Gui/Interaction/Keyboard/KeyMap.cs + KeyMapSwitching.cs:
//   • KeyMap = List<KeyBinding>, KeyBinding = (UserActions Action, KeyCombination{Key,Ctrl,Shift,Alt}).
//   • AddBinding(action, combo): RemoveAll(b.Action==action) then Add  → user override REPLACES the
//     factory binding for that action (one binding per action in our store). (KeyMap.cs:105-107.)
//   • GetUserOrFactoryKeyMap(): user keymap if a user file exists, else factory (KeyMapSwitching.cs:73).
//     We mirror this as: effectiveChord(action) = user override if present, else the factory default
//     the caller passes in. No user file → zero overrides → factory passes through byte-identical.
//   • Persisted as JSON (KeyMapSwitching.cs:50 JsonUtils.TrySaveJson). TiXL keys it by KeyMap.Name +
//     ".json" under the settings folder. We are a single-user store: one JSON in the home dir
//     (machine-level, like audio_settings' ~/.simple_world_audio_device — NOT per-project).
//
// Fork "single-named-map": TiXL supports multiple named keymaps + switching (KeyMapSwitching). We
// keep ONE user override set (the common rebind case). The JSON schema is forward-compatible (an
// array of {action,key,ctrl,shift,alt}), so a named-map layer could be added later without a
// migration. The ImGui key is stored as its integer enum value (ImGuiKey) for byte-stable round-trip.
#pragma once

#include <string>
#include <vector>

namespace sw::km {

// One rebindable key combination. `key` is the ImGuiKey integer (0 = "no key / unbound").
// ctrl on macOS = physical Cmd (ConfigMacOSXBehaviors swap; see keymap.cpp memory note).
struct KeyChord {
  int  key   = 0;      // ImGuiKey value (ImGuiKey_None == 0 means unbound)
  bool ctrl  = false;
  bool shift = false;
  bool alt   = false;

  bool operator==(const KeyChord& o) const {
    return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt;
  }
  bool operator!=(const KeyChord& o) const { return !(*this == o); }
};

// The user override store. In-memory map action-name -> KeyChord (one binding per action, matching
// TiXL AddBinding's RemoveAll+Add). Pure data + JSON serialization; no imgui dependency so the
// round-trip is testable headless.
class KeymapPrefs {
 public:
  // Set/replace the override for an action (= TiXL AddBinding: one binding per action).
  void setOverride(const std::string& action, const KeyChord& chord);
  // Remove an action's override (revert to factory). = TiXL RemoveBinding.
  void clearOverride(const std::string& action);
  // True if `action` has a user override.
  bool hasOverride(const std::string& action) const;
  // The override for `action`; valid only if hasOverride(action).
  KeyChord override_(const std::string& action) const;
  // Effective chord: user override if present, else the factory default passed in
  // (= TiXL GetUserOrFactoryKeyMap, applied per action).
  KeyChord effective(const std::string& action, const KeyChord& factoryDefault) const;

  int  size() const;     // number of overrides
  void clearAll();       // wipe all overrides (test helper / "reset to factory")

  // JSON round-trip (crude_json). toJson()/fromJson() are pure (no disk) so the selftest can
  // round-trip without touching the filesystem.
  std::string toJson() const;
  bool        fromJson(const std::string& json);  // false on parse failure (store left empty)

  // Disk persistence (home-dir JSON, machine-level). save() writes the current overrides; load()
  // replaces the in-memory overrides with the file's (missing file => empty => pure factory, no error).
  bool save(const std::string& path) const;
  bool load(const std::string& path);  // false only on a present-but-corrupt file

 private:
  struct Entry { std::string action; KeyChord chord; };
  std::vector<Entry> entries_;
  int indexOf(const std::string& action) const;
};

// The process-wide singleton store + its default home-dir path (mirrors audio_settings::prefsPath).
KeymapPrefs& prefs();
std::string  defaultPrefsPath();
// Convenience: load the home-dir user keymap into the singleton at startup (wired from main).
void loadUserKeymap();

}  // namespace sw::km
