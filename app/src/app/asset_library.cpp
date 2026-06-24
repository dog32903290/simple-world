// app/asset_library — implementation. See asset_library.h for the design + the TiXL behavior it
// matches + the NAMED FORK (click does what TiXL's drop does: create the load-op).
#include "app/asset_library.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "app/asset_index.h"         // buildAssetIndex (project missing-reference list)
#include "platform/image_decode.h"   // assetLibraryRoot + resolveAssetPath (app → platform is legal)

namespace fs = std::filesystem;

namespace sw::assetlib {

namespace {

// Lowercase a string in place-return (extension normalization; ASCII fold is enough for file exts).
std::string toLowerAscii(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

}  // namespace

std::vector<AvailableAsset> enumerateAvailableAssets(const std::string& assetRoot) {
  std::vector<AvailableAsset> out;
  if (assetRoot.empty()) return out;

  std::error_code ec;
  const fs::path root = fs::path(assetRoot);
  if (!fs::is_directory(root, ec)) return out;  // unset/missing root → empty, never a crash

  // Recursive walk. recursive_directory_iterator with the error_code overload never throws on a bad
  // entry (it sets ec and we keep going) — a permission hiccup on one file can't sink the whole list.
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied,
                                                  ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;  // iterator itself errored → stop (we return what we have so far)
    const fs::directory_entry& entry = *it;

    // Skip hidden entries (leading '.') — and if it's a hidden DIRECTORY, don't descend into it.
    const std::string fname = entry.path().filename().string();
    if (!fname.empty() && fname[0] == '.') {
      if (entry.is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file(ec)) continue;  // directories/symlinks-to-dir: not assets themselves

    // Relative path under the root, with forward slashes, becomes the `Lib:` key body. (Walking
    // root/images/basic/x.png → "images/basic/x.png" → key "Lib:images/basic/x.png".)
    const fs::path rel = fs::relative(entry.path(), root, ec);
    if (ec || rel.empty()) continue;
    std::string relStr = rel.generic_string();  // generic = '/' separators on every platform

    AvailableAsset a;
    a.key = "Lib:" + relStr;
    std::string ext = entry.path().extension().string();  // ".png" → "png"
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    a.extension = toLowerAscii(ext);
    a.exists = true;
    out.push_back(std::move(a));
  }

  // Deterministic order for the UI list + the selftest's exact-set assertion.
  std::sort(out.begin(), out.end(),
            [](const AvailableAsset& x, const AvailableAsset& y) { return x.key < y.key; });
  return out;
}

std::vector<AvailableAsset> availableLibraryAssets() {
  return enumerateAvailableAssets(platform::assetLibraryRoot());
}

std::vector<assetidx::AssetRef> projectMissingAssets(const SymbolLibrary& lib) {
  return assetidx::buildAssetIndex(lib, platform::resolveAssetPath).missing();
}

SymbolChild makeLoadImageChild(const std::string& assetKey, float x, float y) {
  SymbolChild c;
  c.id = 0;                  // caller (UI) assigns the real id via nextFreeChildId before pushing
  c.symbolId = "LoadImage";  // image asset-type's primary operator (= TiXL AssetType.PrimaryOperators)
  c.x = x;
  c.y = y;
  // The per-instance Path override = the chosen asset key. This is the SAME seam the runtime reads
  // (point_ops_loadimage.cpp resolveSourceTexture: strParams["Path"]), so the created op decodes THIS
  // asset, not the spec default. Empty overrides would mean "use the committed default" — wiring the
  // key here is what makes the click pick the clicked asset.
  c.strOverrides["Path"] = assetKey;
  return c;
}

}  // namespace sw::assetlib
