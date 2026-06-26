#include "app/theme_registry.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "crude_json.h"  // third_party/imgui-node-editor (same include path as user_settings)

namespace fs = std::filesystem;

namespace sw::theme {

const char* ThemeRegistry::kFactoryName = "Factory";

// ---------------------------------------------------------------------------
// ColorTheme JSON round-trip.
//   { "version":1, "name":..., "author":..., "colors": { "Field":[r,g,b,a], ... } }
// ---------------------------------------------------------------------------
std::string ColorTheme::toJson() const {
  crude_json::object colorsObj;
  for (const auto& [key, rgba] : colors) {
    crude_json::array arr;
    for (float v : rgba) arr.push_back(crude_json::value((crude_json::number)v));
    colorsObj[key] = crude_json::value(arr);
  }
  crude_json::object root;
  root["version"] = (crude_json::number)1;
  root["name"]    = crude_json::value(name);
  root["author"]  = crude_json::value(author);
  root["colors"]  = crude_json::value(colorsObj);
  return crude_json::value(root).dump(2);
}

bool ColorTheme::fromJson(const std::string& json) {
  name = "untitled";
  author = "unknown";
  colors.clear();
  crude_json::value root = crude_json::value::parse(json);
  if (root.is_discarded() || !root.is_object()) return false;
  if (root.contains("name") && root["name"].is_string())
    name = root["name"].get<crude_json::string>();
  if (root.contains("author") && root["author"].is_string())
    author = root["author"].get<crude_json::string>();
  if (root.contains("colors") && root["colors"].is_object()) {
    for (const auto& [key, val] : root["colors"].get<crude_json::object>()) {
      if (!val.is_array()) continue;
      const auto& a = val.get<crude_json::array>();
      if (a.size() != 4) continue;  // only well-formed RGBA quadruples
      Rgba rgba{};
      bool ok = true;
      for (size_t i = 0; i < 4; ++i) {
        if (!a[i].is_number()) { ok = false; break; }
        rgba[i] = (float)a[i].get<crude_json::number>();
      }
      if (ok) colors[key] = rgba;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Registry.
// ---------------------------------------------------------------------------
void ThemeRegistry::addFactory() {
  // Factory theme = compiled-in defaults. Its colors map is EMPTY by design: an empty map applied via
  // ui::theme::applyColors() falls back to every field's TiXL default (byte-identical to the populated
  // FactoryTheme). The editor fills display values from ui::theme::defaultColorMap() for empty fields.
  ColorTheme factory;
  factory.name = kFactoryName;
  factory.author = "TiXL";
  themes_.push_back(std::move(factory));
}

bool ThemeRegistry::loadFile(const std::string& filepath) {
  std::ifstream f(filepath);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  ColorTheme t;
  if (!t.fromJson(ss.str())) return false;
  // A user file named "Factory" would shadow the built-in factory in find()/dropdown; skip it.
  if (t.name == kFactoryName) return false;
  themes_.push_back(std::move(t));
  return true;
}

void ThemeRegistry::loadThemes(const std::string& themeFolder) {
  themes_.clear();
  addFactory();

  std::error_code ec;
  if (!fs::exists(themeFolder, ec) || !fs::is_directory(themeFolder, ec)) return;
  std::vector<std::string> files;
  for (const auto& entry : fs::directory_iterator(themeFolder, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".json") continue;
    files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());  // deterministic load order
  for (const std::string& fp : files) loadFile(fp);
}

std::vector<std::string> ThemeRegistry::themeNames() const {
  std::vector<std::string> names;
  names.reserve(themes_.size());
  for (const ColorTheme& t : themes_) names.push_back(t.name);
  return names;
}

const ColorTheme* ThemeRegistry::find(const std::string& name) const {
  for (const ColorTheme& t : themes_)
    if (t.name == name) return &t;
  return nullptr;
}

const ColorTheme& ThemeRegistry::userOrFactory(const std::string& selectedName) const {
  if (!selectedName.empty()) {
    if (const ColorTheme* t = find(selectedName)) return *t;
  }
  return factory();
}

bool ThemeRegistry::saveTheme(const std::string& themeFolder, ColorTheme theme) {
  // Trim + default the name (ThemeHandling.SaveTheme:36-40).
  auto trim = [](std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
  };
  theme.name = trim(theme.name);
  if (theme.name.empty()) theme.name = "untitled";
  // Refuse to write a file named "Factory" — it would shadow the built-in on reload (named guard).
  bool writeOk = true;
  if (theme.name == kFactoryName) {
    writeOk = false;
  } else {
    std::error_code ec;
    fs::create_directories(themeFolder, ec);
    std::ofstream f(themeFilePath(themeFolder, theme.name), std::ios::trunc);
    if (f) {
      f << theme.toJson();
      writeOk = (bool)f;
    } else {
      writeOk = false;
    }
  }
  loadThemes(themeFolder);  // reload so themes()/find() see the new file (ThemeHandling.SaveTheme:48)
  return writeOk;
}

bool ThemeRegistry::deleteTheme(const std::string& themeFolder, const std::string& name) {
  if (name == kFactoryName || name.empty()) return false;
  std::error_code ec;
  const std::string fp = themeFilePath(themeFolder, name);
  bool existed = fs::exists(fp, ec);
  if (existed) fs::remove(fp, ec);
  loadThemes(themeFolder);  // revert list to factory + survivors (ThemeHandling.DeleteTheme:61-63)
  return existed && !ec;
}

// ---------------------------------------------------------------------------
// Singleton + paths.
// ---------------------------------------------------------------------------
ThemeRegistry& registry() {
  static ThemeRegistry g;
  return g;
}

std::string defaultThemeFolder() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/.simple_world/Themes";
}

std::string themeFilePath(const std::string& themeFolder, const std::string& name) {
  return themeFolder + "/" + name + ".json";
}

void loadThemes() { registry().loadThemes(defaultThemeFolder()); }

}  // namespace sw::theme
