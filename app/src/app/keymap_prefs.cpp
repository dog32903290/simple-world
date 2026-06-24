#include "app/keymap_prefs.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "crude_json.h"  // third_party/imgui-node-editor (on the include path; same as curve_json.h)

namespace sw::km {

// ---------------------------------------------------------------------------
// KeymapPrefs — in-memory override store (one binding per action = TiXL AddBinding RemoveAll+Add).
// ---------------------------------------------------------------------------
int KeymapPrefs::indexOf(const std::string& action) const {
  for (int i = 0; i < (int)entries_.size(); ++i)
    if (entries_[i].action == action) return i;
  return -1;
}

void KeymapPrefs::setOverride(const std::string& action, const KeyChord& chord) {
  const int i = indexOf(action);
  if (i >= 0) {
    entries_[i].chord = chord;  // replace (= AddBinding: one binding per action)
  } else {
    entries_.push_back({action, chord});
  }
}

void KeymapPrefs::clearOverride(const std::string& action) {
  const int i = indexOf(action);
  if (i >= 0) entries_.erase(entries_.begin() + i);
}

bool KeymapPrefs::hasOverride(const std::string& action) const { return indexOf(action) >= 0; }

KeyChord KeymapPrefs::override_(const std::string& action) const {
  const int i = indexOf(action);
  return i >= 0 ? entries_[i].chord : KeyChord{};
}

KeyChord KeymapPrefs::effective(const std::string& action, const KeyChord& factoryDefault) const {
  const int i = indexOf(action);
  return i >= 0 ? entries_[i].chord : factoryDefault;  // user override else factory (GetUserOrFactory)
}

int  KeymapPrefs::size() const { return (int)entries_.size(); }
void KeymapPrefs::clearAll() { entries_.clear(); }

// ---------------------------------------------------------------------------
// JSON: an array of {action,key,ctrl,shift,alt}. Pure (no disk) so the round-trip is testable.
// ---------------------------------------------------------------------------
std::string KeymapPrefs::toJson() const {
  crude_json::array arr;
  for (const Entry& e : entries_) {
    crude_json::object o;
    o["action"] = e.action;
    o["key"]    = (crude_json::number)e.chord.key;
    o["ctrl"]   = e.chord.ctrl;
    o["shift"]  = e.chord.shift;
    o["alt"]    = e.chord.alt;
    arr.push_back(crude_json::value(o));
  }
  crude_json::object root;
  root["version"]  = (crude_json::number)1;
  root["bindings"] = crude_json::value(arr);
  return crude_json::value(root).dump(2);
}

bool KeymapPrefs::fromJson(const std::string& json) {
  entries_.clear();
  crude_json::value root = crude_json::value::parse(json);
  if (root.is_discarded() || !root.is_object()) return false;
  if (!root.contains("bindings") || !root["bindings"].is_array()) return false;
  for (auto& b : root["bindings"].get<crude_json::array>()) {
    if (!b.is_object() || !b.contains("action") || !b["action"].is_string()) continue;
    const std::string action = b["action"].get<crude_json::string>();
    if (action.empty()) continue;
    KeyChord c;
    if (b.contains("key")   && b["key"].is_number())    c.key   = (int)b["key"].get<crude_json::number>();
    if (b.contains("ctrl")  && b["ctrl"].is_boolean())  c.ctrl  = b["ctrl"].get<crude_json::boolean>();
    if (b.contains("shift") && b["shift"].is_boolean()) c.shift = b["shift"].get<crude_json::boolean>();
    if (b.contains("alt")   && b["alt"].is_boolean())   c.alt   = b["alt"].get<crude_json::boolean>();
    setOverride(action, c);  // dedup-by-action (last wins), same as AddBinding
  }
  return true;
}

bool KeymapPrefs::save(const std::string& path) const {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << toJson();
  return (bool)f;
}

bool KeymapPrefs::load(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    entries_.clear();  // missing file => pure factory (no overrides), not an error
    return true;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string contents = ss.str();
  if (contents.empty()) {  // empty file => pure factory
    entries_.clear();
    return true;
  }
  return fromJson(contents);  // present-but-corrupt => false (entries_ left empty by fromJson)
}

// ---------------------------------------------------------------------------
// Process-wide singleton + home-dir path (mirrors audio_settings::prefsPath).
// ---------------------------------------------------------------------------
KeymapPrefs& prefs() {
  static KeymapPrefs g;  // Meyers singleton; constructed on first use, before any frame.
  return g;
}

std::string defaultPrefsPath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.simple_world_keymap.json";
}

void loadUserKeymap() { prefs().load(defaultPrefsPath()); }

}  // namespace sw::km
