// app/user_settings — editor user-settings store (cross-session memory: recent files).
// Zone: app (product behavior: cross-session memory). Depends on runtime crude_json only; no imgui.
//
// = TiXL Editor/Gui/UiHelpers/UserSettings.cs (the machine-level userSettings.json store).
//   TiXL persists ProjectDirectories / WindowLayoutIndex / ViewedCanvasAreaForSymbolChildId there.
//   This is #12 of the census 檔/資產/設定軸 (continues #11 keymap_prefs.{h,cpp}); we mirror the
//   SAME machine-level home-dir-JSON shape (~/.simple_world_settings.json, like keymap_prefs and the
//   audio-device-UID prefs), crude_json round-trip.
//
// Part 1 — Recent files (MRU). Fork "recent-files-MRU": TiXL has NO recent-projects MRU list (only a
//   ProjectDirectories folder list with no cap/dedup/order — UserSettings.cs:53). A recent-files MRU
//   is the standard editor affordance #12 asks for ("Open Recent"); we add it with the conventional
//   semantics (move-to-front dedup, most-recent-first, cap 10). The cap mirrors TiXL's layout cap of
//   10 named slots (LayoutHandling.cs) — a sane, TiXL-adjacent ceiling. Persisted/reloaded; the
//   File-menu "Open Recent" UI is a separate consumer (the LIST + persistence is the deliverable).
//
// Part 2 — Window layout: SCOPED OUT here, by design (see DELIVERY note in the lane report). TiXL
//   persists layout via ImGui.SaveIniSettingsToMemory() embedded in named layout JSON
//   (LayoutHandling.cs:218-236). Our app deliberately sets `io.IniFilename = nullptr`
//   (app_delegate.cpp:115, "don't litter imgui.ini") for DETERMINISTIC startup — the eye-hand harness
//   (sw_drive.sh shot coordinate tables) and .scn scenarios depend on a stable default layout. The app
//   also holds NO app-owned window-layout state (no panel-visibility flags, no saved frame, no named
//   layouts) to persist instead. Enabling ini here would make startup non-deterministic and break the
//   harness; persisting an inert layout-index with no consumer would be dead weight. So window-layout
//   is a documented follow-up. The JSON schema below is forward-compatible (a "windowLayoutIndex" int
//   can be added later with no migration) so that follow-up lands cleanly when a layout system exists.
#pragma once

#include <string>
#include <vector>

namespace sw::settings {

// The recent-files MRU + (future) editor user-settings. Pure data + JSON serialization (no imgui), so
// the round-trip is testable headless. Singleton accessor below mirrors keymap_prefs::prefs().
class UserSettings {
 public:
  // ---- Recent files (MRU) -------------------------------------------------
  // Push `path` to the FRONT of the recent list (most-recent-first). If it's already present it is
  // MOVED to the front (dedup), not duplicated. The list is capped at maxRecent() (oldest dropped).
  // Empty paths are ignored. Call this on every successful open/save.
  void pushRecentFile(const std::string& path);
  // The recent files, most-recent-first.
  const std::vector<std::string>& recentFiles() const { return recent_; }
  // Drop one path from the list (e.g. a file the user deleted). No-op if absent.
  void removeRecentFile(const std::string& path);
  void clearRecentFiles() { recent_.clear(); }
  // The cap (max kept). Mirrors TiXL's 10 named-layout slots — a TiXL-adjacent ceiling.
  static int maxRecent() { return 10; }

  // ---- JSON round-trip (crude_json) — pure, no disk so the selftest round-trips headless. --------
  std::string toJson() const;
  bool        fromJson(const std::string& json);  // false on parse failure (store left empty)

  // ---- Disk persistence (home-dir JSON, machine-level; mirrors keymap_prefs) ---------------------
  bool save(const std::string& path) const;
  bool load(const std::string& path);  // missing/empty file => empty store, true; corrupt => false

 private:
  std::vector<std::string> recent_;  // most-recent-first, deduped, capped at maxRecent()
};

// Process-wide singleton + its default home-dir path (mirrors keymap_prefs::prefs/defaultPrefsPath).
UserSettings& settings();
std::string   defaultSettingsPath();
// Convenience: load the home-dir user settings into the singleton at startup (wired from main).
void loadUserSettings();
// Convenience: push a path to the recent list AND persist it (the open/save hook calls this).
void noteRecentFile(const std::string& path);

}  // namespace sw::settings
