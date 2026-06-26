#include "app/output_window_state.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include "crude_json.h"  // third_party/imgui-node-editor (on the include path; same as user_settings)

namespace sw::settings {

const OutputWindowState OutputWindowStore::kDefault_{};

namespace {
// Exact float equality is fine here: the values are round-tripped through the same JSON path, and the
// selftest asserts persistence preserves them bit-for-bit (no arithmetic in between).
bool colorEq(const float (&a)[4], const float (&b)[4]) {
  for (int i = 0; i < 4; ++i)
    if (a[i] != b[i]) return false;
  return true;
}
}  // namespace

bool OutputWindowState::operator==(const OutputWindowState& o) const {
  return isPinned == o.isPinned && pinnedNode == o.pinnedNode &&
         resolutionTitle == o.resolutionTitle && resolutionWidth == o.resolutionWidth &&
         resolutionHeight == o.resolutionHeight &&
         resolutionUseAsAspectRatio == o.resolutionUseAsAspectRatio &&
         colorEq(backgroundColor, o.backgroundColor);
}

void OutputWindowStore::setState(const OutputWindowState& s) {
  if (states_.empty())
    states_.push_back(s);
  else
    states_.front() = s;
}

// ---------------------------------------------------------------------------
// JSON: { version, OutputWindows:[ {state...} ] }. The "OutputWindows" array key is TiXL's exact
// name (OutputWindowState.cs:79). Pure (no disk) so the round-trip is testable headless.
// ---------------------------------------------------------------------------
namespace {
crude_json::value stateToJson(const OutputWindowState& s) {
  crude_json::object o;
  o["isPinned"]   = s.isPinned;
  o["pinnedNode"] = (crude_json::number)s.pinnedNode;
  o["resolutionTitle"] = s.resolutionTitle;
  o["resolutionWidth"]  = (crude_json::number)s.resolutionWidth;
  o["resolutionHeight"] = (crude_json::number)s.resolutionHeight;
  o["resolutionUseAsAspectRatio"] = s.resolutionUseAsAspectRatio;
  crude_json::array bg;
  for (int i = 0; i < 4; ++i) bg.push_back((crude_json::number)s.backgroundColor[i]);
  o["backgroundColor"] = crude_json::value(bg);
  return crude_json::value(o);
}

// Read one state. Missing keys keep their Default value (TiXL's per-field defaults), so a partial /
// forward-extended object loads cleanly (a future camera block on an old reader is simply ignored).
OutputWindowState stateFromJson(const crude_json::value& v) {
  OutputWindowState s;  // starts at Defaults
  if (!v.is_object()) return s;
  if (v.contains("isPinned") && v["isPinned"].is_boolean())
    s.isPinned = v["isPinned"].get<bool>();
  if (v.contains("pinnedNode") && v["pinnedNode"].is_number())
    s.pinnedNode = (int)v["pinnedNode"].get<crude_json::number>();
  if (v.contains("resolutionTitle") && v["resolutionTitle"].is_string())
    s.resolutionTitle = v["resolutionTitle"].get<crude_json::string>();
  if (v.contains("resolutionWidth") && v["resolutionWidth"].is_number())
    s.resolutionWidth = (int)v["resolutionWidth"].get<crude_json::number>();
  if (v.contains("resolutionHeight") && v["resolutionHeight"].is_number())
    s.resolutionHeight = (int)v["resolutionHeight"].get<crude_json::number>();
  if (v.contains("resolutionUseAsAspectRatio") && v["resolutionUseAsAspectRatio"].is_boolean())
    s.resolutionUseAsAspectRatio = v["resolutionUseAsAspectRatio"].get<bool>();
  if (v.contains("backgroundColor") && v["backgroundColor"].is_array()) {
    const auto& arr = v["backgroundColor"].get<crude_json::array>();
    for (int i = 0; i < 4 && i < (int)arr.size(); ++i)
      if (arr[i].is_number()) s.backgroundColor[i] = (float)arr[i].get<crude_json::number>();
  }
  return s;
}
}  // namespace

std::string OutputWindowStore::toJson() const {
  crude_json::array arr;
  for (const OutputWindowState& s : states_) arr.push_back(stateToJson(s));
  crude_json::object root;
  root["version"]       = (crude_json::number)1;
  root["OutputWindows"] = crude_json::value(arr);
  return crude_json::value(root).dump(2);
}

bool OutputWindowStore::fromJson(const std::string& json) {
  states_.clear();
  crude_json::value root = crude_json::value::parse(json);
  if (root.is_discarded() || !root.is_object()) return false;
  if (root.contains("OutputWindows") && root["OutputWindows"].is_array()) {
    for (auto& e : root["OutputWindows"].get<crude_json::array>())
      states_.push_back(stateFromJson(e));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Disk persistence (per-project sidecar JSON; mirrors user_settings::save/load).
// ---------------------------------------------------------------------------
bool OutputWindowStore::save(const std::string& path) const {
  if (path.empty()) return false;
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  f << toJson();
  return (bool)f;
}

bool OutputWindowStore::load(const std::string& path) {
  states_.clear();
  std::ifstream f(path);
  if (!f) return true;  // missing sidecar => Defaults (no error: project never saved view state)
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string contents = ss.str();
  if (contents.empty()) return true;  // empty file => Defaults
  return fromJson(contents);          // present-but-corrupt => false (states_ left empty by fromJson)
}

// "<project>.swproj" -> "<project>.swproj.ui". Empty in -> empty out (anon project: nothing to write).
std::string outputWindowStatePathFor(const std::string& projectPath) {
  if (projectPath.empty()) return "";
  return projectPath + ".ui";
}

// ---------------------------------------------------------------------------
// The live process store + restore latch + document hooks.
// ---------------------------------------------------------------------------
OutputWindowStore& outputWindowStore() {
  static OutputWindowStore g;  // Meyers singleton; constructed before any frame (mirrors settings()).
  return g;
}

bool& outputWindowStateRestorePending() {
  static bool pending = false;
  return pending;
}

void saveOutputWindowStateFor(const std::string& projectPath) {
  const std::string sidecar = outputWindowStatePathFor(projectPath);
  if (sidecar.empty()) return;            // anon/unsaved project: nothing to persist
  outputWindowStore().save(sidecar);      // best-effort (a failed UI-state write must not fail Save)
}

void loadOutputWindowStateFor(const std::string& projectPath) {
  const std::string sidecar = outputWindowStatePathFor(projectPath);
  // A missing/empty/corrupt sidecar leaves the store at Defaults (load() clears then keeps empty),
  // so opening a project WITHOUT saved view state restores TiXL defaults — never the prior project's
  // globals. Arm the latch either way so the ui resets its session globals on the next frame.
  outputWindowStore().load(sidecar);      // empty path => Defaults (load("") -> ifstream fails -> true)
  outputWindowStateRestorePending() = true;
}

void resetOutputWindowStateToDefaults() {
  outputWindowStore().setState(OutputWindowState{});
  outputWindowStateRestorePending() = true;
}

}  // namespace sw::settings
