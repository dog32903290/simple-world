// app/asset_index — implementation. See asset_index.h for the design + the asset model it rides.
#include "app/asset_index.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace sw::assetidx {

namespace {

// The asset-key discriminant: a strOverride is an asset REFERENCE iff its value carries TiXL's
// resource-pack prefix. Generic non-asset string overrides (VariableName, etc., also stored in
// strOverrides) lack it and are excluded. SSOT for "is this string an asset key".
constexpr const char* kLibPrefix = "Lib:";
bool isLibKey(const std::string& s) { return s.rfind(kLibPrefix, 0) == 0; }

bool fileExists(const std::string& absPath) {
  if (absPath.empty()) return false;
  std::error_code ec;  // non-throwing exists (a bad path → false, never an exception)
  return fs::exists(absPath, ec) && fs::is_regular_file(absPath, ec);
}

}  // namespace

bool AssetIndex::references(const std::string& key) const {
  for (const auto& r : refs)
    if (r.key == key) return true;
  return false;
}

bool AssetIndex::isMissing(const std::string& key) const {
  for (const auto& r : refs)
    if (r.key == key) return !r.resolves;
  return false;  // not referenced at all → not "missing"
}

std::vector<AssetRef> AssetIndex::missing() const {
  std::vector<AssetRef> out;
  for (const auto& r : refs)
    if (!r.resolves) out.push_back(r);
  return out;
}

AssetIndex buildAssetIndex(const SymbolLibrary& lib,
                           std::string (*resolveLibKey)(const std::string&)) {
  // Collect deduped Lib: image keys across EVERY symbol's children (every instance project-wide).
  // std::map keeps them lexicographically ordered + deduped in one structure.
  std::map<std::string, bool> libKeys;  // key -> resolves
  for (const auto& [symId, sym] : lib.symbols) {
    (void)symId;
    for (const auto& child : sym.children) {
      for (const auto& [slotId, value] : child.strOverrides) {
        (void)slotId;
        if (isLibKey(value)) libKeys.emplace(value, false);  // first-seen wins; dedup is by key
      }
    }
  }

  AssetIndex idx;
  idx.refs.reserve(libKeys.size() + 1);
  for (auto& [key, resolves] : libKeys) {
    const std::string abs = resolveLibKey ? resolveLibKey(key) : std::string();
    resolves = fileExists(abs);
    idx.refs.push_back(AssetRef{key, AssetKind::LibImage, resolves});
  }

  // The one external asset: the soundtrack (an absolute path used as-is, NOT a Lib: key). Appended
  // last so the order is [Lib images sorted..., soundtrack]. Deduped against itself trivially (one).
  const std::string& st = lib.composition.soundtrackPath;
  if (!st.empty()) {
    idx.refs.push_back(AssetRef{st, AssetKind::Soundtrack, fileExists(st)});
  }

  return idx;
}

}  // namespace sw::assetidx
