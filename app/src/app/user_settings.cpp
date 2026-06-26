#include "app/user_settings.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "crude_json.h"  // third_party/imgui-node-editor (on the include path; same as keymap_prefs)

namespace sw::settings {

// ---------------------------------------------------------------------------
// Recent files (MRU): move-to-front dedup, most-recent-first, capped.
// ---------------------------------------------------------------------------
void UserSettings::pushRecentFile(const std::string& path) {
  if (path.empty()) return;
  // Dedup: drop an existing occurrence so the re-opened file moves to the front (not duplicated).
  for (auto it = recent_.begin(); it != recent_.end(); ++it) {
    if (*it == path) {
      recent_.erase(it);
      break;
    }
  }
  recent_.insert(recent_.begin(), path);     // most-recent-first
  if ((int)recent_.size() > maxRecent())     // cap: drop the oldest (back)
    recent_.resize(maxRecent());
}

void UserSettings::removeRecentFile(const std::string& path) {
  for (auto it = recent_.begin(); it != recent_.end(); ++it) {
    if (*it == path) {
      recent_.erase(it);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// JSON: { version, recentFiles:[...] }. Pure (no disk) so the round-trip is testable.
// version + the object root leave room for a forward-compatible "windowLayoutIndex" etc. later.
// ---------------------------------------------------------------------------
std::string UserSettings::toJson() const {
  crude_json::array arr;
  for (const std::string& p : recent_) arr.push_back(crude_json::value(p));
  crude_json::object root;
  root["version"]        = (crude_json::number)1;
  root["recentFiles"]    = crude_json::value(arr);
  root["colorThemeName"] = crude_json::value(colorThemeName_);
  return crude_json::value(root).dump(2);
}

bool UserSettings::fromJson(const std::string& json) {
  recent_.clear();
  colorThemeName_.clear();
  crude_json::value root = crude_json::value::parse(json);
  if (root.is_discarded() || !root.is_object()) return false;
  if (root.contains("colorThemeName") && root["colorThemeName"].is_string())
    colorThemeName_ = root["colorThemeName"].get<crude_json::string>();
  if (root.contains("recentFiles") && root["recentFiles"].is_array()) {
    for (auto& e : root["recentFiles"].get<crude_json::array>()) {
      if (!e.is_string()) continue;
      const std::string p = e.get<crude_json::string>();
      if (p.empty()) continue;
      // Re-run through pushRecentFile semantics in REVERSE so the on-disk order (most-recent-first)
      // is preserved after dedup/cap. Simpler: push each tail-first. Here we just append + cap,
      // trusting the file we wrote; a hand-edited file with dups collapses to first-wins, capped.
      bool dup = false;
      for (const std::string& q : recent_)
        if (q == p) { dup = true; break; }
      if (!dup && (int)recent_.size() < maxRecent()) recent_.push_back(p);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Disk persistence (home-dir JSON, machine-level; mirrors keymap_prefs::save/load).
// ---------------------------------------------------------------------------
bool UserSettings::save(const std::string& path) const {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << toJson();
  return (bool)f;
}

bool UserSettings::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) {  // missing file => empty store (no error)
    recent_.clear();
    colorThemeName_.clear();
    return true;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string contents = ss.str();
  if (contents.empty()) {  // empty file => empty store
    recent_.clear();
    colorThemeName_.clear();
    return true;
  }
  return fromJson(contents);  // present-but-corrupt => false (recent_ left empty by fromJson)
}

// ---------------------------------------------------------------------------
// Process-wide singleton + home-dir path (mirrors keymap_prefs::prefs/defaultPrefsPath).
// ---------------------------------------------------------------------------
UserSettings& settings() {
  static UserSettings g;  // Meyers singleton; constructed on first use, before any frame.
  return g;
}

std::string defaultSettingsPath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.simple_world_settings.json";
}

void loadUserSettings() { settings().load(defaultSettingsPath()); }

void noteRecentFile(const std::string& path) {
  settings().pushRecentFile(path);
  settings().save(defaultSettingsPath());  // persist immediately (machine-level, crude_json)
}

}  // namespace sw::settings
